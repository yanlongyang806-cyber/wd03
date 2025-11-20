#include "aiCivilian.h"
#include "aiCivilianPrivate.h"
#include "aiCivilianTraffic.h"
#include "aiStructCommon.h"
#include "cmdparse.h"
#include "Entity.h"
#include "EntityGrid.h"
#include "gslHeatmaps.h"
#include "gslPatrolRoute.h"
#include "LineDist.h"
#include "MemoryPool.h"
#include "PhysicsSDK.h"
#include "rand.h"
#include "StringCache.h"
#include "TriCube/vec.h"
#include "utilitiesLib.h"
#include "wlPhysicalProperties.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "WorldBounds.h"
#include "wlEncounter.h"
#include "wlVolumes.h"
#include "bounds.h"

#include "aiCivLegGeneration_c_ast.h"
#include "aiCivilianPrivate_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include <stdio.h>
#include <math.h>
#include "Materials.h"
#include "Capsule.h"
#include "wlSavedTaggedGroups.h"
#include "AutoGen/wlSavedTaggedGroups_h_ast.h"

// ---------------------------------------------------------------
typedef struct AICivilianPathGenLine AICivilianPathGenLine;
typedef struct AICivilianPathGenBox AICivilianPathGenBox;
typedef struct AICivilianPathGenBoxConnection AICivilianPathGenBoxConnection;
typedef struct ACGSegmentGroup ACGSegmentGroup;
typedef struct GenNodeIndicies GenNodeIndicies;

// ---------------------------------------------------------------
extern EntityRef aiDebugEntRef;
extern EAICivilianType g_eHeatMapSet;
extern F32 s_fCivTrolleyStopDistance;

#define ACG_INTERSECT_DIST 200
#define ACG_MINIMUM_CIRCUIT_DIST 200
#define NODE_BLOCK_BITS 8
#define NODE_BLOCK_SIZE (1<<NODE_BLOCK_BITS)

#define ACG_LEG_ANGLE_TOL 0.999
#define ACG_POINT_STEAL_BIAS 0.45

#define ACG_ISECT_MID_DANGLING_THRESHOLD 25.0
#define MAX_PATHLEG_MID_INTERSECTIONS 2

#define CIV_FILE_SUFFIX "civ"
#define CIV_INFO_SUFFIX "map_info"
#define CIV_DEF_SUFFIX "map_def"
#define CIV_NODE_SUFFIX "civ_node"


#define ACG_DEBUGPRINT
// #define LEG_DESTROY_PARANOID

static int s_acgProcessVersion = 44;
static int s_civlogcrc = 0;

static int s_debugDrawDeletedLegs = 0;
static int s_acgSkip_GroundCoplanarCheck = 0;
static int s_acgSkip_RemoveLowCircutCheck = 0;
static int s_acgSkip_LegSplit = 0;
static int s_legmapIncludePartition = 0;
static int s_acg_forceRegenPartial = 0;
static int s_acg_forceLoadPartial = 0;

static int s_acg_regenForcedLegsOnly = 0;

static const char *s_physConcrete = NULL;
static const char *s_physAsphalt = NULL;
static const char *s_physSidewalk = NULL;
static const char *s_physIntersection = NULL;
static const char *s_physStreetcar = NULL;
static const char *s_physParking = NULL;

static int s_enableCivilianGeneration = 0;
static int s_ignoreUptodate = 0;
static int acg_d_lines = 0;
static int acg_d_edges = 0;
static int acg_d_pairs = 0;
static int acg_d_split = 0;
static int acg_d_intersectionMaterial = 0;
static int acg_d_intersectionCorrecting = 0;
static Vec3 s_acgDebugPos;
static int acg_d_pos = 0;
static int s_sendCivQuads = 1; // I like legs showing quads a lot more!
static int s_acgShowGroundCoplanar = 1;
static int s_testLegOBB = 0;

int acg_d_nodeGenerator = 0;
static Vec3 s_acgNodeGeneratorPos;

static int s_cachePartialFiles = 0;
static int s_bSkipDeadEndOneWayStreets = 0;

static F32 s_acg_fLocalRegenHalfExtent = 500.0f;
static F32 s_acg_fCrosswalkDefaultWidth = 7.0f;

AUTO_CMD_INT(s_sendCivQuads, sendCivQuads);
AUTO_CMD_INT(s_testLegOBB, acgTestLegOBB);
AUTO_CMD_INT(s_civlogcrc, civlogcrc);
AUTO_CMD_INT(s_ignoreUptodate, forcecivgen);
AUTO_CMD_INT(s_enableCivilianGeneration, civgen);
AUTO_CMD_INT(acg_d_lines, acg_d_lines);
AUTO_CMD_INT(acg_d_edges, acg_d_edges);
AUTO_CMD_INT(acg_d_pairs, acg_d_pairs);
AUTO_CMD_INT(acg_d_pos, acg_d_pos);
AUTO_CMD_INT(acg_d_split, acg_d_split);
AUTO_CMD_INT(acg_d_intersectionMaterial, acg_d_intersectionMaterial);
AUTO_CMD_INT(acg_d_intersectionCorrecting, acg_d_intersectionCorrecting);
AUTO_CMD_INT(s_acgShowGroundCoplanar, acgShowGroundCoplanar);

AUTO_CMD_INT(s_cachePartialFiles, acgCachePartialFiles);
AUTO_CMD_INT(acg_d_nodeGenerator, acg_d_nodeGenerator);

AUTO_CMD_INT(s_legmapIncludePartition, aiCivLegmapIncludePartition);
AUTO_CMD_INT(s_acgSkip_GroundCoplanarCheck, acgSkipGroundCoplanar);
AUTO_CMD_INT(s_acgSkip_RemoveLowCircutCheck, acgSkipRemoveLowCircutCheck);
AUTO_CMD_INT(s_acgSkip_LegSplit, acgSkipLegSplit);
AUTO_CMD_INT(s_debugDrawDeletedLegs, aiCivDrawDeletedLegs);
AUTO_CMD_FLOAT(s_acg_fLocalRegenHalfExtent, acgLocalRegenHalfExtent);
AUTO_CMD_INT(s_bSkipDeadEndOneWayStreets, acgSkipDeadEndOneWayStreets);

// ---------------------------------------------------------------
typedef enum EConnectionPlug
{
	EConnectionPlug_NONE = -1,
	EConnectionPlug_START = 0,
	EConnectionPlug_END,
	EConnectionPlug_MID,
} EConnectionPlug;
// ---------------------------------------------------------------
typedef enum AILegFlow {
	ALF_NEXT,
	ALF_PREV,
	ALF_MID,
} AILegFlow;


// ---------------------------------------------------------------
// .CIV - The leg civ bin file
// ---------------------------------------------------------------

// ---------------------------------------------------------------
// The leg civ bin file
AUTO_STRUCT;
typedef struct AICivilianPathInfo {
	AICivilianPathLeg **legs;
	AICivilianPathLeg **deletedLegs;
	ACGDelNode **deletedNodes;
	ACGDelLine **deletedLines;
	AICivilianPathIntersection **intersects;
	AICivilianPathPoint **pathPoints;
	AICivilianPathPointIntersection **pathPointIntersections;
	AICivIntersectionCurve **eaIntersectionCurves;
} AICivilianPathInfo;

// ---------------------------------------------------------------
typedef struct AICivilianPathGenNode {
	union {
		struct {
			S32 type : 8;
			S32 y_coord : 24;
		};
		S32 type_y_coord;
	};
	unsigned char block_x;
	unsigned char block_z;
	unsigned char grid_x;
	unsigned char grid_z;

	union {
		AICivilianPathGenLine *line;
		AICivilianPathGenBox *box;
		int userInt;
		F32 userFloat;
		char touched;
	};
} AICivilianPathGenNode;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct ACGDelNode {
	EAICivilianLegType type;  AST(INT)
	Vec3 pos;
	CivGenState delState;
	int delSubState;
} ACGDelNode;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct ACGDelNodeList {
	ACGDelNode **nodes;
} ACGDelNodeList;

// ---------------------------------------------------------------
typedef struct AICivilianPathGenNodeBlock {
	int grid_x, grid_z;
	AICivilianPathGenNode **genNodeList;
	AICivilianPathGenNode *nodes[NODE_BLOCK_SIZE][NODE_BLOCK_SIZE];
} AICivilianPathGenNodeBlock;

// ---------------------------------------------------------------
typedef struct AICivilianPathGenBox {
	AICivilianPathGenNode *ll;
	AICivilianPathGenNode *ul;
	AICivilianPathGenNode *lr;
	AICivilianPathGenNode *ur;

	AICivilianPathGenBoxConnection **connections;
} AICivilianPathGenBox;

// ---------------------------------------------------------------
typedef struct AICivilianPathGenBoxConnection {
	Vec3 start;
	Vec3 end;

	AICivilianPathGenBox *box1;
	AICivilianPathGenBox *box2;
} AICivilianPathGenBoxConnection;

// ---------------------------------------------------------------
typedef struct AICivilianPathGenPair {
	AICivilianPathGenLine *pair_line;
	AICivilianPathGenNode *pair_start;
	AICivilianPathGenNode *pair_end;
	AICivilianPathGenNode *self_start;
	AICivilianPathGenNode *self_end;

	F32 medianDist;
	F32 minDist;
} AICivilianPathGenPair;

// ---------------------------------------------------------------
typedef struct AICivilianPathGenLine {
	AICivilianPathGenNode **nodes;

	AICivilianPathLeg *leg;
	AICivilianPathGenPair **pairs;

	F32 legMaxDist;
	F32 fMergeError;
	Vec3 perp;
	CivGenState touched;
	U32 clean_up : 1;
} AICivilianPathGenLine;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct ACGDelLine {
	EAICivilianLegType type;  AST(INT)
	Vec3 start;
	Vec3 end;
	CivGenState delState;
} ACGDelLine;

// ---------------------------------------------------------------
AUTO_STRUCT;
typedef struct ACGDelLineList {
	ACGDelLine **list;
} ACGDelLineList;

// ---------------------------------------------------------------
typedef struct ACGNodeBlocks {
	AICivilianPathGenNodeBlock **aiCivGenNodeBlocks;

	IVec2 grid_min;
	IVec2 grid_max;
	Vec3 world_min;
	Vec3 world_max;

	U32 valid : 1;
} ACGNodeBlocks;

// ---------------------------------------------------------------
struct {
	const CivGenState state;

	struct {
		int count;
	} block;
	struct {
		AICivilianPathGenNode **nodes;
	} areaClean;
	struct {
		int reset;
		AICivilianPathGenNode *node;
		AICivilianPathGenNode *loop_start;
	} lines;
	struct {
		int sorted;
		int i;
	} minError;
	struct {
		int i;
	} lowArea;
	struct {
		int i;
		int sorted;
	} pairs;
	struct {
		int i;
		int sorted;
	} lanes;
	struct {
		int i;
	} legs;
	struct {
		int i;
	} orphans;
	struct {
		int i;
		AICivilianPathLeg **connections;
		F32*		eaNodeLinePos;
		GenNodeIndicies **eaGenNodes;
	} inter;
	struct {
		AICivilianPathLeg **eaNearbyLegs;
	} isect;
	struct {
		int i;
	} dist;
	struct {
		int set_count;
		int missed_mid_connections;
		ACGSegmentGroup **eaNeighborList;
		ACGSegmentGroup **eaLeftNeighborList;
		ACGSegmentGroup **eaRightNeighborList;
	} split;

	struct {
		AICivilianPathLeg **eaLegList;
		AICivilianPathLeg **eaStartLegIntersections;
		AICivilianPathLeg **eaEndLegIntersections;
	} int1;

	WorldCivilianGenerator **lastWorldList;
	AICivilianPathGenNodeBlock **genBlockList;
	AICivilianPathGenNode **genStartList;
	AICivilianPathGenNode **edgeStartList;
	AICivilianPathGenLine **lineList;
	AICivilianPathGenBox **boxList;
	AICivilianPathGenBox **boxOpenList;
	AICivilianPathGenBox **boxClosedList;
	AICivilianPathLeg **legList;
	AICivilianPathLeg **deletedLegs;
	ACGDelNode **deletedNodes;
	ACGDelLine **deletedLines;

	AICivilianPathPoint **eaPathPoints;
	AICivilianPathPointIntersection **eaPathPointIntersections;

	AICivIntersectionCurve **eaIntersectionCurves;

	SavedTaggedGroups *pTagData;

} s_acgProcess;


static AICivilianRuntimePathInfo s_acgPathInfo = {0};

static bool s_acgSkipTable[CGS_COUNT];

// ---------------------------------------------------------------
MP_DEFINE(AICivilianPathGenNodeBlock);
MP_DEFINE(AICivilianPathGenNode);
MP_DEFINE(AICivilianPathGenLine);
MP_DEFINE(AICivilianPathGenPair);
MP_DEFINE(AICivilianPathLeg);

// ---------------------------------------------------------------
static CivilianGenerator **destroyedGens;
EntityRef aiCivDebugRef;
static AICivRegenReport *s_pRegenReport = NULL;


// ---------------------------------------------------------------
typedef struct ACGNodeFileContents
{
	IVec2 grid_min;
	IVec2 grid_max;
	S32 edgeOtherCount;
	AICivilianPathGenNode *paOtherNodes;
	S32 edgeStartCount;
	AICivilianPathGenNode *paEdgeStartNodes;

} ACGNodeFileContents;

// ---------------------------------------------------------------
typedef struct ACGLineNodeFile
{
	unsigned char block_x;
	unsigned char block_z;
	unsigned char grid_x;
	unsigned char grid_z;
} ACGLineNodeFile;

// ---------------------------------------------------------------
typedef struct ACGLineFile
{
	S32 nodeCount;
	ACGLineNodeFile *paNodes;
	CivGenState touched;
} ACGLineFile;

// ---------------------------------------------------------------
typedef struct ACGLineFileContents
{
	S32 lineCount;
	ACGLineFile *paLines;

} ACGLineFileContents;

static ACGNodeFileContents s_acgFileNodeContents;
static ACGLineFileContents s_acgFileLineContents;


static ACGNodeBlocks s_acgNodeBlocks;


int colors[] = {0xFFFF0000, 
				0xFF00FF00,  // AI_CIV_PERSON
				0xFF0000FF, // AI_CIV_CAR
				0xFFFFFF00, // AI_CIV_PARKING
				0xFFFF0000, // AI_CIV_STREETCAR
				0xFFFF8080 // AI_CIV_INTERSECTION
				};
int offsetsNoCorners[][2] =	{{-1, 0},
							 {1, 0},
							 {0, -1},
							 {0, 1}};

int offsetsCorners[][2] =  {{-1,-1},
							{-1,0},
							{-1,1},
							{0,1},
							{1,1},
							{1,0},
							{1,-1},
							{0,-1}};


// struct of post line culling data
static struct
{
	S32		do_culling;
	S32		gridX;
	S32		gridZ;
	S32		xRun;
	S32		ZRUN;

	Vec3	boxMin;
	Vec3	boxMax;
} s_acg_d_culling;

// ---------------------------------------------------------------
static void acgGenerate(int iPartitionIdx);
static CivilianGenerator* acgGeneratorCreate(Vec3 world_pos);
static void acgCivilianVolumeCreateFunc(WorldVolumeEntry *entry);
static void acgCivilianVolumeDestroyFunc(WorldVolumeEntry *entry);
static void acgPlayableDestroyFunc(WorldVolumeEntry *ent);
static void acgPlayableCreateFunc(WorldVolumeEntry *ent);

static void acgGeneratorDestroy(CivilianGenerator *civgen);
static void acgProcessShutdown(void);
static void acgInitData(void);
static int acgProcessFinal(Entity *debugger);
static int calcLegOrient(const AICivilianPathLeg *leg, const AICivilianPathLeg *other_leg);
bool aiCivHeatmapGatherCivs(gslHeatMapCBHandle *pHandle, char **ppErrorString); // defined in aiCivilian.c, should be moved to .h
static bool acgHeatmapGatherLegs(gslHeatMapCBHandle *pHandle, char **ppErrorString);

static bool acgSplitLegsOnObjects(int iPartitionIdx, Entity *debugger);
static void acgNodeBlocksFree();
static void acgClipLegsToForcedLegVolumes();
static void acgClipForcedLegsToDisableVolumes();
static void acgClipIntersectingLegs();
static void acgCreateForcedVolumeLegs(int iPartitionIdx);
static void acgRemoveSidewalksFromMedians();
static void acgValidateAllLegConnections(bool bFixErrors);
static void acgMidIntersectionGetLegAndIsectLeg(const AICivilianPathIntersection *mid_acpi, PathLegIntersect **intPli, PathLegIntersect **legPli);
static void acgIntersection_ProcessLeg(int iPartitionIdx, AICivilianPathLeg *pLeg, S32 bStart);
static void acgLeg_GetLanePos(const AICivilianPathLeg *leg, S32 lane, F32 fStartToEndRatio, S32 forward, Vec3 vOutPos);
static void acgLegGenReport_AddLocation(const Vec3 vPos, const char *pchProblemDescription, S32 bIsAnError,F32 fProblemAreaSize);
static bool acgLineSegVsBox(const Vec3 vPt1, const Vec3 vPt2, const Vec3 vBoxMin, const Vec3 vBoxMax, Mat4 BoxMtx, Vec3 vIsect);

// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenNode* acgNodeAlloc(void)
{
	return MP_ALLOC(AICivilianPathGenNode);
}

static void acgNodeFree(AICivilianPathGenNode *node)
{
	MP_FREE(AICivilianPathGenNode, node);
}

AICivilianPathGenLine *acgLineAlloc(void)
{
	return MP_ALLOC(AICivilianPathGenLine);
}

static void acgLineFree(AICivilianPathGenLine *line)
{
	MP_FREE(AICivilianPathGenLine, line);
}


AICivilianPathGenPair* acgPairAlloc(void)
{
	return MP_ALLOC(AICivilianPathGenPair);
}

static void acgPairFree(AICivilianPathGenPair *pair)
{
	MP_FREE(AICivilianPathGenPair, pair);
}

AICivilianPathGenNodeBlock* acgBlockAlloc(void)
{
	return MP_ALLOC(AICivilianPathGenNodeBlock);
}

static void acgBlockFree(AICivilianPathGenNodeBlock *block)
{
	MP_FREE(AICivilianPathGenNodeBlock, block);
}

AICivilianPathLeg* acgPathLeg_Alloc(void)
{
	return StructCreate(parse_AICivilianPathLeg);
}


static void acgPathLeg_Free(AICivilianPathLeg *leg)
{
	if (leg)
	{
		// destroy the NO_AST data
		eaDestroy(&leg->midInts);
		stashTableDestroy(leg->flowStash);
		stashTableDestroy(leg->tracked_ents);
		eaDestroy(&leg->eaCrosswalkLegs);
		eaDestroy(&leg->eaCrosswalkUsers);
		if (leg->pXTrafficQuery)
			aiCivCrossTrafficManagerReleaseQuery(&leg->pXTrafficQuery);
	}
	StructDestroySafe(parse_AICivilianPathLeg, &leg);
}

static PathLegIntersect* acgPathLegIntersect_Alloc()
{
	return calloc(1, sizeof(PathLegIntersect));
}

void aiCivPathLegStopLight_Free(PathLegStopLight* pli);

static void acgPathLegIntersect_Free(PathLegIntersect *pli)
{
	if (pli)
	{
		eaDestroyEx(&pli->eaStopLights, aiCivPathLegStopLight_Free);
		free(pli);
	}
}

static AICivilianPathIntersection* acgPathIntersection_Alloc(void)
{
	return calloc(1, sizeof(AICivilianPathIntersection));
}

static void acgPathIntersection_Free(AICivilianPathIntersection *pacpi)
{
	if (pacpi)
	{
		eaDestroyEx(&pacpi->legIntersects, acgPathLegIntersect_Free);
		stashTableDestroy(pacpi->tracked_ents);

		eaDestroy(&pacpi->eaStopSignUsers);
				
		free(pacpi);
	}
}

// ------------------------------------------------------------------------------------------------------------------
// AICivPathInfo_LegDefTags
// ------------------------------------------------------------------------------------------------------------------

static void aiCivilianPathInfo_Free(AICivilianPathInfo **ppPathInfo)
{
	if(*ppPathInfo)
	{
		AICivilianPathInfo *pPathInfo = *ppPathInfo;

		eaDestroyEx(&pPathInfo->legs, acgPathLeg_Free);
		eaDestroyEx(&pPathInfo->deletedLegs, acgPathLeg_Free);
		eaDestroyEx(&pPathInfo->pathPoints, NULL);
		eaDestroyEx(&pPathInfo->pathPointIntersections, NULL);
		eaDestroyEx(&pPathInfo->eaIntersectionCurves, NULL);
		

		eaDestroyEx(&pPathInfo->intersects, acgPathIntersection_Free);

		StructDestroySafe(parse_AICivilianPathInfo, ppPathInfo);
	}

}


// ------------------------------------------------------------------------------------------------------------------
static CivilianGenerator* acgGeneratorCreate(Vec3 world_pos)
{
	CivilianGenerator *civgen = StructCreate(parse_CivilianGenerator);

	copyVec3(world_pos, civgen->world_pos);

	return civgen;
}

static void acgGeneratorDestroy(CivilianGenerator *civgen)
{
	eaPush(&destroyedGens, civgen);
}

static void acgDestroyPathGenLine(AICivilianPathGenLine *line)
{
	eaDestroy(&line->nodes);
	eaDestroyEx(&line->pairs, acgPairFree);

	acgLineFree(line);
}

static SavedTaggedGroups* acgGetSavedTaggedGroups()
{
	if (! s_acgProcess.pTagData)
	{
		s_acgProcess.pTagData = wlLoadSavedTaggedGroupsForCurrentMap();
	}

	return s_acgProcess.pTagData;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static U32 acgGetTypeColor(EAICivilianLegType type, int deleted)
{
	U32 alpha = 0xff000000;
	U32 color = 0;

	switch(type)
	{
		xcase EAICivilianLegType_NONE: {
			color = 0x00000000;
		}
		xcase EAICivilianLegType_PERSON: {
			color = 0x0000FF00;
		}
		xcase EAICivilianLegType_CAR: {
			color = 0x000000FF;
		}
		xcase EAICivilianLegType_PARKING: {
			color = 0x00FFFF00;
		}
		xcase EAICivilianLegType_TROLLEY: {
			color = 0x00FF0000;
		}
		xcase EAICivilianLegType_INTERSECTION: {
			color = 0x0000FFFF;
		}

	}

	return alpha | color | (deleted ? 0x00404040 : 0x00000000);
}

// ------------------------------------------------------------------------------------------------------------------
const char* aiCivilianGetSuffixedFileName(int server, int bin, const char* suffix)
{
	static char filename[MAX_PATH];
	char map_path[MAX_PATH];
	char ns[MAX_PATH];
	char rel_path[MAX_PATH];

	sprintf(map_path, "%s.%s", zmapGetFilename(NULL), suffix);
	if(bin)
		strcpy(map_path, worldMakeBinName(map_path));

	if(fileGetNameSpacePath(map_path, ns, rel_path))
	{
		sprintf(filename, "%s:/%s/%s%s", ns, server ? "server" : "", bin ? "/bin/" : "", rel_path);
	}
	else
		sprintf(filename, "%s/%s%s", server ? "server" : "", bin ? "/bin/" : "", map_path);

	return filename;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_CalculateMaxLanes(AICivilianPathLeg *leg)
{
	if(leg->lane_width==0)
		leg->max_lanes = 0;
	else
		leg->max_lanes = ((leg->width - leg->median_width) * 0.5f) / leg->lane_width + 0.2;  // Round if 80% of a leg
	if (leg->max_lanes == 0) 
		leg->bIsOneWay = true;	
}

// ------------------------------------------------------------------------------------------------------------------
// 
void aiCivilian_LoadRuntimePathInfo(AICivilianRuntimePathInfo *pRTPathInfo, const AICivilianMapDef *pMapDef, const AICivilianPathInfo *pCivPathInfo)
{
	int i;
	S32 pathPointListSize = eaSize(&pCivPathInfo->pathPoints);
	S32 intersectionListSize = eaSize(&pCivPathInfo->intersects);
	S32 pathPointIntersetsSize = eaSize(&pCivPathInfo->pathPointIntersections);
	
	// so that we don't spam errors more than once per tag, save a list of the ones we've errored about
	const char **eaTagErrorList = NULL;
	
	// destroy the old runtime pathing info
	aiCivilian_DestroyRuntimePathInfo(pRTPathInfo);
	
	// clone the pCivPathInfo, and set the function to use that one now
	pRTPathInfo->pCivilianPathInfoCopy = StructClone(parse_AICivilianPathInfo, pCivPathInfo);
	pCivPathInfo = pRTPathInfo->pCivilianPathInfoCopy;
	
	// go through the intersections on the AICivilianPathInfo and set all the pointers up
	// and set up the civState struct
	for(i=0; i < intersectionListSize; i++)
	{
		int j;
		AICivilianPathIntersection *intersect = pCivPathInfo->intersects[i];
		
		for(j=0; j<eaSize(&intersect->legIntersects); j++)
		{
			PathLegIntersect *pli = intersect->legIntersects[j];

			pli->leg = pCivPathInfo->legs[pli->legByIndex];

			if (pli->crosswalkByIndex != -1)
			{
				pli->pCrosswalkLeg = pCivPathInfo->legs[pli->crosswalkByIndex];
			}

			// by default mark all these as valid
			pli->continue_isvalid = true;
		}

		intersect->tracked_ents = stashTableCreateInt(5);

		eaPush(&pRTPathInfo->intersects, intersect);
	}

	// next go through the legs
	for(i=0; i<eaSize(&pCivPathInfo->legs); i++)
	{
		int j;
		int added = 1;
		AICivilianPathLeg *leg = pCivPathInfo->legs[i];

		leg->flowStash = stashTableCreateAddress(10);

		// if we have a leg tag, try to find the leg def
		if (leg->pchLegTag)
		{
			leg->pLegDef = aiCivMapDef_GetCivLegDef(pMapDef, leg->pchLegTag);
			if (!leg->pLegDef)
			{
				if (eaFind(&eaTagErrorList, leg->pchLegTag) == -1)
				{
					Errorf("Leg tag (%s) could not be found on the civilian map def file.", leg->pchLegTag);
					eaPush(&eaTagErrorList, leg->pchLegTag);
				}
			}
		}

		// set the leg's lane width and then calculate the lane number
		{
			if (leg->bIsCrosswalk == false)
			{
				leg->lane_width = (leg->type == EAICivilianLegType_CAR) ? pMapDef->carLaneWidth : 3;
				acgLeg_CalculateMaxLanes(leg);
			}
			else
			{
				// crosswalk's use the car's lane width
				leg->lane_width = pMapDef->carLaneWidth;
				if(leg->lane_width==0)
					leg->max_lanes = 0;
				else
					leg->max_lanes = (leg->len * 0.5f) / leg->lane_width + 0.2;  // Round if 80% of a leg
				leg->max_lanes *= 2;
			}
		}
		
		crossVec3(leg->dir, leg->perp, leg->normal);
		normalVec3(leg->normal);

		if(leg->nextByIndex!=-1)
		{
			leg->next = pCivPathInfo->legs[leg->nextByIndex];
			added = stashAddressAddInt(leg->flowStash, leg->next, ALF_NEXT, 1);
			devassert(added);
		}

		if(leg->prevByIndex!=-1)
		{
			leg->prev = pCivPathInfo->legs[leg->prevByIndex];
			added = stashAddressAddInt(leg->flowStash, leg->prev, ALF_PREV, 1);
			devassert(added);
		}

		if(leg->nextIntByIndex!=-1)
		{
			leg->nextInt = pCivPathInfo->intersects[leg->nextIntByIndex];

			for(j=0; j<eaSize(&leg->nextInt->legIntersects); j++)
			{
				PathLegIntersect *pli = leg->nextInt->legIntersects[j];

				if(pli->leg==leg)
					continue;

				added = stashAddressAddInt(leg->flowStash, pli->leg, ALF_NEXT, 1);
				devassert(added);
			}
		}

		if(leg->prevIntByIndex!=-1)
		{
			leg->prevInt = pCivPathInfo->intersects[leg->prevIntByIndex];

			for(j=0; j<eaSize(&leg->prevInt->legIntersects); j++)
			{
				PathLegIntersect *pli = leg->prevInt->legIntersects[j];

				if(pli->leg==leg)
					continue;

				added = stashAddressAddInt(leg->flowStash, pli->leg, ALF_PREV, 1);
				devassert(added);
			}
		}

		for(j=0; j<eaiSize(&leg->midIntsByIndex); j++)
		{
			int k;
			int intersectIndex = leg->midIntsByIndex[j];
			AICivilianPathIntersection *intersect = pCivPathInfo->intersects[intersectIndex];

			devassert(intersect->bIsMidIntersection);
			eaPush(&leg->midInts, intersect);

			for(k=0; k<eaSize(&leg->midInts[j]->legIntersects); k++)
			{
				PathLegIntersect *pli = leg->midInts[j]->legIntersects[k];

				if(pli->leg==leg)
					continue;

				if(vec3IsZero(pli->intersect))  // | of the T
					added = stashAddressAddInt(leg->flowStash, pli->leg, ALF_MID, 1);
				else
					added = stashAddressAddInt(leg->flowStash, pli->leg, calcLegOrient(pli->leg, leg), 1);
				devassert(added);
			}
		}

		for (j = 0; j < eaiSize(&leg->eaCrosswalkLegsByIndex); j++)
		{
			S32 xwalkIndex = leg->eaCrosswalkLegsByIndex[j];
			AICivilianPathLeg *pxwalk = pCivPathInfo->legs[xwalkIndex];
			eaPush(&leg->eaCrosswalkLegs, pxwalk);
		}

		if (leg->crosswalkNearestRoadByIndex != -1)
		{
			leg->pCrosswalkNearestRoad = pCivPathInfo->legs[leg->crosswalkNearestRoadByIndex];
		}
		

		leg->tracked_ents = stashTableCreateInt(5);
				
		if (pMapDef->bEnableLegsWhenPlayerEntersRegion)
		{
			leg->bSpawningDisabled = true;
		}

		eaPush(&pRTPathInfo->legs[leg->type], leg);
	}

	for(i = 0; i < pathPointIntersetsSize; i++)
	{
		S32 x;
		AICivilianPathPointIntersection *pPathPointIsect = pCivPathInfo->pathPointIntersections[i];

		for (x = 0; x < eaiSize(&pPathPointIsect->eaPathPointIndicies); x++)
		{
			S32 idx = pPathPointIsect->eaPathPointIndicies[x];
			assert(idx >= 0 && idx < pathPointListSize);
			eaPush(&pPathPointIsect->eaPathPoints, pCivPathInfo->pathPoints[idx]);
		}
		
		eaPush(&pRTPathInfo->eaPathPointIntersects, pPathPointIsect);
	}

	for(i = 0; i < pathPointListSize; i++)
	{
		AICivilianPathPoint *pPathPoint = pCivPathInfo->pathPoints[i];

		if (pPathPoint->nextPathPointIndex != -1)
		{
			devassert(pPathPoint->nextPathPointIndex >= 0 && pPathPoint->nextPathPointIndex < pathPointListSize);
			pPathPoint->pNextPathPoint = pCivPathInfo->pathPoints[pPathPoint->nextPathPointIndex];
		}

		if(pPathPoint->intersectionIndex != -1)
		{
			devassert(pPathPoint->intersectionIndex >= 0 && pPathPoint->intersectionIndex < intersectionListSize);
			pPathPoint->pIntersection = pCivPathInfo->intersects[pPathPoint->intersectionIndex];
		}

		if(pPathPoint->pathPointIntersectionIndex != -1)
		{
			devassert(pPathPoint->pathPointIntersectionIndex >= 0 && pPathPoint->pathPointIntersectionIndex < pathPointIntersetsSize);
			pPathPoint->pPathPointIntersection = pCivPathInfo->pathPointIntersections[pPathPoint->pathPointIntersectionIndex];
		}
		
		eaPush(&pRTPathInfo->eaPathPoints, pPathPoint);
	}

	if (eaSize(&pCivPathInfo->eaIntersectionCurves))
	{
		S32 numCurves = eaSize(&pCivPathInfo->eaIntersectionCurves);
		pRTPathInfo->stashIntersectionCurves = stashTableCreateFixedSize(numCurves, sizeof(ACGCurveKey));
		
		FOR_EACH_IN_EARRAY_FORWARDS(pCivPathInfo->eaIntersectionCurves, AICivIntersectionCurve, pcurve)
		{
			ANALYSIS_ASSUME(pCivPathInfo->legs);
			pcurve->legSource = pCivPathInfo->legs[pcurve->legSourceIndex];
			pcurve->legDest = pCivPathInfo->legs[pcurve->legDestIndex];
			pcurve->key = CreateKeyFromCurve(pcurve);

			stashAddPointer(pRTPathInfo->stashIntersectionCurves, &pcurve->key, pcurve, false);

			{
				
				ACGCurveKey testkey = CreateKey(pcurve->legSource, pcurve->legDest, 
													pcurve->sourceLane, pcurve->destLane, 0);
				StashElement	e;
								
				if(stashFindElement(pRTPathInfo->stashIntersectionCurves, &testkey, &e))
				{
					AICivIntersectionCurve *pcurve1 = stashElementGetPointer(e);
					int xxx =0;
				}
			}

		}
		FOR_EACH_END
	}
	
	eaDestroy(&eaTagErrorList);
	

}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_DestroyRuntimePathInfo(AICivilianRuntimePathInfo *pRTPathInfo)
{
	S32 i;
	for(i = 0; i < ARRAY_SIZE(pRTPathInfo->legs); i++)
		eaDestroy(&pRTPathInfo->legs[i]);
	
	eaDestroy(&pRTPathInfo->intersects);
	eaDestroy(&pRTPathInfo->eaPathPoints);
	eaDestroy(&pRTPathInfo->eaPathPointIntersects);
	eaDestroy(&pRTPathInfo->eaIntersectionCurves);

	if (pRTPathInfo->stashIntersectionCurves)
	{
		stashTableDestroy(pRTPathInfo->stashIntersectionCurves);
		pRTPathInfo->stashIntersectionCurves = NULL;
	}
	
	// 
	aiCivilianPathInfo_Free(&pRTPathInfo->pCivilianPathInfoCopy);
}

// ------------------------------------------------------------------------------------------------------------------
// 
static void acgInitPathInfo(const AICivilianRuntimePathInfo *pRTPathInfo, AICivilianPathInfo *pCivPathInfo)
{
	int i;
		
	// push all the legs and intersections from the AICivilianState to the AICivilianPathInfo
	{
		for(i=0; i<ARRAY_SIZE(pRTPathInfo->legs); i++)
		{
			int j;
			for(j=0; j<eaSize(&pRTPathInfo->legs[i]); j++)
			{
				AICivilianPathLeg *leg = pRTPathInfo->legs[i][j];

				devassert(!leg->deleted);

				leg->index = eaSize(&pCivPathInfo->legs);
				eaPush(&pCivPathInfo->legs, leg);
			}
		}

		for(i=0; i<eaSize(&pRTPathInfo->intersects); i++)
		{
			int j;
			AICivilianPathIntersection *intersect = pRTPathInfo->intersects[i];

			intersect->index = i;
			eaPush(&pCivPathInfo->intersects, intersect);

			for(j=0; j<eaSize(&intersect->legIntersects); j++)
			{
				PathLegIntersect *pli = intersect->legIntersects[j];

				pli->legByIndex = pli->leg->index;
				pli->crosswalkByIndex = pli->pCrosswalkLeg ? pli->pCrosswalkLeg->index : -1;
			}
		}

		// Set up the path points by putting them onto the pCivPathInfo->'s list
		// and setting their index
		for(i=0; i < eaSize(&pRTPathInfo->eaPathPointIntersects); i++)
		{
			AICivilianPathPointIntersection *pPathPointIntersect = pRTPathInfo->eaPathPointIntersects[i];
			pPathPointIntersect->myIndex = i;
			eaPush(&pCivPathInfo->pathPointIntersections, pPathPointIntersect);
		}

		for(i=0; i < eaSize(&pRTPathInfo->eaPathPoints); i++)
		{
			AICivilianPathPoint *pPathPoint = pRTPathInfo->eaPathPoints[i];
			pPathPoint->myIndex = i;
			eaPush(&pCivPathInfo->pathPoints, pPathPoint);
		}

		// add the AICivilianPathPoint
		FOR_EACH_IN_EARRAY_FORWARDS(pRTPathInfo->eaIntersectionCurves, AICivIntersectionCurve, pcurve)
		{
			pcurve->legSourceIndex = pcurve->legSource->index;
			pcurve->legDestIndex = pcurve->legDest->index;

			eaPush(&pCivPathInfo->eaIntersectionCurves, pcurve);
		}
		FOR_EACH_END
	}

	// go through all the legs and get the indicies to all the other legs and intersections
	for(i=0; i<eaSize(&pCivPathInfo->legs); i++)
	{
		int j;
		AICivilianPathLeg *leg = pCivPathInfo->legs[i];

		devassert(!leg->next || !leg->next->deleted);
		devassert(!leg->prev || !leg->prev->deleted);
		leg->nextByIndex = leg->next ? leg->next->index : -1;
		leg->prevByIndex = leg->prev ? leg->prev->index : -1;
		leg->nextIntByIndex = leg->nextInt ? leg->nextInt->index : -1;
		leg->prevIntByIndex = leg->prevInt ? leg->prevInt->index : -1;
		
		for(j=0; j<eaSize(&leg->midInts); j++)
		{
			devassert(leg->midInts[j]->bIsMidIntersection);
			eaiPush(&leg->midIntsByIndex, leg->midInts[j]->index);
		}

		leg->crosswalkNearestRoadByIndex = leg->pCrosswalkNearestRoad ? leg->pCrosswalkNearestRoad->index : -1;

		for (j = 0; j < eaSize(&leg->eaCrosswalkLegs); j++)
		{
			eaiPush(&leg->eaCrosswalkLegsByIndex, leg->eaCrosswalkLegs[j]->index);
		}
	}

	// now set up all the indicies for the links to other pathPoints and their associated intersections, etc
	for(i=0; i < eaSize(&pCivPathInfo->pathPoints); i++)
	{
		AICivilianPathPoint *pPathPoint = pCivPathInfo->pathPoints[i];
		pPathPoint->nextPathPointIndex = pPathPoint->pNextPathPoint ? pPathPoint->pNextPathPoint->myIndex : -1;
		pPathPoint->intersectionIndex = pPathPoint->pIntersection ? pPathPoint->pIntersection->index : -1;
		pPathPoint->pathPointIntersectionIndex = pPathPoint->pPathPointIntersection ? pPathPoint->pPathPointIntersection->myIndex : -1;
	}

	for(i=0; i < eaSize(&pCivPathInfo->pathPointIntersections); i++)
	{
		S32 x;
		AICivilianPathPointIntersection *pPathPointIntersect = pCivPathInfo->pathPointIntersections[i];
		
		for(x = 0; x < eaSize(&pPathPointIntersect->eaPathPoints); x++)
		{
			AICivilianPathPoint *pPathPoint = pPathPointIntersect->eaPathPoints[x];
			eaiPush(&pPathPointIntersect->eaPathPointIndicies, pPathPoint->myIndex);
		}
	}

	// todo: does this need to go here
	//eaPushEArray(&pCivPathInfo->deletedLegs, &pCivState->deletedLegs);
	//eaPushEArray(&pCivPathInfo->deletedNodes, &pCivState->deletedNodes);
	//eaPushEArray(&pCivPathInfo->deletedLines, &pCivState->deletedLines);
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathInfo* aiCivilian_LoadPathInfo(const char *pszFilename)
{
	// First load the civilian pathing information.
	if(fileExists(pszFilename))
	{
		AICivilianPathInfo* pPathInfo; 

		acgInitData();

		loadstart_printf("Loading civilians...");

		pPathInfo = StructCreate(parse_AICivilianPathInfo);

		if(!ParserOpenReadBinaryFile(NULL, pszFilename, parse_AICivilianPathInfo, pPathInfo, NULL, NULL, NULL, NULL, 0, 0, 0))
		{
			pszFilename = aiCivilianGetSuffixedFileName(1, 0, CIV_FILE_SUFFIX);
			if(ParserReadTextFile(pszFilename, parse_AICivilianPathInfo, pPathInfo, 0))
			{
				pszFilename = aiCivilianGetSuffixedFileName(1, 1, CIV_FILE_SUFFIX);
				ParserWriteBinaryFile(pszFilename, NULL, parse_AICivilianPathInfo, pPathInfo, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
			}
		}

		loadend_printf("done (%d legs)", eaSize(&pPathInfo->legs));

		return pPathInfo;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_UnloadCivLegsAndMapDef()
{
	aiCivilian_ClearAllPartitionData();

	aiCivilianPathInfo_Free(&g_civSharedState.pCivPathInfo);
	
	aiCivMapDef_Free();

	aiCivEditing_ReportCivMapDefUnload();

	StructDestroySafe(parse_AICivilianPathMapInfo, &g_civSharedState.map_info);
}

// ------------------------------------------------------------------------------------------------------------------
// Makes sure the static data structures are loaded, and then loads teh pathing informatio 
void aiCivilian_LoadCivLegsAndMapDef()
{
	const char *pszFilename = aiCivilianGetSuffixedFileName(1, 1, CIV_FILE_SUFFIX);
	
	if (!aiCivilian_CreateAndInitSharedData())
		return;

	aiCivilian_UnloadCivLegsAndMapDef();
	
	// first load the pathing information
	g_civSharedState.pCivPathInfo = aiCivilian_LoadPathInfo(pszFilename);
	
	// Next load the civilian definition file
	aiCivilian_LoadMapDef();
}


// ------------------------------------------------------------------------------------------------------------------
// 
static void acgClearData(void)
{
	acgProcessShutdown();
	
	aiCivilian_ClearAllPartitionData();

	aiCivilian_UnloadCivLegsAndMapDef();

	aiCivilian_DestroyRuntimePathInfo(&s_acgPathInfo);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgInitData(void)
{
	MP_CREATE(AICivilianPathGenNodeBlock, 10);
	MP_CREATE(AICivilianPathGenNode, 1000);
	MP_CREATE(AICivilianPathGenPair, 100);
	MP_CREATE(AICivilianPathGenLine, 10);
	MP_CREATE(AICivilianPathLeg, 20);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgMapLoad(int iPartitionIdx)
{
	if(g_civSharedState.bIsDisabled)
		return;
	
	if(gbMakeBinsAndExit)
	{
		s_enableCivilianGeneration = 1;
	}

	if(s_enableCivilianGeneration)
	{
		g_civSharedState.pCivPathInfo = NULL;
		acgClearData();
		acgGenerate(iPartitionIdx);
	}
	
	acgClearData();
	
	aiCivilian_LoadCivLegsAndMapDef();

	// load data for all available partitions
	aiCivilian_LoadAllPartitionData();
}

// ------------------------------------------------------------------------------------------------------------------
static void acgMapLoadWrapper(ZoneMap *zmap)
{
	if(gbMakeBinsAndExit)
	{
		acgMapLoad(worldGetAnyCollPartitionIdx());
	}
}

// ------------------------------------------------------------------------------------------------------------------
void acgMapUnload(void)
{
	// Free everything
	acgClearData();
}
void acgMapUnloadWrapper(void)
{
	// Free everything
	if(gbMakeBinsAndExit)
		acgClearData();
}

void acgReloadMap(int iPartitionIdx)
{
	S32 saveCivGen = s_enableCivilianGeneration;
	s_enableCivilianGeneration = false;
	
	acgMapUnload();
	acgMapLoad(iPartitionIdx);

	s_enableCivilianGeneration = saveCivGen;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianStartup(void)
{
	worldLibSetCivGenFunctions(acgGeneratorCreate, acgGeneratorDestroy);
	worldLibSetCivCallbacks(acgMapUnloadWrapper, acgMapLoadWrapper);
	worldLibSetCivilianVolumeFunctions(acgCivilianVolumeCreateFunc, acgCivilianVolumeDestroyFunc);
	worldLibSetPlayableFunctions(acgPlayableCreateFunc, acgPlayableDestroyFunc);

	s_physAsphalt = allocAddString("asphalt");
	s_physConcrete = allocAddString("concrete");
	s_physSidewalk = allocAddString("civsidewalk");
	s_physIntersection = allocAddString("civintersection");
	s_physStreetcar = allocAddString("civstreetcar");
	s_physParking = allocAddString("civparking");
	
	gslHeatMapRegisterType("CivilianDensity", &aiCivHeatmapGatherCivs);
	gslHeatMapRegisterType("LegMap", &acgHeatmapGatherLegs );
}

// ------------------------------------------------------------------------------------------------------------------
int aiCivGenClassifyResults(WorldCollCollideResults *results)
{
	PhysicalProperties* phys_prop;

	if(!results->hitSomething)
		return EAICivilianLegType_NONE;

	phys_prop = wcoGetPhysicalProperties(results->wco, results->tri.index, results->posWorldImpact, NULL);

	if(!phys_prop || !phys_prop->civilianEnabled) 
		return EAICivilianLegType_NONE;

	if(physicalPropertiesGetName(phys_prop)==s_physSidewalk) 
		return EAICivilianLegType_PERSON;
	
	if(physicalPropertiesGetName(phys_prop)==s_physAsphalt) 
		return EAICivilianLegType_CAR;

	if (physicalPropertiesGetName(phys_prop)==s_physParking)
		return EAICivilianLegType_PARKING;

	if (physicalPropertiesGetName(phys_prop)==s_physStreetcar)
		return EAICivilianLegType_TROLLEY;

	if (physicalPropertiesGetName(phys_prop)==s_physIntersection)
		return EAICivilianLegType_INTERSECTION;

	return EAICivilianLegType_NONE;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgIsTypeIntersection(WorldCollCollideResults *results)
{
	PhysicalProperties* phys_prop;

	if(!results->hitSomething)
		return false;

	phys_prop = wcoGetPhysicalProperties(results->wco, results->tri.index, results->posWorldImpact, NULL);

	if(!phys_prop || !phys_prop->civilianEnabled) 
		return false;

	return (physicalPropertiesGetName(phys_prop) == s_physIntersection);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int acgType_DoesCheckGroundAngle(EAICivilianLegType type)
{
	return type != EAICivilianLegType_TROLLEY;
}

// for the phase: CGS_MIN
__forceinline static int acgType_DoesCheckMinDistance(EAICivilianLegType type)
{
	return type != EAICivilianLegType_PARKING;
}

__forceinline static int acgType_DoesTypeCreateLegs(EAICivilianLegType type)
{
	return !(type == EAICivilianLegType_INTERSECTION || type == EAICivilianLegType_TROLLEY || type == EAICivilianLegType_PARKING);
}


static bool acgType_CanTypeIntersectOverType(EAICivilianLegType type, EAICivilianLegType isectType)
{
	static bool s_Table[EAICivilianLegType_COUNT + 1][EAICivilianLegType_COUNT + 1] =
	{	// _NONE,		_PERSON,		_CAR,		_TROLLEY,		_PARKING,			_INTERSECTION
		{false,			false,			false,		false,			false,				false }, // _NONE
		{1,				1,				false,		false,			false,				false }, // _PERSON
		{1,				false,			1,			1,				1,					1	  }, // _CAR
		{false,			false,			false,		1,				false,				false }, // _STREETCAR
		{false,			false,			1,			false,			1,					false }, // _PARKING
		{false,			false,			false,		false,			false,				false }  // _INTERSECTION
	};

	assert(type >= EAICivilianLegType_NONE && type < EAICivilianLegType_COUNT);
	assert(isectType >= EAICivilianLegType_NONE && isectType < EAICivilianLegType_COUNT);

	// offset into the table, as EAICivilianLegType_NONE is actually the 0th index
	type ++;
	isectType++;

	return s_Table[type][isectType];
}

// todo: read in the desired lane widths
//	need a civGen config?
__forceinline static F32 acgType_GetMaxLegWidth(EAICivilianLegType type)
{
	switch (type)
	{
		case EAICivilianLegType_NONE:
			return 0.f;
		case EAICivilianLegType_PERSON:
			return 60.f;
		case EAICivilianLegType_CAR:
			return 20.f * 4.5f; 
		case EAICivilianLegType_PARKING:
			return 30.f;
		case EAICivilianLegType_TROLLEY:
			return 50.f;
		case EAICivilianLegType_INTERSECTION:
			return 0.f;
	}

	return 0.f;
}

static F32 acgType_GetMergeAngleThreshold(EAICivilianLegType type)
{
#define DEFAULT_MERGE_THRESHOLD	RAD(5.f)
	switch (type)
	{
		case EAICivilianLegType_NONE:
			return DEFAULT_MERGE_THRESHOLD;
		case EAICivilianLegType_PERSON:
			return DEFAULT_MERGE_THRESHOLD;
		case EAICivilianLegType_CAR:
			return DEFAULT_MERGE_THRESHOLD; 
		case EAICivilianLegType_PARKING:
			return DEFAULT_MERGE_THRESHOLD;
		case EAICivilianLegType_TROLLEY:
			return DEFAULT_MERGE_THRESHOLD;
		case EAICivilianLegType_INTERSECTION:
			return DEFAULT_MERGE_THRESHOLD;
	}

	return 0.f;
}

static F32 acgType_GetMaxErrorThreshold(EAICivilianLegType type)
{
#define DEFAULT_MAX_ERROR_THRESHOLD	RAD(10.f)
	switch (type)
	{
		case EAICivilianLegType_NONE:
			return DEFAULT_MAX_ERROR_THRESHOLD;
		case EAICivilianLegType_PERSON:
			return DEFAULT_MAX_ERROR_THRESHOLD * 0.5f;
		case EAICivilianLegType_CAR:
			return DEFAULT_MAX_ERROR_THRESHOLD * 0.7f; 
		case EAICivilianLegType_PARKING:
			return DEFAULT_MAX_ERROR_THRESHOLD * 0.5f;
		case EAICivilianLegType_TROLLEY:
			return DEFAULT_MAX_ERROR_THRESHOLD;
		case EAICivilianLegType_INTERSECTION:
			return DEFAULT_MAX_ERROR_THRESHOLD;
	}

	return 0.f;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPlayableCreateFunc(WorldVolumeEntry *ent)
{
	eaPush(&g_civSharedState.eaPlayableVolumes, ent);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPlayableDestroyFunc(WorldVolumeEntry *ent)
{
	eaFindAndRemoveFast(&g_civSharedState.eaPlayableVolumes, ent);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgCivilianProcessVolumeCritterOverride(WorldVolumeEntry *entry)
{
	WorldCivilianVolumeProperties *civVolume = entry->server_volume.civilian_volume_properties;
	if (civVolume->critter_spawns)
	{
		S32 i;

		civVolume->pedestrian_total_weight = 0.f;
		civVolume->car_total_weight = 0.f;

		for (i = 0; i < eaSize(&civVolume->critter_spawns); i++)
		{
			CivilianCritterSpawn *pSpawn = civVolume->critter_spawns[i];
			if (!pSpawn->is_car)
			{
				civVolume->pedestrian_total_weight += pSpawn->spawn_weight;

			}
			else
			{
				civVolume->car_total_weight += pSpawn->spawn_weight;
			}
		}
	}

	
}


// ------------------------------------------------------------------------------------------------------------------
static void acgCivilianVolumeCreateFunc(WorldVolumeEntry *entry)
{
	devassert(entry->server_volume.civilian_volume_properties);

	// filter the volume into particular lists for faster searching 
	if (entry->server_volume.civilian_volume_properties->forced_sidewalk || 
		entry->server_volume.civilian_volume_properties->forced_road || 
		entry->server_volume.civilian_volume_properties->forced_crosswalk)
	{
		
		eaPush(&g_civSharedState.eaCivVolumeForcedLegs, entry);
	}
	else
	{
		if (entry->server_volume.civilian_volume_properties->pedestrian_wander_area)
		{
			aiCivWanderArea_AddVolumeToEachPartition(entry);
			eaPush(&g_civSharedState.eaPedestrianWanderVolumes, entry);
		}

		{
			acgCivilianProcessVolumeCritterOverride(entry);
			eaPush(&g_civSharedState.eaCivilianVolumes, entry);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgCivilianVolumeDestroyFunc(WorldVolumeEntry *entry)
{
	S32 ret = false;
		
	if (entry->server_volume.civilian_volume_properties->forced_sidewalk || 
		entry->server_volume.civilian_volume_properties->forced_road || 
		entry->server_volume.civilian_volume_properties->forced_crosswalk)
	{
		eaFindAndRemoveFast(&g_civSharedState.eaCivVolumeForcedLegs, entry);
	}
	else
	{
		if (entry->server_volume.civilian_volume_properties->pedestrian_wander_area)
		{
			aiCivWanderArea_RemoveVolumeFromEachPartition(entry);
			eaFindAndRemoveFast(&g_civSharedState.eaPedestrianWanderVolumes, entry);
		}

		{
			eaFindAndRemoveFast(&g_civSharedState.eaCivilianVolumes, entry);
		}
	}
	
}


// ------------------------------------------------------------------------------------------------------------------
bool aiCivGetPlayableBounds(Vec3 min, Vec3 max)
{
	S32 i;
		
	if (!eaSize(&g_civSharedState.eaPlayableVolumes))
	{
		zeroVec3(min);
		zeroVec3(max);
		return false;
	}

	setVec3same(min, FLT_MAX);
	setVec3same(max, -FLT_MAX);

	for (i = 0; i < eaSize(&g_civSharedState.eaPlayableVolumes); i++)
	{
		Vec3 	xposMin, xposMax;
		WorldVolumeEntry *ent = g_civSharedState.eaPlayableVolumes[i];

		mulVecMat4(ent->base_entry.shared_bounds->local_min, ent->base_entry.bounds.world_matrix, xposMin);
		mulVecMat4(ent->base_entry.shared_bounds->local_max, ent->base_entry.bounds.world_matrix, xposMax);

		MINVEC3(xposMin, min, min);
		MAXVEC3(xposMax, max, max);
	}

	return true;
}


// ------------------------------------------------------------------------------------------------------------------
// ACGNodeBlocks
// ------------------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------------
//typedef static void (*WorldCollObjectTraverseCB)(void* userPointer, const WorldCollObjectTraverseParams* params);
static void acgMeasureWorld(void *userPointer, const WorldCollObjectTraverseParams *params)
{
	WorldCollStoredModelData*	smd = NULL;
	WorldCollModelInstanceData* inst = NULL;

	if(wcoGetStoredModelData(&smd, &inst, params->wco, WC_QUERY_BITS_WORLD_ALL))
	{
		Vec3 world_min, world_max;
		assert(inst);
		mulBoundsAA(smd->min, smd->max, inst->world_mat, world_min,	world_max);
		MINVEC3(s_acgNodeBlocks.world_min, world_min, s_acgNodeBlocks.world_min);
		MAXVEC3(s_acgNodeBlocks.world_max, world_max, s_acgNodeBlocks.world_max);

		s_acgNodeBlocks.valid = true;
	}

	SAFE_FREE(inst);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgGetNodeBlocksMinMax(int iPartitionIdx)
{
	setVec3same(s_acgNodeBlocks.world_min, MAX_PLAYABLE_DIST_ORIGIN_SQR);
	setVec3same(s_acgNodeBlocks.world_max, -MAX_PLAYABLE_DIST_ORIGIN_SQR);
	s_acgNodeBlocks.valid = false;
	wcTraverseObjects(worldGetActiveColl(iPartitionIdx), acgMeasureWorld, NULL, NULL, NULL, /*unique=*/0, WCO_TRAVERSE_STATIC);

	if(!s_acgNodeBlocks.valid)
		return;

	// One block buffer
	s_acgNodeBlocks.world_min[0] -= NODE_BLOCK_SIZE;
	s_acgNodeBlocks.world_min[2] -= NODE_BLOCK_SIZE;

	s_acgNodeBlocks.world_max[0] += NODE_BLOCK_SIZE;
	s_acgNodeBlocks.world_max[2] += NODE_BLOCK_SIZE;

	s_acgNodeBlocks.grid_min[0] = (int)(floor(s_acgNodeBlocks.world_min[0]/NODE_BLOCK_SIZE))-1;
	s_acgNodeBlocks.grid_min[1] = (int)(floor(s_acgNodeBlocks.world_min[2]/NODE_BLOCK_SIZE))-1;

	s_acgNodeBlocks.grid_max[0] = (int)(ceil(s_acgNodeBlocks.world_max[0]/NODE_BLOCK_SIZE))+1;
	s_acgNodeBlocks.grid_max[1] = (int)(ceil(s_acgNodeBlocks.world_max[2]/NODE_BLOCK_SIZE))+1;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgNodeBlocksInitialize(int iPartitionIdx)
{
	S32 numNodeBlocks;
	acgNodeBlocksFree();

	acgGetNodeBlocksMinMax(iPartitionIdx);

	if(!s_acgNodeBlocks.valid)
		return;

	numNodeBlocks = (s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1) *
									(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]+1);

	s_acgNodeBlocks.aiCivGenNodeBlocks = calloc(numNodeBlocks, sizeof(AICivilianPathGenNodeBlock*));

}

// ------------------------------------------------------------------------------------------------------------------
static void acgGeneratorDestroyNodeBlock(AICivilianPathGenNodeBlock *block)
{
	int i;

	for(i=0; i<NODE_BLOCK_SIZE; i++)
	{
		int j;
		for(j=0; j<NODE_BLOCK_SIZE; j++)
		{
			AICivilianPathGenNode *node = block->nodes[i][j];

			if(node)
			{
				acgNodeFree(node);
			}
		}
	}

	eaDestroy(&block->genNodeList);
	acgBlockFree(block);
}


// ------------------------------------------------------------------------------------------------------------------
static void acgNodeBlocksFree()
{
	if (s_acgNodeBlocks.aiCivGenNodeBlocks)
	{
		S32 x;
		S32 numNodeBlocks;

		numNodeBlocks = (s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1) *
			(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]+1);

		for (x = 0; x < numNodeBlocks; x++)
		{
			AICivilianPathGenNodeBlock *block = s_acgNodeBlocks.aiCivGenNodeBlocks[x];
			if (block)
			{
				acgGeneratorDestroyNodeBlock(block);
			}
		}

		free(s_acgNodeBlocks.aiCivGenNodeBlocks);

	}

	ZeroStruct(&s_acgNodeBlocks);

}



// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenNodeBlock* acgGeneratorGetNodeBlockByGrid(int grid_x, int grid_z, int create)
{
	AICivilianPathGenNodeBlock **blockPtr;
	blockPtr = &s_acgNodeBlocks.aiCivGenNodeBlocks[grid_z+grid_x*(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1])];

	if(grid_x<0 || grid_x>=(s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]))
		return NULL;

	if(grid_z<0 || grid_z>=(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]))
		return NULL;

	if(!*blockPtr && create)
	{
		*blockPtr = acgBlockAlloc();

		(*blockPtr)->grid_x = grid_x;
		(*blockPtr)->grid_z = grid_z;
	}

	return *blockPtr;
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenNode* acgGeneratorGetGridNode(AICivilianPathGenNodeBlock* block, int block_x, int block_z, int create)
{
	AICivilianPathGenNode *node = block->nodes[block_x][block_z];

	if(block_x<0 || block_z>=NODE_BLOCK_SIZE)
		return NULL;

	if(block_z<0 || block_z>=NODE_BLOCK_SIZE)
		return NULL;

	if(!node && create)
	{
		node = block->nodes[block_x][block_z] = acgNodeAlloc();
		node->block_x = block_x;
		node->block_z = block_z;
		node->grid_x = block->grid_x;
		node->grid_z = block->grid_z;
	}

	return node;
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenNode* acgGeneratorGetNode(int grid_x, int grid_z, int block_x, int block_z, int create)
{
	AICivilianPathGenNodeBlock *block = acgGeneratorGetNodeBlockByGrid(grid_x, grid_z, create);

	if(block)
		return acgGeneratorGetGridNode(block, block_x, block_z, create);

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathGenNode* acgGeneratorGetNodeByWorld(F32 x, F32 z, int create)
{
	int grid_x, grid_z;
	AICivilianPathGenNodeBlock *block;

	grid_x = x/NODE_BLOCK_SIZE - s_acgNodeBlocks.grid_min[0];
	grid_z = z/NODE_BLOCK_SIZE - s_acgNodeBlocks.grid_min[1];

	if (grid_x < 0 && grid_x >= (2<<8))
	{
		return NULL;
	}
	if (grid_z < 0 && grid_z >= (2<<8))
	{
		return NULL;
	}

	block = acgGeneratorGetNodeBlockByGrid(grid_x, grid_z, create);

	if(block)
	{
		int block_x, block_z;

		block_x = ((int)x) & (NODE_BLOCK_SIZE-1);
		block_z = ((int)z) & (NODE_BLOCK_SIZE-1);

		return acgGeneratorGetGridNode(block, block_x, block_z, create);
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgGeneratorValidateLocation(int *grid_x, int *grid_z, int *block_x, int *block_z)
{
	while(*block_x>=NODE_BLOCK_SIZE)
	{
		*block_x -= NODE_BLOCK_SIZE; *grid_x += 1;
	}
	while(*block_x<=-1)
	{
		*block_x += NODE_BLOCK_SIZE; *grid_x -= 1;
	}
	while(*block_z>=NODE_BLOCK_SIZE)
	{
		*block_z -= NODE_BLOCK_SIZE; *grid_z += 1;
	}
	while(*block_z<=-1)
	{
		*block_z += NODE_BLOCK_SIZE; *grid_z -= 1;
	}
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenNode* acgGeneratorGetNeighbor(AICivilianPathGenNode *node, int offx, int offz, int create)
{
	int grid_x, grid_z;
	int block_x, block_z;
	AICivilianPathGenNodeBlock *block;

	grid_x = node->grid_x;
	grid_z = node->grid_z;

	block_x = node->block_x+offx;
	block_z = node->block_z+offz;

	acgGeneratorValidateLocation(&grid_x, &grid_z, &block_x, &block_z);

	block = acgGeneratorGetNodeBlockByGrid(grid_x, grid_z, 1);
	devassert(block);

	return acgGeneratorGetGridNode(block, block_x, block_z, create);
}

// ------------------------------------------------------------------------------------------------------------------
typedef struct GenNodeIndicies
{
	int grid_x;
	int grid_z;
	int block_x;
	int block_z;
} GenNodeIndicies;

static bool acgGetGridBlockIndiciesFromWorldPos(F32 x, F32 z, GenNodeIndicies *pIndicies)
{
	pIndicies->grid_x = x/NODE_BLOCK_SIZE - s_acgNodeBlocks.grid_min[0];
	pIndicies->grid_z = z/NODE_BLOCK_SIZE - s_acgNodeBlocks.grid_min[1];

	if(pIndicies->grid_x < 0 || pIndicies->grid_x >= (s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]))
		return false;

	if(pIndicies->grid_z < 0 || pIndicies->grid_z >= (s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]))
		return false;

	pIndicies->block_x = ((int)x) & (NODE_BLOCK_SIZE-1);
	pIndicies->block_z = ((int)z) & (NODE_BLOCK_SIZE-1);
	return true;
}

static AICivilianPathGenNode* acgIndiciesGetNeighbor(const GenNodeIndicies *pIndicies, int offx, int offz, GenNodeIndicies *pNeighborIndicies)
{
	AICivilianPathGenNodeBlock *block;

	pNeighborIndicies->grid_x = pIndicies->grid_x;
	pNeighborIndicies->grid_z = pIndicies->grid_z;
	pNeighborIndicies->block_x = pIndicies->block_x + offx;
	pNeighborIndicies->block_z = pIndicies->block_z + offz;

	acgGeneratorValidateLocation(&pNeighborIndicies->grid_x, &pNeighborIndicies->grid_z, 
										&pNeighborIndicies->block_x, &pNeighborIndicies->block_z);

	block = acgGeneratorGetNodeBlockByGrid(pNeighborIndicies->grid_x, pNeighborIndicies->grid_z, 1);
	devassert(block);

	return acgGeneratorGetGridNode(block, pNeighborIndicies->block_x, pNeighborIndicies->block_z, false);
}

static AICivilianPathGenNode** acgIndiciesGetNodeReference(const GenNodeIndicies *pIndicies)
{
	AICivilianPathGenNodeBlock *block;

	block = acgGeneratorGetNodeBlockByGrid(pIndicies->grid_x, pIndicies->grid_z, 1);
	devassert(block);

	return &block->nodes[pIndicies->block_x][pIndicies->block_z];
}



// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenNode** acgGeneratorGetNodeReference(AICivilianPathGenNode *node, int offx, int offz)
{
	int grid_x, grid_z;
	int block_x, block_z;
	AICivilianPathGenNodeBlock *block;

	grid_x = node->grid_x;
	grid_z = node->grid_z;

	block_x = node->block_x+offx;
	block_z = node->block_z+offz;
	while(block_x>=NODE_BLOCK_SIZE)
	{
		block_x -= NODE_BLOCK_SIZE; grid_x += 1;
	}
	while(block_x<=-1)
	{
		block_x += NODE_BLOCK_SIZE; grid_x -= 1;
	}
	while(block_z>=NODE_BLOCK_SIZE)
	{
		block_z -= NODE_BLOCK_SIZE; grid_z += 1;
	}
	while(block_z<=-1)
	{
		block_z += NODE_BLOCK_SIZE; grid_z -= 1;
	}

	block = acgGeneratorGetNodeBlockByGrid(grid_x, grid_z, 1);
	devassert(block);

	return &block->nodes[block_x][block_z];
}

// ------------------------------------------------------------------------------------------------------------------
static int GenNodeIndiciesCompare(const GenNodeIndicies *pArrayObj, const GenNodeIndicies *pUser)
{
	return (pArrayObj->block_x == pUser->block_x) && (pArrayObj->block_z == pUser->block_z) &&
		(pArrayObj->grid_x == pUser->grid_x) && (pArrayObj->grid_z == pUser->grid_z);
}


// ------------------------------------------------------------------------------------------------------------------
#pragma optimize("gt", on)
static void acgGetPathGenNodePosition(const AICivilianPathGenNode *node, Vec3 world_pos)
{
	world_pos[0] = ((node->grid_x + s_acgNodeBlocks.grid_min[0]) << NODE_BLOCK_BITS) + node->block_x;
	world_pos[1] = node->y_coord;
	world_pos[2] = ((node->grid_z + s_acgNodeBlocks.grid_min[1]) << NODE_BLOCK_BITS) + node->block_z;
}

static void acgGetGenNodeIndiciesPosition(const GenNodeIndicies *indicies, F32 yCoord, Vec3 world_pos)
{
	world_pos[0] = ((indicies->grid_x + s_acgNodeBlocks.grid_min[0]) << NODE_BLOCK_BITS) + indicies->block_x;
	world_pos[1] = yCoord;
	world_pos[2] = ((indicies->grid_z + s_acgNodeBlocks.grid_min[1]) << NODE_BLOCK_BITS) + indicies->block_z;
}
#pragma optimize("", on)

// ------------------------------------------------------------------------------------------------------------------
static void acgCreateStartingNode(const Vec3 pos)
{

	AICivilianPathGenNode *node = acgGeneratorGetNodeByWorld(pos[0], pos[2], 0);

	if(!node)
	{
		AICivilianPathGenNodeBlock *block = NULL;
		Vec3 node_pos;
		node = acgGeneratorGetNodeByWorld(pos[0], pos[2], 1);
		if(!node)
			return;

		block = acgGeneratorGetNodeBlockByGrid(node->grid_x, node->grid_z, 0);
		devassert(block);

		acgGetPathGenNodePosition(node, node_pos);
		devassert(distance3XZ(pos, node_pos)<2);
		node->y_coord = pos[1];
		eaPush(&block->genNodeList, node);
		eaPushUnique(&s_acgProcess.genBlockList, block);
		eaPush(&s_acgProcess.genStartList, node);
	}
}



// ------------------------------------------------------------------------------------------------------------------
static int acgProcessCheckStart(int iPartitionIdx, Entity *debugger)
{
	int i;
	WorldRegion **regions;
	static WorldCivilianGenerator **wlcivgens = NULL;
	static WorldCivilianGenerator **diff = NULL;

	if(s_enableCivilianGeneration && !s_acgNodeBlocks.aiCivGenNodeBlocks)
	{
		acgNodeBlocksInitialize(iPartitionIdx);
	}

	if(!s_acgNodeBlocks.valid)
		return 0;

	if (s_enableCivilianGeneration && acg_d_nodeGenerator == true)
	{
		// if we're debugging the node generation, only create this node and then exit
		acgCreateStartingNode(s_acgNodeGeneratorPos);
		return 1;
	}


	regions = worldGetAllWorldRegions();

	eaSetSize(&wlcivgens, 0);
	for(i=0; i<eaSize(&regions); i++)
	{
		WorldRegion *region = regions[i];
		WorldCivilianGenerator **region_civgens = worldRegionGetCivilianGenerators(region);

		eaPushEArray(&wlcivgens, &region_civgens);
	}

	eaSetSize(&diff, 0);
	eaDiffAddr(&wlcivgens, &s_acgProcess.lastWorldList, &diff);

	for(i=0; i<eaSize(&diff); i++)
	{
		// New generators
		CivilianGenerator *civgen = worldGetCivilianGenerator(diff[i]);

		if(!civgen)
		{
			civgen = StructCreate(parse_CivilianGenerator);
			copyVec3(diff[i]->position, civgen->world_pos);
			worldSetCivilianGenerator(diff[i], civgen);
		}

		if(s_enableCivilianGeneration)
		{
			acgCreateStartingNode(civgen->world_pos);
		}
	}

	eaCopy(&s_acgProcess.lastWorldList, &wlcivgens);

	for(i=0; i<eaSize(&destroyedGens); i++)
	{
		// Deleted generators
		StructDestroySafe(parse_CivilianGenerator, &destroyedGens[i]);
	}
	eaClear(&destroyedGens);

	if(eaSize(&s_acgProcess.genBlockList))
		return 1;
	
	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static F32 acgMath_GetAngleBetweenNorms(const Vec3 v1, const Vec3 v2)
{
	F32 fAngle = dotVec3(v1, v2);
	return acosf(CLAMP(fAngle, -1.f, 1.f));
}

F32 acgMath_GetAngleBetweenNormsAbs(const Vec3 v1, const Vec3 v2)
{
	F32 fAngle = dotVec3(v1, v2);
	fAngle = ABS(fAngle);
	return acosf(MIN(fAngle, 1.f));
}

static F32 acgMath_GetAngleBetween(const Vec3 v1, const Vec3 v2)
{
	F32 fLengths;
	F32 fDot;
	F32 fAngle;

	fLengths = lengthVec3(v1) * lengthVec3(v2);
	if (fLengths == 0.f)
		return RAD(90.f);

	fDot = dotVec3(v1, v2);
	fAngle = fDot / fLengths;
	return acosf(CLAMP(fAngle, -1.f, 1.f));
}

static F32 acgMath_GetAngleDiff(F32 fAngle1, F32 fAngle2)
{
	F32 diff = fixAngle(fAngle1 - fAngle2);
	return diff;
}

static F32 acgMath_GetVec3Yaw(const Vec3 v)
{
	return fixAngle( getVec3Yaw(v) );
}

static void acgMath_YawToVec3(Vec3 v1, F32 yaw)
{
	setVec3(v1, sinf(yaw), 0, cosf(yaw));
}


typedef enum EWinding
{
	EWinding_CLOCKWISE,
	EWinding_COUNTERCLOCKWISE,
} EWinding;

static EWinding acgMath_GetWinding(const Vec3 v1, const Vec3 v2)
{
	F32 fcrossY;

	F32 fXZDot = dotVec3XZ(v1, v2); 

	fcrossY = (v1)[2]*(v2)[0] - (v1)[0]*(v2)[2];
	if (fXZDot < 0.f)
		fcrossY = -fcrossY;

	return (fcrossY < 0.f) ? EWinding_COUNTERCLOCKWISE : EWinding_CLOCKWISE;
}



// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool acgMath_LineLine2dIntersection(const Vec3 l1_pt, const Vec3 l1_dir, const Vec3 l2_pt, const Vec3 l2_dir, Vec3 vIsectPos)
{
	F32 t = l2_dir[2] * l1_dir[0] - l2_dir[0] * l1_dir[2];
	if (t != 0)
	{
		t = (l2_dir[0] * (l1_pt[2] - l2_pt[2]) - l2_dir[2] * (l1_pt[0] - l2_pt[0])) / t;

		vIsectPos[0] = l1_pt[0] + l1_dir[0] * t;
		vIsectPos[1] = l1_pt[1];
		vIsectPos[2] = l1_pt[2] + l1_dir[2] * t;
		return true;
	}

	// lines are parallel
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgMath_IsPointInLegColumn(const Vec3 vPos, const AICivilianPathLeg *leg)
{
	F32 perpDist;
	Vec3 vStartToPos;

	subVec3(vPos, leg->start, vStartToPos);
	perpDist = fabs(dotVec3(vStartToPos, leg->perp));
	perpDist = perpDist - (leg->width * 0.5f);
	return perpDist <= 0.001f;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline bool acgPointInWorldVolumeEntry(const Vec3 vPos, const WorldVolumeEntry *ent)
{
	WorldVolumeElement	*volumeElement = NULL;
	Vec3 vLocalPos, vTmpPos;

	subVec3(vPos, ent->base_entry.bounds.world_matrix[3], vTmpPos);
	mulVecMat3Transpose(vTmpPos, ent->base_entry.bounds.world_matrix, vLocalPos);
	volumeElement = eaGet(&ent->elements, 0);
	devassert(volumeElement);

	return (pointBoxCollision(vLocalPos, volumeElement->local_min, volumeElement->local_max));
}

#define DISABLE_SIDEWALK	0x01
#define DISABLE_ROAD		0x02
#define DISABLE_BOTH		0x03
// returns flags set to what is disabled for the given position
__forceinline static S32 acgCheckForCivilianVolumeLegDisable(const Vec3 pos, bool bUseIntersections)
{
	S32 i;
	S32 flags = 0;

	
	for (i = 0; i < eaSize(&g_civSharedState.eaCivilianVolumes); i++)
	{
		WorldVolumeEntry *ent = g_civSharedState.eaCivilianVolumes[i];
		devassert(ent->server_volume.civilian_volume_properties);

		if (ent->server_volume.civilian_volume_properties->disable_roads || 
			ent->server_volume.civilian_volume_properties->disable_sidewalks ||
			(bUseIntersections && ent->server_volume.civilian_volume_properties->forced_intersection) ) // can this volume disable roads/sidewalks
		{
			if (acgPointInWorldVolumeEntry(pos, ent))
			{
				flags |= ent->server_volume.civilian_volume_properties->disable_sidewalks ? DISABLE_SIDEWALK : 0;
				flags |= ent->server_volume.civilian_volume_properties->disable_roads ? DISABLE_ROAD : 0;

				if (bUseIntersections) // intersections disable roads, but then connect roads together
					flags |= ent->server_volume.civilian_volume_properties->forced_intersection ? DISABLE_ROAD : 0;

				if ((flags & DISABLE_BOTH) == DISABLE_BOTH)
				{
					return flags;
				}
			}
		}
	}

	return flags;
}

// ------------------------------------------------------------------------------------------------------------------
static WorldVolumeEntry* acgCheckForCivilianIntersectionVolume(const Vec3 vPos)
{
	S32 i;

	for (i = 0; i < eaSize(&g_civSharedState.eaCivilianVolumes); i++)
	{
		WorldVolumeEntry *ent = g_civSharedState.eaCivilianVolumes[i];
		devassert(ent->server_volume.civilian_volume_properties);

		if (ent->server_volume.civilian_volume_properties->forced_intersection)
		{
			if (acgPointInWorldVolumeEntry(vPos, ent))
			{
				return ent;
			}
		}
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
#define CGS_GRID_BLOCK_CAST_DISTUP		5.0f
#define CGS_GRID_BLOCK_CAST_DISTDOWN	10.0f
static void acgProcessBlockNodes(int iPartitionIdx, Entity *debugger, AICivilianPathGenNodeBlock *block, int accrue, AICivilianPathGenNodeBlock ***blocksOut)
{
	int i;
	static AICivilianPathGenNode **accrueList = NULL;
	eaClear(&accrueList);

	while(eaSize(&block->genNodeList))
	{
		Vec3 world_pos;
		Vec3 world_pos_up, world_pos_down;
		WorldCollCollideResults results = {0};
		AICivilianPathGenNode *node = eaPop(&block->genNodeList);
		int reachable = 0;
		int neighbors = 0;

		acgGetPathGenNodePosition(node, world_pos);
		ROUNDVEC3(world_pos, world_pos);

		copyVec3(world_pos, world_pos_up);
		copyVec3(world_pos, world_pos_down);
		world_pos_up[1] += CGS_GRID_BLOCK_CAST_DISTUP;
		world_pos_down[1] -= CGS_GRID_BLOCK_CAST_DISTDOWN;

		wcRayCollide(worldGetActiveColl(iPartitionIdx), world_pos_up, world_pos_down, WC_QUERY_BITS_AI_CIV, &results);
		if(results.errorFlags.noScene)
		{
			if (psdkSceneLimitReached())
				continue;
			eaPush(&block->genNodeList, node);  // Just wait a tick!
			wcForceSimulationUpdate();
			continue;
		}

		if(results.errorFlags.noCell)
			continue;

		node->type = aiCivGenClassifyResults(&results);
		node->y_coord = results.posWorldImpact[1]+2;

		if (s_acg_d_culling.do_culling)
		{	// DEBUGGING
			// throw out any nodes that are not in our bounding box
			if (! pointBoxCollision(world_pos, s_acg_d_culling.boxMin, s_acg_d_culling.boxMax))
				node->type = EAICivilianLegType_NONE;
		}

		if (node->type != -1 && acgType_DoesCheckGroundAngle(node->type) )
		{
			F32 fDot = dotVec3(results.normalWorld, upvec);
			if (fDot <= 0.64) // cos 50 = .64 (50 degrees off of up)
			{
				node->type = EAICivilianLegType_NONE;
			}
		}

		// if we have a valid type, check if we need to disable the node,
		// due to a civilian volume disabling legs
		if (node->type != -1)
		{
			S32 disableFlag = acgCheckForCivilianVolumeLegDisable(world_pos, true);

			if (disableFlag)
			{
				if ( ((node->type == EAICivilianLegType_PERSON) && (disableFlag & DISABLE_SIDEWALK)) ||
					 ((node->type == EAICivilianLegType_CAR) && (disableFlag & DISABLE_ROAD)) )
				{
					node->type = EAICivilianLegType_NONE;
				}
			}
		}

		if (node->type != EAICivilianLegType_NONE)
		{
			// check the neighbors and if the node is reachable
			for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
			{
				AICivilianPathGenNode *neighbor;
				Vec3 n_pos_up;
				int offx = offsetsCorners[i][0], offz = offsetsCorners[i][1];

				neighbor = acgGeneratorGetNeighbor(node, offx, offz, 0);

				if(neighbor && neighbor->type != EAICivilianLegType_NONE)
				{
					int hitF, hitR;
					neighbors++;
					acgGetPathGenNodePosition(neighbor, n_pos_up);
					n_pos_up[1] += 5;

					hitR = wcRayCollide(worldGetActiveColl(iPartitionIdx), n_pos_up, world_pos_up, WC_QUERY_BITS_AI_CIV, NULL);
					hitF = hitR || wcRayCollide(worldGetActiveColl(iPartitionIdx), world_pos_up, n_pos_up, WC_QUERY_BITS_AI_CIV, NULL);

					if(hitF || hitR)
						continue;

					reachable = 1;
					break;
				}
			}

			if(!reachable && neighbors)
				node->type = EAICivilianLegType_NONE;
		}

		if(node->type == EAICivilianLegType_NONE)
			continue;

		world_pos[1] = results.posWorldImpact[1]+2;

		for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
		{
			AICivilianPathGenNode *new_node = NULL;
			int offx = offsetsCorners[i][0], offz = offsetsCorners[i][1];

			new_node = acgGeneratorGetNeighbor(node, offx, offz, 0);
			if(!new_node)
			{
				AICivilianPathGenNodeBlock *otherBlock = NULL;
				new_node = acgGeneratorGetNeighbor(node, offx, offz, 1);
				otherBlock = acgGeneratorGetNodeBlockByGrid(new_node->grid_x, new_node->grid_z, 0);
				new_node->y_coord = results.posWorldImpact[1]+2;
				devassert(otherBlock);
				new_node->type = EAICivilianLegType_NONE;

				if(accrue)  // So we can just do known edges
					eaPush(&accrueList, new_node);
				else
					eaPush(&otherBlock->genNodeList, new_node);

				if(otherBlock!=block)
				{
					eaPushUnique(&s_acgProcess.genBlockList, otherBlock);
					if(blocksOut) eaPushUnique(blocksOut, otherBlock);
				}
			}
		}
	}

	if(eaSize(&accrueList))
	{
		for(i=eaSize(&accrueList)-1; i>=0; i--)
		{
			AICivilianPathGenNodeBlock *otherBlock = acgGeneratorGetNodeBlockByGrid(accrueList[i]->grid_x, accrueList[i]->grid_z, 0);
			devassert(otherBlock);

			eaPush(&otherBlock->genNodeList, accrueList[i]);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
int acgNodeTouchesNode(AICivilianPathGenNode *node, AICivilianPathGenNode *other, CivGenState ignore)
{
	return node && (!other || other->touched==ignore || (node->type==other->type && fabs(node->y_coord-other->y_coord)<2));
}

// ------------------------------------------------------------------------------------------------------------------
int acgNodeIsEdge(AICivilianPathGenNode *node, CivGenState ignore)
{
	int i;

	for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
	{
		AICivilianPathGenNode *neighbor = NULL;

		neighbor = acgGeneratorGetNeighbor(node, offsetsCorners[i][0], offsetsCorners[i][1], 0);

		if(!acgNodeTouchesNode(node, neighbor, ignore))
			return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
int acgGeneratorDestroyNode(AICivilianPathGenNode *node)
{
	AICivilianPathGenNodeBlock *block = acgGeneratorGetNodeBlockByGrid(node->grid_x, node->grid_z, 0);
	devassert(block);

	block->nodes[node->block_x][node->block_z] = NULL;
	acgNodeFree(node);

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessBlockEdges(Entity *debugger, AICivilianPathGenNodeBlock *block)
{
	int i;
	for(i=0; i<NODE_BLOCK_SIZE; i++)
	{
		int j;
		for(j=0; j<NODE_BLOCK_SIZE; j++)
		{
			AICivilianPathGenNode *node = acgGeneratorGetNode(block->grid_x, block->grid_z, i, j, 0);

			if(node && node->touched!=s_acgProcess.state && node->type>=0)
			{
				node->touched = s_acgProcess.state;
				if(!acgNodeIsEdge(node, -1))
					acgGeneratorDestroyNode(node);
				else
					eaPush(&s_acgProcess.edgeStartList, node);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessGridNodes(int iPartitionIdx, Entity *debugger)
{
	int i;

	while(eaSize(&s_acgProcess.genBlockList))
	{
		AICivilianPathGenNodeBlock *block = eaPop(&s_acgProcess.genBlockList);
		static AICivilianPathGenNodeBlock **newBlocks = NULL;

		eaClear(&newBlocks);
		acgProcessBlockNodes(iPartitionIdx, debugger, block, 0, &newBlocks);

		// Process any edge nodes of other blocks, so edge process can be accurate
		for(i=eaSize(&newBlocks)-1; i>=0; i--)
			acgProcessBlockNodes(iPartitionIdx, debugger, newBlocks[i], 1, NULL);

		acgProcessBlockEdges(debugger, block);

		s_acgProcess.block.count++;

		if (s_acgProcess.block.count % 4 == 0)
		{
			printf("\tBlocks done %d/%d\r", s_acgProcess.block.count,
				(s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0])*(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]));
		}
	}

	//if(!eaSize(&s_acgProcess.genBlockList))
	{
	//	printf("\n");
	//	return 1;
	}

	//return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgGeneratorNodeContiguousNeighbors3by3(AICivilianPathGenNode *node, AICivilianPathGenNode ***lineOut)
{
	int i;
	int max_neighbors = 0;
	int neighbors = 0;
	AICivilianPathGenNode **neighborsArray = NULL;

	for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
	{
		AICivilianPathGenNode *neighbor = acgGeneratorGetNeighbor(node, offsetsCorners[i][0], offsetsCorners[i][1], 0);
		if(offsetsCorners[i][0] && offsetsCorners[i][1])
		{
			if(acgNodeTouchesNode(neighbor, node, -1))
			{
				neighbors++;
				eaPush(&neighborsArray, neighbor);
			}

			if(neighbors>max_neighbors)
			{
				max_neighbors = neighbors;
				eaCopy(lineOut, &neighborsArray);
			}

			eaClear(&neighborsArray);
			if(acgNodeTouchesNode(neighbor, node, -1))
			{
				neighbors = 1;
				eaPush(&neighborsArray, neighbor);
			}
			else
				neighbors = 0;
		}
		else if(acgNodeTouchesNode(neighbor, node, -1))
		{
			neighbors++;
			eaPush(&neighborsArray, neighbor);
		}
	}

	return max_neighbors;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgGeneratorNodeCountNeighbors(AICivilianPathGenNode *node, AICivilianPathGenNode ***ignoreArray)
{
	int i;
	int count = 0;

	for(i=0; i<ARRAY_SIZE(offsetsNoCorners); i++)
	{
		AICivilianPathGenNode *neighbor = acgGeneratorGetNeighbor(node, offsetsNoCorners[i][0], offsetsNoCorners[i][1], 0);

		if(neighbor && acgNodeTouchesNode(neighbor, node, -1) && (!ignoreArray || eaFind(ignoreArray, neighbor)==-1)) count++;
	}

	return count;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgGeneratorNodeCountNeighborsCorners(AICivilianPathGenNode *node, AICivilianPathGenNode *ignore)
{
	int i;
	int count = 0;

	for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
	{
		AICivilianPathGenNode *neighbor = acgGeneratorGetNeighbor(node, offsetsCorners[i][0], offsetsCorners[i][1], 0);

		if(neighbor && neighbor!=ignore && acgNodeTouchesNode(neighbor, node, -1) ) count++;
	}

	return count;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgGeneratorNodeMaxConsecutiveNeighbors(AICivilianPathGenNode *node, AICivilianPathGenNode *ignore)
{
	int i;
	int maxneighbors = 0;
	int neighbors = 0;

	for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
	{
		AICivilianPathGenNode *neighbor = acgGeneratorGetNeighbor(node, offsetsCorners[i][0], offsetsCorners[i][1], 0);

		if(neighbor!=ignore && acgNodeTouchesNode(node, neighbor, -1))
			neighbors++;
		else
		{
			if(neighbors>maxneighbors)
				maxneighbors = neighbors;
			neighbors = 0;
		}
	}

	return maxneighbors;
}

// ------------------------------------------------------------------------------------------------------------------
static int findNode(AICivilianPathGenNode **nodes, AICivilianPathGenNode *node)
{
	int j;
	for(j=0; j<eaSize(&nodes); j++)
	{
		if(nodes[j]->grid_x==node->grid_x && nodes[j]->grid_z==node->grid_z &&
			nodes[j]->block_x==node->block_x && nodes[j]->block_z==node->block_z)
			return 1;
	}
	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessEdgesCleanAreasHelper(AICivilianPathGenNode *node)
{
	static AICivilianPathGenNode **nodeList = NULL;
	static AICivilianPathGenNode **toTest = NULL;
	AICivilianPathGenNode tempNodes[200] = {0};
	int curTemp = 0;
	int foundInvalid = 0;
	Vec3 pos;
	void *invalid = NULL;
	EAICivilianLegType hitTypes[EAICivilianLegType_COUNT] = {0, 0, 0, 0, 0};
	F32 hitYMin = 0, hitYMax = 0;

	eaSetSize(&nodeList, 0);
	eaSetSize(&toTest, 0);
	eaPush(&toTest, node);
	while(eaSize(&nodeList)+eaSize(&toTest)<ARRAY_SIZE(tempNodes) && eaSize(&toTest)>0)
	{
		int i;
		AICivilianPathGenNode *test = eaPop(&toTest);
		foundInvalid = 0;
		acgGetPathGenNodePosition(node, pos);
		if(acg_d_pos && distance3SquaredXZ(pos, s_acgDebugPos)<SQR(2))
			printf("");
		eaPush(&nodeList, test);
		for(i=0; i<ARRAY_SIZE(offsetsCorners) && eaSize(&nodeList)+eaSize(&toTest)<ARRAY_SIZE(tempNodes); i++)
		{
			int gx, gz, bx, bz;
			AICivilianPathGenNode *t = &tempNodes[curTemp];
			AICivilianPathGenNode *atT = NULL;

			*t = *test;
			t->type = 0;

			gx = t->grid_x; gz = t->grid_z; bx = t->block_x; bz = t->block_z;
			bx += offsetsCorners[i][0];
			bz += offsetsCorners[i][1];
			acgGeneratorValidateLocation(&gx, &gz, &bx, &bz);
			t->grid_x = gx; t->grid_z = gz; t->block_x = bx; t->block_z = bz;
			atT = acgGeneratorGetNeighbor(t, 0, 0, 0);

			acgGetPathGenNodePosition(t, pos);
			if(acg_d_pos && distance3SquaredXZ(pos, s_acgDebugPos)<SQR(2))
				printf("");

			if(*((U32*)&atT)&0x1)
			{
				foundInvalid = 1;  // Don't break now to make sure toTest gets all neighbors
				invalid = atT;
			}
			else if(atT && atT->type != EAICivilianLegType_NONE)
			{
				// Cannot hit car and pedestrian types
				int typeIndex = atT->type;

				if(!hitYMin || atT->y_coord < hitYMin)
					hitYMin = atT->y_coord;
				if(!hitYMax || atT->y_coord > hitYMax)
					hitYMax = atT->y_coord;

				hitTypes[typeIndex] = 1;
				if(hitTypes[0] && hitTypes[1])
				{
					foundInvalid = 1;
					break;
				}
				if(fabs(hitYMax-hitYMin)>5)
				{
					foundInvalid = 1;
					break;
				}
				continue;
			}
			else
			{
				if(!atT)
				{
					if(!findNode(toTest, t) && !findNode(nodeList, t))
					{
						curTemp++;
						eaPush(&toTest, t);
					}
				}
				else if(!findNode(toTest, atT) && !findNode(nodeList, atT))
				{
					devassert(eaFind(&toTest, atT)==-1 && eaFind(&nodeList,atT)==-1);
					eaPush(&toTest, atT);
				}
			}
		}

		if(foundInvalid)
			break;
	}

	if(!foundInvalid && eaSize(&nodeList)<ARRAY_SIZE(tempNodes) && eaSize(&toTest)==0)
	{	// Passed the check, destroy all without a non-NULL edge neighbor
		int i;
		AICivilianPathGenNode **edgeNodes = NULL;
		// Gather all neighbors and destroy the -1 edge nodes
		for(i=0; i<eaSize(&nodeList); i++)
		{
			AICivilianPathGenNode *fakeNode = nodeList[i];
			if(fakeNode->type == EAICivilianLegType_NONE)
			{
				int j;
				for(j = 0; j < ARRAY_SIZE(offsetsCorners); j++)
				{
					AICivilianPathGenNode *n = acgGeneratorGetNeighbor(fakeNode, offsetsCorners[j][0], offsetsCorners[j][1], 0);

					if(n && n->type!= EAICivilianLegType_NONE)
						eaPushUnique(&edgeNodes, n);
				}
				fakeNode->touched = s_acgProcess.state;
			}
		}
		// After destroying, see if the neighbors are still edges, if not, destroy them
		for(i=0; i<eaSize(&edgeNodes); i++)
		{
			if(!acgNodeIsEdge(edgeNodes[i], s_acgProcess.state))
				edgeNodes[i]->touched = s_acgProcess.state;
		}
		eaDestroy(&edgeNodes);
	}
	else
	{	// Failed, set all to 0x00000001 so nothing checks these in the future
		int i;
		for(i = 0; i < eaSize(&nodeList); i++)
		{
			if(nodeList[i]->type != EAICivilianLegType_NONE)
			{
				AICivilianPathGenNode **nodeRef = acgGeneratorGetNodeReference(nodeList[i], 0, 0);

				devassert(*nodeRef==NULL || *((U32*)(nodeRef))&(0x1));
				if(foundInvalid)
					*nodeRef = invalid;
				else
					*nodeRef = (void*)((*(U64*)&node)|0x1);
			}
		}
	}

	eaDestroy(&nodeList);
	eaDestroy(&toTest);
}

// ------------------------------------------------------------------------------------------------------------------
#define acgDelSaveNode(n) (acgDelSaveNodeEx(n, 0))

static void acgDelSaveNodeEx(AICivilianPathGenNode *node, int subState)
{
	ACGDelNode *dn = StructAlloc(parse_ACGDelNode);
	acgGetPathGenNodePosition(node, dn->pos);
	dn->delState = s_acgProcess.state;
	dn->type = node->type;
	dn->delSubState = subState;

	eaPush(&s_acgProcess.deletedNodes, dn);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgDelSaveLine(AICivilianPathGenLine *line)
{
	ACGDelLine *dl = StructAlloc(parse_ACGDelLine);
	AICivilianPathGenNode *node = eaHead(&line->nodes);
	acgGetPathGenNodePosition(eaHead(&line->nodes), dl->start);
	acgGetPathGenNodePosition(eaTail(&line->nodes), dl->end);
	dl->delState = s_acgProcess.state;

	ANALYSIS_ASSUME(node);
	dl->type = node->type;

	eaPush(&s_acgProcess.deletedLines, dl);
}

// ------------------------------------------------------------------------------------------------------------------
static int acgProcessEdgesCleanAreas(Entity *debugger)
{
	eaPushEArray(&s_acgProcess.areaClean.nodes, &s_acgProcess.edgeStartList);
	
	while(eaSize(&s_acgProcess.areaClean.nodes))
	{
		int i;
		AICivilianPathGenNode *node = eaPop(&s_acgProcess.areaClean.nodes);

		if(node->touched == s_acgProcess.state)
			continue;

		for(i=0; i<ARRAY_SIZE(offsetsNoCorners); i++)
		{
			AICivilianPathGenNode *n = acgGeneratorGetNeighbor(node, offsetsNoCorners[i][0], offsetsNoCorners[i][1], 0);

			if(n && !(*((U32*)&n)&0x1) && n->touched!=s_acgProcess.state && n->type == EAICivilianLegType_NONE)
				acgProcessEdgesCleanAreasHelper(n);
		}
	}

	if(!eaSize(&s_acgProcess.areaClean.nodes))
	{
		int i, j, k, l;
		// Quickly get rid of cleaned up nodes
		for(i=eaSize(&s_acgProcess.edgeStartList)-1; i>=0; i--)
			if(s_acgProcess.edgeStartList[i]->touched==s_acgProcess.state)
				eaRemoveFast(&s_acgProcess.edgeStartList, i);

		// Clean up all node pointers so nothing crashes later
		for(i=0; i<s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1; i++)
		{
			for(j=0; j<s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1; j++)
			{
				AICivilianPathGenNodeBlock *block = acgGeneratorGetNodeBlockByGrid(i, j, 0);
				if(block)
				{
					for(k=0; k<NODE_BLOCK_SIZE; k++)
					{
						for(l=0; l<NODE_BLOCK_SIZE; l++)
						{
							if(*((U32*)&block->nodes[k][l])&0x1)
								block->nodes[k][l] = NULL;
							if(block->nodes[k][l] && block->nodes[k][l]->touched==s_acgProcess.state)
							{
								acgDelSaveNode(block->nodes[k][l]);
								acgGeneratorDestroyNode(block->nodes[k][l]);
							}
						}
					}
				}
			}
		}
		eaDestroy(&s_acgProcess.areaClean.nodes);
		return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessEdgesCleanInvalid(Entity *debugger)
{
	int gx, gz, bx, bz;
	// If a node is only surrounded by -1 and bits of the same line, it is too thin
	for(gx=0; gx<s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1; gx++)
	{
		for(gz=0; gz<s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1; gz++)
		{
			AICivilianPathGenNodeBlock *block = acgGeneratorGetNodeBlockByGrid(gx, gz, 0);
			if(block)
			{
				for(bx=0; bx<NODE_BLOCK_SIZE; bx++)
				{
					for(bz=0; bz<NODE_BLOCK_SIZE; bz++)
					{
						AICivilianPathGenNode *node = acgGeneratorGetNode(gx, gz, bx, bz, 0);
						int i;
						int foundOther = 0;
						Vec3 vPos;

						if(!node || node->type == EAICivilianLegType_NONE)
							continue;

						acgGetPathGenNodePosition(node, vPos);
						if(acg_d_pos && distance3SquaredXZ(vPos, s_acgDebugPos) < SQR(2))
							printf("");


						for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
						{
							AICivilianPathGenNode *n;

							n = acgGeneratorGetNeighbor(node, offsetsCorners[i][0], offsetsCorners[i][1], 0);

							if(!n)
							{
								foundOther = 1;
								break;
							}
							else if(n->type != EAICivilianLegType_NONE && (n->type!=node->type || fabs(n->y_coord-node->y_coord)>2))
							{
								foundOther = 1;
								break;
							}
						}

						if(!foundOther)
							node->touched = s_acgProcess.state;
					}
				}
			}
		}
	}

	for(gx=eaSize(&s_acgProcess.edgeStartList)-1; gx>=0; gx--)
	{
		AICivilianPathGenNode *node = s_acgProcess.edgeStartList[gx];

		if(node->touched==s_acgProcess.state)
		{
			eaRemoveFast(&s_acgProcess.edgeStartList, gx);
			acgDelSaveNode(node);
			acgGeneratorDestroyNode(node);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessEdgesCleanExtraneous(Entity *debugger)
{
	int i = 0;

	// See if you can remove it and and a few levels of orphans without creating orphans
	for(i = eaSize(&s_acgProcess.edgeStartList)-1; i >= 0; i--)
	{
		AICivilianPathGenNode *node = s_acgProcess.edgeStartList[i];
		Vec3 world_pos;

		acgGetPathGenNodePosition(node, world_pos);
		if(acg_d_pos && distance3SquaredXZ(world_pos, s_acgDebugPos)<SQR(6))
			printf("");

		if(node->touched==s_acgProcess.state)
			continue;

		if(acgGeneratorNodeCountNeighborsCorners(node, NULL) < 1)
		{
			eaRemoveFast(&s_acgProcess.edgeStartList, i);
			acgDelSaveNodeEx(node, 0);
			acgGeneratorDestroyNode(node);
			continue;
		}

		if(acgGeneratorNodeMaxConsecutiveNeighbors(node, NULL)>2)
		{
			int tested_nodes = 0;
			int node_index = 0;
			int bad = 0;
			AICivilianPathGenNode **nodes = NULL;

			eaPush(&nodes, node);

			// TODO (AM): Rewrite this to be, pull off node, check all neighbors for >=2
			// If passes, remove nodes.  Else, take all <2 neighbors and test again.
			for(node_index=0; node_index<eaSize(&nodes) && tested_nodes < 10; node_index++)
			{
				int j;
				AICivilianPathGenNode *test_node = nodes[node_index];
				tested_nodes++;

				for(j=0; j<ARRAY_SIZE(offsetsNoCorners); j++)
				{
					AICivilianPathGenNode *test_neighbor = acgGeneratorGetNeighbor(test_node, offsetsNoCorners[j][0], offsetsNoCorners[j][1], 0);

					if(test_neighbor && acgNodeTouchesNode(test_neighbor, test_node, -1))
					{
						if(acgGeneratorNodeCountNeighbors(test_neighbor, &nodes)<=1)
							eaPushUnique(&nodes, test_neighbor);
					}
				}
			}

			if(tested_nodes<10)
			{
				//succeeded
				int j;

				for(j=0; j<eaSize(&nodes); j++)
					nodes[j]->touched = s_acgProcess.state;
			}

			eaDestroy(&nodes);
		}
	}

	for(i = eaSize(&s_acgProcess.edgeStartList)-1; i>=0; i--)
	{
		AICivilianPathGenNode *node = s_acgProcess.edgeStartList[i];

		if(node->touched==s_acgProcess.state)
		{
			acgDelSaveNodeEx(node, 1);
			eaRemoveFast(&s_acgProcess.edgeStartList, i);
			acgGeneratorDestroyNode(node);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
int calcLineLen(AICivilianPathGenLine *line)
{
	return eaSize(&line->nodes);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessLines(Entity *debugger)
{
	if(!s_acgProcess.lines.reset)
	{
		int i;
		for(i=eaSize(&s_acgProcess.edgeStartList)-1; i>=0; i--)
			s_acgProcess.edgeStartList[i]->line = NULL;
		s_acgProcess.lines.reset = 1;
	}

	while(eaSize(&s_acgProcess.edgeStartList))
	{
		AICivilianPathGenLine *line = NULL;
		int count;
		Vec3 pos;

		if(!s_acgProcess.lines.node)
			s_acgProcess.lines.node = eaPop(&s_acgProcess.edgeStartList);

		if(s_acgProcess.lines.node->line)
		{
			s_acgProcess.lines.node = NULL;
			continue;
		}

		acgGetPathGenNodePosition(s_acgProcess.lines.node, pos);
		if(acg_d_pos && distance3SquaredXZ(pos, s_acgDebugPos)<SQR(2))
			printf("");

		line = acgLineAlloc();
		for(count=0; count<=10; count++)
		{
			int i;
			AICivilianPathGenNode *newnode = NULL;
			s_acgProcess.lines.node->line = line;
			eaPush(&line->nodes, s_acgProcess.lines.node);

			for(i=0; i<2; i++)
			{
				int j;
				for(j=0; j<2; j++)
				{
					int dirx = 0, dirz = 0;
					AICivilianPathGenNode *neighbor = NULL;
					if(!j)
						dirx = i ? -1 : 1;
					else
						dirz = i ? -1 : 1;

					neighbor = acgGeneratorGetNeighbor(s_acgProcess.lines.node, dirx, dirz, 0);

					if(neighbor)
					{
						acgGetPathGenNodePosition(neighbor, pos);
						if(acg_d_pos && distance3SquaredXZ(pos, s_acgDebugPos)<SQR(2))
							printf("");
					}

					if(neighbor && !neighbor->line &&
						acgNodeTouchesNode(s_acgProcess.lines.node, neighbor, -1))
					{
						newnode = neighbor;
						break;
					}
				}
				if(newnode)
					break;
			}

			if(newnode)
			{
				s_acgProcess.lines.node = newnode;
			}
			else
			{
				s_acgProcess.lines.node = NULL;
				break;
			}
		}

		eaPush(&s_acgProcess.lineList, line);

	}

	eaDestroy(&s_acgProcess.edgeStartList);
	
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianPathGenLine* acgGeneratorFindNeighborLine(AICivilianPathGenNode *node, AICivilianPathGenNode **lineNodeOut)
{
	int i;
	for(i=0; i<2; i++)
	{
		int j;
		for(j=0; j<2; j++)
		{
			int dirx = 0, dirz = 0;
			AICivilianPathGenNode *neighbor = NULL;
			if(!j)
				dirx = i ? -1 : 1;
			else
				dirz = i ? -1 : 1;

			neighbor = acgGeneratorGetNeighbor(node, dirx, dirz, 0);

			if(neighbor && neighbor->line && neighbor->line!=node->line && acgNodeTouchesNode(node, neighbor, -1))
			{
				if(lineNodeOut)
					*lineNodeOut = neighbor;
				return neighbor->line;
			}
		}
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLineSetNewLine(const AICivilianPathGenLine *old, AICivilianPathGenLine *newline)
{
	int i;

	if(eaSize(&old->nodes)==0)
		return;

	devassert(newline);
	for(i=0; i<eaSize(&old->nodes); i++)
		old->nodes[i]->line = newline;
}

#if 0
static void acgLine_GetBestFitLine(const AICivilianPathGenNode **eaNodes1, const AICivilianPathGenNode **eaNodes2,
								   Vec3 vStart, Vec3 vEnd, Vec3 vDir)
{
	F32 num = (F32)eaSize(&line->nodes);
	F32 m, b;
	F32 sumXZ, sumX, sumZ, sumXSqr;
	Vec3 vNodePos;

	if (!eaSize(&line->nodes))
	{
		zeroVec3(vStart);
		zeroVec3(vEnd);
		return;
	}

	sumXZ = sumX = sumZ = sumXSqr = 0.f;

	acgGetPathGenNodePosition(eaHead(&line->nodes), vStart);
	acgGetPathGenNodePosition(eaTail(&line->nodes), vEnd);

	FOR_EACH_IN_EARRAY(line->nodes, AICivilianPathGenNode, pNode)
		acgGetPathGenNodePosition(pNode, vNodePos);
		sumXZ += vNodePos[0] + vNodePos[2];
		sumX += vNodePos[0];
		sumZ += vNodePos[2];
		sumX += vNodePos[0]*vNodePos[0];
 
	FOR_EACH_END;
	
	// 
	m = num * sumXSqr - sumX * sumX;
	if (m == 0.f)
	{
		acgGetPathGenNodePosition(eaHead(&line->nodes), vStart);
		acgGetPathGenNodePosition(eaTail(&line->nodes), vEnd);
		return; 
	}
	m = (num * sumXZ - sumX * sumZ) / m;
	b = (sumZ - m * sumX) / num;

	vStart[0] 
	
}

#endif 
// what we really want is a statistical best line fit from a set of points
// but since we can assume the points are continuous, 
// we're doing just a little bit of end-point averaging and projection
static F32 acgLine_GetBestFitLine(const AICivilianPathGenNode **eaNodes1, const AICivilianPathGenNode **eaNodes2,
								   Vec3 vStart, Vec3 vEnd, Vec3 vDir)
{
#if 1
	const AICivilianPathGenNode *pNode;
	Vec3 vHeadPos, vTailPos, vTmp;
	Vec3 vAverageHeadPos, vAverageTailPos;
	F32 fLength, fDot;

	if (!eaNodes2 || eaSize(&eaNodes2) == 0)
		eaNodes2 = eaNodes1;
	
	if (eaSize(&eaNodes1) + eaSize(&eaNodes2) <= 2)
	{
		if (eaSize(&eaNodes1) == 0)
		{
			zeroVec3(vStart);
			zeroVec3(vEnd);
			zeroVec3(vDir);
			return 0.f;
		}
		acgGetPathGenNodePosition(eaHead(&eaNodes1), vStart);
		acgGetPathGenNodePosition(eaTail(&eaNodes2), vEnd);
		subVec3(vEnd, vStart, vDir);
		return normalVec3(vDir);
	}

#if 0
	{
		//S32 i;
		Vec3 vBaryCenter, vNodePos;
		F32 fBaryScalar = eaSize(&eaNodes1) + ((eaNodes1 != eaNodes2)? eaSize(&eaNodes2) : 0);
		fBaryScalar = 1.0f / fBaryScalar;
	
		zeroVec3(vBaryCenter);
		{
			FOR_EACH_IN_EARRAY(eaNodes1, const AICivilianPathGenNode, pNode)
				acgGetPathGenNodePosition(pNode, vNodePos);
				scaleAddVec3(vNodePos, fBaryScalar, vBaryCenter, vBaryCenter);
			FOR_EACH_END;
		}
		

		if (eaNodes2 != eaNodes1)
		{
			FOR_EACH_IN_EARRAY(eaNodes2, const AICivilianPathGenNode, pNode)
				acgGetPathGenNodePosition(pNode, vNodePos);
				scaleAddVec3(vNodePos, fBaryScalar, vBaryCenter, vBaryCenter);
			FOR_EACH_END;
		}
	
		/*
		pNode = eaGet(&eaNodes1, 0);
		acgGetPathGenNodePosition(pNode, vHeadPos);
		for (i = 1; i < sizeof(&eaNodes1); --i)
		{
			const AICivilianPathGenNode *pNode = eaNodes1[i];
		}
		*/
	}
#endif
	
	// get the averaged head pos
	pNode = eaGet(&eaNodes1, 0);
	acgGetPathGenNodePosition(pNode, vHeadPos);
	pNode = eaGet(&eaNodes1, 1);
	acgGetPathGenNodePosition(pNode, vTmp);
	interpVec3(0.5f, vHeadPos, vTmp, vAverageHeadPos);

	// get the averaged tail pos
	pNode = eaGet(&eaNodes2, eaSize(&eaNodes2) - 1);
	acgGetPathGenNodePosition(pNode, vTailPos);
	pNode = eaGet(&eaNodes2, eaSize(&eaNodes2) - 2);
	acgGetPathGenNodePosition(pNode, vTmp);
	interpVec3(0.5f, vTailPos, vTmp, vAverageTailPos);

	// Get the line direction
	subVec3(vAverageTailPos, vAverageHeadPos, vDir);
	fLength = normalVec3(vDir);

	// fix-up the start/end positions
	subVec3(vHeadPos, vAverageHeadPos, vTmp);
	fDot = dotVec3(vTmp, vDir);
	scaleAddVec3(vDir, fDot, vAverageHeadPos, vStart);
	// 
	subVec3(vTailPos, vAverageTailPos, vTmp);
	fDot = dotVec3(vTmp, vDir);
	scaleAddVec3(vDir, fDot, vAverageTailPos, vEnd);
	
	// we should be calculating the same direction, 
	// but we need the true length, so just recalculating it
	subVec3(vEnd, vStart, vDir);
	return normalVec3(vDir);
#else
	F32 num;
	F32 m, b;
	F32 sumXZ, sumX, sumZ, sumXSqr;
	Vec3 vNodePos;

	if (!eaNodes2 || eaSize(&eaNodes2) == 0)
		eaNodes2 = eaNodes1;
	if (eaSize(&eaNodes1) + eaSize(&eaNodes2) <= 2)
	{
		if (eaSize(&eaNodes1) == 0)
		{
			zeroVec3(vStart);
			zeroVec3(vEnd);
			zeroVec3(vDir);
			return 0.f;
		}
		acgGetPathGenNodePosition(eaHead(&eaNodes1), vStart);
		acgGetPathGenNodePosition(eaTail(&eaNodes2), vEnd);
		subVec3(vEnd, vStart, vDir);
		return normalVec3(vDir);
	}

	sumXZ = sumX = sumZ = sumXSqr = 0.f;

	acgGetPathGenNodePosition(eaHead(&eaNodes1), vStart);
	acgGetPathGenNodePosition(eaTail(&eaNodes2), vEnd);

	num = (F32)eaSize(&eaNodes1);
	FOR_EACH_IN_EARRAY(eaNodes1, const AICivilianPathGenNode, pNode)
		acgGetPathGenNodePosition(pNode, vNodePos);
		sumXZ += vNodePos[0] * vNodePos[2];
		sumX += vNodePos[0];
		sumZ += vNodePos[2];
		sumXSqr += vNodePos[0]*vNodePos[0];
	FOR_EACH_END;
	if (eaNodes1 != eaNodes2)
	{
		num += (F32)eaSize(&eaNodes2);

		FOR_EACH_IN_EARRAY(eaNodes2, const AICivilianPathGenNode, pNode)
			acgGetPathGenNodePosition(pNode, vNodePos);
			sumXZ += vNodePos[0] + vNodePos[2];
			sumX += vNodePos[0];
			sumZ += vNodePos[2];
			sumXSqr += vNodePos[0]*vNodePos[0];
		FOR_EACH_END;
	}

	// 
	m = num * sumXSqr - sumX * sumX;
	if (m == 0.f)
	{
		acgGetPathGenNodePosition(eaHead(&eaNodes1), vStart);
		acgGetPathGenNodePosition(eaTail(&eaNodes2), vEnd);
		subVec3(vEnd, vStart, vDir);
		return normalVec3(vDir);
	}
	m = (num * sumXZ - sumX * sumZ) / m;
	b = (sumZ - m * sumX) / num;

	vStart[2] = m*vStart[0] + b;

	vEnd[2] = m*vEnd[0] + b;
	subVec3(vEnd, vStart, vDir);
	normalVec3(vDir);

	{
		Vec3 vHeadPos, vTailPos, vTmp;
		F32 fDot;

		acgGetPathGenNodePosition(eaHead(&eaNodes1), vHeadPos);
		acgGetPathGenNodePosition(eaTail(&eaNodes2), vTailPos);

		// fix-up the start/end positions
		subVec3(vHeadPos, vStart, vTmp);
		fDot = dotVec3(vTmp, vDir);
		scaleAddVec3(vDir, fDot, vStart, vStart);
		// 
		subVec3(vTailPos, vEnd, vTmp);
		fDot = dotVec3(vTmp, vDir);
		scaleAddVec3(vDir, fDot, vEnd, vEnd);

		// we should be calculating the same direction, 
		// but we need the true length, so just recalculating it
		subVec3(vEnd, vStart, vDir);
		return normalVec3(vDir);
	}
#endif
}


static F32 acgMergedLineError(AICivilianPathGenLine *line, AICivilianPathGenLine *toMergeLine, bool insertAtEnd)
{
	Vec3 vLineStart, vLineEnd, vLineDir, vNodeDir, vNodePos;
	F32 fLineLen;
	S32 i;
	F32 fErrorDot, fErrorLen, fError;
	AICivilianPathGenNode **resultLine = NULL;
#define MIN_LINE_NODE_LEN	15


	fErrorDot = 1.0f;
	fErrorLen = 0.0f;
	fError = 0.0f;

	if (eaSize(&line->nodes) <= MIN_LINE_NODE_LEN)
		return 0.0f;
	
	if (insertAtEnd)
	{
		eaPushEArray(&resultLine, &line->nodes);
		eaPushEArray(&resultLine, &toMergeLine->nodes);
	}
	else
	{
		eaPushEArray(&resultLine, &line->nodes);
		eaInsertEArray(&resultLine, &toMergeLine->nodes, 0);
	}
	
#if 0
	{
		acgGetPathGenNodePosition(eaHead(&resultLine), vLineStart);
		acgGetPathGenNodePosition(eaTail(&resultLine), vLineEnd);
				
		subVec3(vLineEnd, vLineStart, vLineDir);
		fLineLen = normalVec3(vLineDir);
	}
#else
	{
		fLineLen = acgLine_GetBestFitLine(resultLine, NULL, vLineStart, vLineEnd, vLineDir);
	}
#endif	


	for (i = MIN_LINE_NODE_LEN; i < eaSize(&resultLine); i++)
	{
		AICivilianPathGenNode *node = resultLine[i];
		F32 fCurLen, fDot, fVal;

		acgGetPathGenNodePosition(node, vNodePos);
		subVec3(vNodePos, vLineStart, vNodeDir);
		fCurLen = normalVec3(vNodeDir);
		fDot = dotVec3(vNodeDir, vLineDir);

		fDot = MIN(fDot, 1.0f);

		fVal = acos(fDot);
		fVal = fVal / QUARTERPI;
		fVal = fVal * fLineLen;

		if (fVal > fError)
		{
			fError = fVal;
			fErrorLen = fCurLen;
			fErrorDot = fDot;
		}
	}

	eaDestroy(&resultLine);

	return fError;
}

static F32 acgLine_GetPotentialError(const AICivilianPathGenLine *line, const AICivilianPathGenLine *otherLine, bool end)
{
	Vec3 vOldLineDir, vOtherLineDir, vNewDir, vStart, vEnd;
	F32 err = 0.f;

	acgLine_GetBestFitLine(line->nodes, NULL, vStart, vEnd, vOldLineDir);
	acgLine_GetBestFitLine(otherLine->nodes, NULL, vStart, vEnd, vOtherLineDir);

	if(end)
	{
		//eaPushEArray(&line->nodes, &line_test->nodes);
		acgLine_GetBestFitLine(line->nodes, otherLine->nodes, vStart, vEnd, vNewDir);
	}
	else
	{
		//eaInsertEArray(&line->nodes, &line_test->nodes, 0);
		acgLine_GetBestFitLine(otherLine->nodes, line->nodes, vStart, vEnd, vNewDir );
	}
	
	{
		F32 fErr1, fErr2;

		err = MAX(line->fMergeError, otherLine->fMergeError);

		fErr1 = acgMath_GetAngleBetweenNormsAbs(vNewDir, vOldLineDir);
		fErr2 = acgMath_GetAngleBetweenNormsAbs(vOtherLineDir, vOldLineDir);

		err += MAX(fErr1, fErr2);
	}

	return err;
}

static void acgLine_Merge(AICivilianPathGenLine *line, AICivilianPathGenLine *otherLine, bool end)
{
	line->fMergeError = acgLine_GetPotentialError(line, otherLine, end);

	acgLineSetNewLine(otherLine, line);

	// Integrate new nodes in line list
	if(end)
	{
		eaPushEArray(&line->nodes, &otherLine->nodes);
	}
	else
	{
		eaInsertEArray(&line->nodes, &otherLine->nodes, 0);
	}

	otherLine->clean_up = 1;
}


// ------------------------------------------------------------------------------------------------------------------
#define ACG_LINE_ANGLE_TOL		(cos(RAD(3.f))) // (cos(RAD(2.56f)))
#define ACG_LINE_SKIP_ANGLE_TOL (cos(RAD(18.3f))) // ACG_LINE_ANGLE_TOL - 0.05f)

#define LINE_MERGE_ANGLE_TOL	RAD(15.f)
#define LINE_MERGE_MAX_ERROR	RAD(10.f)
int acgProcessLinesMergeHelperNoSkip(Entity *debugger, AICivilianPathGenLine *line, bool end)
{
	AICivilianPathGenLine *line_test;
	Vec3 line_start, line_end, line_dir;
			
	int modified = 0;

	acgLine_GetBestFitLine(line->nodes, NULL, line_start, line_end, line_dir);
	
	if(end)
	{
		line_test = acgGeneratorFindNeighborLine(eaTail(&line->nodes), NULL);
	}
	else
	{
		line_test = acgGeneratorFindNeighborLine(eaHead(&line->nodes), NULL);
	}
	
	if(!line_test || line_test==line)
		return 0;

	{
		Vec3 test_start, test_end, test_dir;
		F32 mergedDot, fError;
		F32 fAngleDiff;

		acgLine_GetBestFitLine(line_test->nodes, NULL, test_start, test_end, test_dir);
		
		fAngleDiff = acgMath_GetAngleBetweenNorms(test_dir, line_dir);
				
		// 
		{
			Vec3 skippedDir;

			if (end)
			{
				subVec3(test_end, line_start, skippedDir);
				normalVec3(skippedDir);
				mergedDot = dotVec3(skippedDir, line_dir);
			}
			else
			{
				subVec3(test_end, line_end, skippedDir);
				normalVec3(skippedDir);
				mergedDot = dotVec3(skippedDir, line_dir);
			}
		}

		
		if (mergedDot > 0.0f && fAngleDiff < LINE_MERGE_ANGLE_TOL) 
		{
			fError = acgLine_GetPotentialError(line, line_test, end);
			if (fError < LINE_MERGE_MAX_ERROR)
			{
				acgLine_Merge(line, line_test, end);
				modified = 1;
			}
		}
		
#if 0
		// Don't use absolute value of dots, because they HAVE to face the SAME dir
		if(	(test_dot > ACG_LINE_ANGLE_TOL && mergedDot > 0.0f) || 
			(test_dot > cos(RAD(12.0f)) && mergedDot > cos(RAD(6.25f))))
		{

			if (acgMergedLineError(line, line_test, end) < 6.0f)//0.999f)
			{
				modified = 1;

				acgLineSetNewLine(line_test, line);

				// Integrate new nodes in line list
				if(end)
					eaPushEArray(&line->nodes, &line_test->nodes);
				else
					eaInsertEArray(&line->nodes, &line_test->nodes, 0);

				line_test->clean_up = 1;
			}
		}
#endif
	}

	return modified;
}


// ------------------------------------------------------------------------------------------------------------------
#define ACG_LINE_MERGE_PROTRUSION_DIST	(25.0f)

int acgProcessLinesMergeHelperWithSkip(Entity *debugger, AICivilianPathGenLine *line, int end)
{
	AICivilianPathGenLine *line_test;
	AICivilianPathGenNode *node_test = NULL;
	static AICivilianPathGenLine **test_lines = NULL;
	F32 dist = 0;
	Vec3 line_start, line_end, line_dir;
	F32 line_len;
	Vec3 test_start, test_end, test_dir;
	F32 test_dot, test_len;
	int modified = 0;

	line_len = acgLine_GetBestFitLine(line->nodes, NULL, line_start, line_end, line_dir);

	if(end)
		line_test = acgGeneratorFindNeighborLine(eaTail(&line->nodes), NULL);
	else
		line_test = acgGeneratorFindNeighborLine(eaHead(&line->nodes), NULL);
	eaClear(&test_lines);
	while(dist <= ACG_LINE_MERGE_PROTRUSION_DIST)
	{
		Vec3 skippedDir;
		F32 skipDot = ACG_LINE_SKIP_ANGLE_TOL;
		
		if(!line_test)
			break;

		if(eaFind(&test_lines, line_test)!=-1 || line_test==line)  // No loops
			break;
		eaPush(&test_lines, line_test);

		test_len = acgLine_GetBestFitLine(line_test->nodes, NULL, test_start, test_end, test_dir);
		test_dot = dotVec3(test_dir, line_dir);
		if(node_test)
		{
			acgGetPathGenNodePosition(node_test, skippedDir);

			if(end)
				subVec3(skippedDir, line_end, skippedDir);
			else
				subVec3(skippedDir, line_start, skippedDir);
			normalVec3(skippedDir);
			skipDot = dotVec3(skippedDir, line_dir);
		}

		// Don't use absolute value of dots, because they HAVE to face the SAME dir
		if(	(node_test && skipDot > ACG_LINE_SKIP_ANGLE_TOL) && (test_dot > ACG_LINE_ANGLE_TOL) )
		{
			int j;
			modified = 1;
			for(j=0; j<eaSize(&test_lines); j++)
			{
				AICivilianPathGenLine *toMerge = test_lines[j];

				acgLineSetNewLine(toMerge, line);

				// Integrate new nodes in line list
				if(end)
					eaPushEArray(&line->nodes, &toMerge->nodes);
				else
					eaInsertEArray(&line->nodes, &toMerge->nodes, 0);

				toMerge->clean_up = 1;
			}
			break;
		}

		dist += test_len;
		if(end)
			line_test = acgGeneratorFindNeighborLine(eaTail(&line_test->nodes), &node_test);
		else
			line_test = acgGeneratorFindNeighborLine(eaHead(&line_test->nodes), &node_test);
		devassert(!line_test || !line_test->clean_up);
	}

	return modified;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessLinesMerge(Entity* debugger, bool doSkip)
{
	S32 i;
	
	for(i = 0; i < eaSize(&s_acgProcess.lineList); i++)
	{
		AICivilianPathGenLine *line;
		int modified = 0;
		devassert(s_acgProcess.lineList);
		line = s_acgProcess.lineList[i];

		if(line->touched == s_acgProcess.state || line->clean_up)
			continue;

		// debug breakpoint block
		{
			Vec3 start, end, dir;
			acgLine_GetBestFitLine(line->nodes, NULL, start, end, dir);

			if(acg_d_pos && (distance3SquaredXZ(start, s_acgDebugPos)<SQR(6) ||
							 distance3SquaredXZ(end, s_acgDebugPos)<SQR(6)))
				printf("");
		}

		while(line)
		{
			if (doSkip)
			{
				modified = acgProcessLinesMergeHelperWithSkip(debugger, line, 1);
				modified = acgProcessLinesMergeHelperWithSkip(debugger, line, 0) || modified;  // Make sure it gets executed
			}
			else
			{
				modified = acgProcessLinesMergeHelperNoSkip(debugger, line, 1);
				modified = acgProcessLinesMergeHelperNoSkip(debugger, line, 0) || modified;  // Make sure it gets executed
			}
			

			if(!modified)
				line = NULL;
		}
	}

	{
		for(i = eaSize(&s_acgProcess.lineList)-1; i >= 0; i--)
		{
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];
			if(line->clean_up)
			{
				line->clean_up = 0;
				//acgDelSaveLine(line);
				eaRemoveFast(&s_acgProcess.lineList, i);
				acgDestroyPathGenLine(line);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static F32 calcError(const AICivilianPathGenLine *line, F32 *distOut, int save)
{
	int i;
	F32 error = 0;
	Vec3 start, dir;
	F32 len;

	if(!line || !eaSize(&line->nodes))
		return 0;

	{
		acgGetPathGenNodePosition(eaHead(&line->nodes), start);
		acgGetPathGenNodePosition(eaTail(&line->nodes), dir);
		subVec3(dir, start, dir);
		len = normalVec3(dir);
	}
	

	if(distOut)
		*distOut = len;

	for(i=0; i<eaSize(&line->nodes); i++)
	{
		Vec3 cursor_pos;
		acgGetPathGenNodePosition(line->nodes[i], cursor_pos);
		if(save && line->nodes[i]->userFloat)
			error += line->nodes[i]->userFloat;
		else
		{
			F32 e = PointLineDistSquared(cursor_pos, start, dir, len, NULL);
			if(save)
				line->nodes[i]->userFloat = e;
			error += e;
		}
	}

	return error;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLineStealPointsFrom(AICivilianPathGenLine *line, AICivilianPathGenLine *other, int from)
{
	AICivilianPathGenNode *cursor;
	F32 min_error, min_error_other;
	F32 error, error_other;
	F32 d1 = 0, d2 = 0, dt;

	if(from)
		cursor = eaHead(&other->nodes);
	else
		cursor = eaTail(&other->nodes);
	min_error = calcError(line, &d1, 0);
	min_error_other = calcError(other, &d2, 0);
	dt = d1+d2;
	while(other && eaSize(&other->nodes))
	{
		// Steal point
		if(from)
		{
			eaRemove(&other->nodes, 0);
			eaPush(&line->nodes, cursor);
		}
		else
		{
			eaPop(&other->nodes);
			eaInsert(&line->nodes, cursor, 0);
		}
		if(cursor)
			cursor->line = line;

		error = calcError(line, NULL, 0);
		error_other = calcError(other, NULL, 0);

		if((error+error_other)-(min_error+min_error_other)>ACG_POINT_STEAL_BIAS/dt)
		{
			// undo and break
			if(from)
			{
				eaPop(&line->nodes);
				eaInsert(&other->nodes, cursor, 0);
			}
			else
			{
				eaRemove(&line->nodes, 0);
				eaPush(&other->nodes, cursor);
			}
			if(cursor)
				cursor->line = other;
			break;
		}

		min_error = error;
		min_error_other = error_other;
		if(from)
			cursor = eaHead(&other->nodes);
		else
			cursor = eaTail(&other->nodes);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLineStealPoints(AICivilianPathGenLine *line)
{
	AICivilianPathGenLine *next = acgGeneratorFindNeighborLine(eaTail(&line->nodes), NULL);
	AICivilianPathGenLine *prev = acgGeneratorFindNeighborLine(eaHead(&line->nodes), NULL);

	acgLineStealPointsFrom(line, next, 1);
	acgLineStealPointsFrom(line, prev, 0);
}

// ------------------------------------------------------------------------------------------------------------------
int cmpError(const AICivilianPathGenLine **left, const AICivilianPathGenLine **right)
{
	F32 error_left = 0;
	F32 error_right = 0;

	error_left = calcError(*left, NULL, 1);
	error_right = calcError(*right, NULL, 1);

	return error_left < error_right ? -1 : 1;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgProcessLinesMinimizeError(Entity *debugger)
{
	int i;
	if(!s_acgProcess.minError.sorted)
	{
		s_acgProcess.minError.sorted = 1;
		for(i=0; i<eaSize(&s_acgProcess.lineList); i++)
		{
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];
			int j;

			for(j=0; j<eaSize(&line->nodes); j++)
				line->nodes[j]->userFloat = 0;
		}
		eaQSort(s_acgProcess.lineList, cmpError);
		for(i=0; i<eaSize(&s_acgProcess.lineList); i++)
		{
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];
			int j;

			for(j=0; j<eaSize(&line->nodes); j++)
				line->nodes[j]->line = line;
		}
	}

	ANALYSIS_ASSUME(s_acgProcess.lineList);
	for(i=s_acgProcess.minError.i; i<eaSize(&s_acgProcess.lineList); i++)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];

		if(eaSize(&line->nodes)>0)
			acgLineStealPoints(line);
	}
	s_acgProcess.minError.i = i;

	if(i==eaSize(&s_acgProcess.lineList))
	{
		for(i=eaSize(&s_acgProcess.lineList)-1; i>=0; i--)
		{
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];

			if(eaSize(&line->nodes)==0)
			{
				eaRemoveFast(&s_acgProcess.lineList, i);
				acgDestroyPathGenLine(line);
			}
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
F32 calcLineDir(AICivilianPathGenLine *line, Vec3 dirOut)
{
	Vec3 start, end;

	acgGetPathGenNodePosition(eaHead(&line->nodes), start);
	acgGetPathGenNodePosition(eaTail(&line->nodes), end);

	subVec3(end, start, dirOut);
	return normalVec3(dirOut);
}

// ------------------------------------------------------------------------------------------------------------------
int acgNodeDist(AICivilianPathGenNode *start, AICivilianPathGenNode *end)
{
	Vec3 vstart, vend;
	acgGetPathGenNodePosition(start, vstart);
	acgGetPathGenNodePosition(end, vend);
	return distance3(vstart, vend);
}

// ------------------------------------------------------------------------------------------------------------------
int acgProcessLinesRemoveLowArea(Entity *debugger)
{
	int i;

	ANALYSIS_ASSUME(s_acgProcess.lineList);
	for(i=s_acgProcess.lowArea.i; i<eaSize(&s_acgProcess.lineList); i++)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];
		F32 len;
		Vec3 dir;

		len = calcLineDir(line, dir);

		if(len < 5)
		{
			// Will be caught by connections and ignored by pairs anyways
			int k;
			eaRemoveFast(&s_acgProcess.lineList, i);
			i--;

			for(k=0; k<eaSize(&line->nodes); k++)
			{
				AICivilianPathGenNode *node = line->nodes[k];
				acgDelSaveNode(node);
				acgGeneratorDestroyNode(node);
			}
		}
	}
	s_acgProcess.lowArea.i = i;

	if(i==eaSize(&s_acgProcess.lineList))
		return 1;

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
typedef int (*WalkLineCallback)(AICivilianPathGenNode *node, void *userdata);
static void acgWalkLineWorld(Vec3 start, Vec3 dir, F32 len, WalkLineCallback func, void *userdata)
{
	AICivilianPathGenNode *node;
	int xoff, zoff, xdir, zdir;
	int xmajor = 0;
	F32 error = 0;
	int nolen = 0;

	if(len<=0)
		nolen = 1;
	xmajor = fabs(dir[0])>fabs(dir[2]);

	xoff = 0;
	zoff = 0;

	xdir = dir[0]>0 ? 1 : -1;
	zdir = dir[2]>0 ? 1 : -1;

	node = acgGeneratorGetNodeByWorld(round(start[0])+xoff, round(start[2])+zoff, 0);

	if(node && func && func(node, userdata))
		return;

	while((len > 0 || nolen) && len > -5000)
	{
		len -= 1;
		if(xmajor)
		{
			xoff += xdir;
			error += xdir/dir[0]*zdir*dir[2];
		}
		else
		{
			zoff += zdir;
			error += zdir/dir[2]*xdir*dir[0];
		}

		node = acgGeneratorGetNodeByWorld(round(start[0])+xoff, round(start[2])+zoff, 0);

		if(node && func && func(node, userdata))
			return;

		if(error>=1)
		{
			len -= 0.414;
			if(xmajor)
			{
				error--;
				zoff += zdir;
			}
			else
			{
				error--;
				xoff += xdir;
			}

			node = acgGeneratorGetNodeByWorld(round(start[0])+xoff, round(start[2])+zoff, 0);

			if(node && func && func(node, userdata))
				return;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgWalkLine(AICivilianPathGenLine *line, WalkLineCallback func, void *userdata)
{
	int i;

	if(!func)
		return;

	for(i=0; i<eaSize(&line->nodes); i++)
	{
		AICivilianPathGenNode *node = line->nodes[i];

		if(func(node, userdata))
			return;
	}
}

// ------------------------------------------------------------------------------------------------------------------
typedef int (*acgIntersectLineIgnoreNode)(AICivilianPathGenNode *node, void *userdata);


typedef struct ACGFindPairIgnoreData {
	U32 same_type_count;
	AICivilianPathGenNode *baseNode;
	AICivilianPathGenNode **hitNodes;
	AICivilianPathGenLine *curLine;

} ACGFindPairIgnoreData;


// ------------------------------------------------------------------------------------------------------------------
// Just trying to find the line's perpendicular facing
static int aiFindOppIgnorePerp(AICivilianPathGenNode *node, ACGFindPairIgnoreData *data)
{
	if(!node)
		return 1;

	if(node->type == EAICivilianLegType_NONE)
		return 0;

	if(node->line==data->curLine)
		return 1;

	return 0;
}


// ------------------------------------------------------------------------------------------------------------------
static int aiFindOppIgnore(AICivilianPathGenNode *node, ACGFindPairIgnoreData *data)
{
	if(!node)
		return 1;

	if(node->line==data->curLine)
		return 1;

	if(eaSize(&data->hitNodes))
	{
		AICivilianPathGenNode *tail = eaTail(&data->hitNodes);
		ANALYSIS_ASSUME(tail);
		if(tail->type==node->type && acgNodeDist(tail, node)<2)  // Neighbors/sameline
			return 1;
	}

	// Save nodes for later processing
	eaPushUnique(&data->hitNodes, node);

	if (data->baseNode->type == node->type)
	{
		// count the number of times we are hitting the same type
		data->same_type_count++;
		if (data->same_type_count >= 3)
		{	// Only allow penetrating once
			return 0;
		}

		return 1;
	}
	else if (data->same_type_count == 0)
	{
		// hit a different type first
		return 0;
	}

	if(acgNodeDist(eaHead(&data->hitNodes), node) < 40+50)	// Allow some passing through
		return 1;

	return 0;
}


typedef struct ACGHitNodeData {
	AICivilianPathGenNode *hitNode;

	acgIntersectLineIgnoreNode ignoreFunc;
	void *ignoreData;
} ACGHitNodeData;

// ------------------------------------------------------------------------------------------------------------------
static int acgHitNode(AICivilianPathGenNode *node, ACGHitNodeData *hitNodeData)
{
	if(node && hitNodeData->ignoreFunc && hitNodeData->ignoreFunc(node, hitNodeData->ignoreData))
		return 0;

	hitNodeData->hitNode = node;

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathGenNode* acgIntersectLine(Vec3 start, Vec3 dir, F32 len, acgIntersectLineIgnoreNode func, void *userdata)
{
	ACGHitNodeData hitData = {0};
	hitData.ignoreFunc = func;
	hitData.ignoreData = userdata;
	acgWalkLineWorld(start, dir, len, acgHitNode, &hitData);

	return hitData.hitNode;
}




// ------------------------------------------------------------------------------------------------------------------
static bool acgDoesTypeGenerateMedians(EAICivilianLegType type)
{
	return EAICivilianLegType_CAR == type;
}

// ------------------------------------------------------------------------------------------------------------------
typedef struct ACGFindPairData {
	Vec3 perp;
	AICivilianPathGenLine **hitLines;
	AICivilianPathGenLine *line;
	AICivilianPathGenPair **pairs;
	F32 fMedianDist;
} ACGFindPairData;

#define ACG_MAX_MEDIAN_DIST 	15

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathGenNode* acgFindPerpNode(AICivilianPathGenNode *node, ACGFindPairData *pairData, ACGFindPairIgnoreData *ignore, F32 *fMedianDist)
{
	Vec3 start;
	int count;

	acgGetPathGenNodePosition(node, start);

	if(acg_d_pos)
	{
		if (distance3SquaredXZ(start, s_acgDebugPos)<SQR(2) || distance3SquaredXZ(start, s_acgDebugPos)<SQR(2))
		{
			int xxx = 0;
		}
	}

	ZeroStruct(ignore);
	ANALYSIS_ASSUME(ignore);

	ignore->baseNode = node;
	ignore->curLine = pairData->line;

	acgIntersectLine(start, pairData->perp, -1, aiFindOppIgnore, ignore);

	count = eaSize(&ignore->hitNodes);
	if(count)
	{
		AICivilianPathGenNode *tail, *head;

		tail = eaTail(&ignore->hitNodes);
		head = eaHead(&ignore->hitNodes);

		ANALYSIS_ASSUME(tail && head);

		if(head->type != node->type)
		{
			eaDestroy(&ignore->hitNodes);
			return NULL;
		}
		else if(tail->type != node->type)
		{
			eaDestroy(&ignore->hitNodes);
			return head;
		}
		else if (! acgDoesTypeGenerateMedians(node->type))
		{
			eaDestroy(&ignore->hitNodes);
			return head;
		}
		else
		{
			S32 i;

			if (count >= 2)
			{
				if (ignore->hitNodes[0]->type == ignore->hitNodes[1]->type)
				{
					// not allowing the first two nodes be the same type.
					eaDestroy(&ignore->hitNodes);
					return head;
				}
			}

			if (ignore->same_type_count != 3)
			{
				// we need at least three of the same type to consider a median
				eaDestroy(&ignore->hitNodes);
				return head;
			}

			/*
			// check the nodes in between for angle thresholds
			for(i = 0; i < count; i++)
			{
				AICivilianPathGenLine *line = ignore->hitNodes[i]->line;

				// checking angle between the hitnode & sourcenode's lines
				// if the angle is greater then (cos(45) ~= 0.7, 45 degrees), discard
				if(line && fabs(dotVec3(line->perp, node->line->perp)) < 0.7)
				{
					eaDestroy(&ignore->hitNodes);
					return head;
				}
			}
			*/

			// this is now being considered for a median
			{
				F32 medianDist;
				AICivilianPathGenNode *mid1 = NULL, *mid2 = NULL;

				// get the two median legs
				for(i = 0; i < count; i++)
				{
					AICivilianPathGenNode *genNode = ignore->hitNodes[i];
					if (genNode->type == node->type)
					{
						if (mid1 == NULL)
						{
							mid1 = genNode;
						}
						else
						{
							mid2 = genNode;
							break;
						}
					}

					// cannot create medians over these types
					if (genNode->type == EAICivilianLegType_PARKING || 
						genNode->type ==  EAICivilianLegType_INTERSECTION)
					{
						eaDestroy(&ignore->hitNodes);
						return head;
					}
				}

				medianDist = acgNodeDist(mid1, mid2);

				if (medianDist > 45) //ACG_MAX_MEDIAN_DIST)
				{	// the median is too large
					eaDestroy(&ignore->hitNodes);
					return head;
				}

				*fMedianDist = medianDist;

				return tail;
			}

		}

	}

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
static int acgProcessLinePerp(Entity *debugger)
{
	int i;
	F32 len;
	Vec3 dir, perp;
	Vec3 start, end;
	ACGFindPairIgnoreData ignore;

	for(i=eaSize(&s_acgProcess.lineList)-1; i>=0; i--)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];
		AICivilianPathGenNode *nodeHit = NULL;
		AICivilianPathGenNode *node = NULL;
		Vec3 node_pos;
		Vec3 temp;

		if(!eaSize(&line->nodes))
			continue;

#if 0
		acgGetPathGenNodePosition(eaHead(&line->nodes), start);
		acgGetPathGenNodePosition(eaTail(&line->nodes), end);
		subVec3(end, start, dir);
		len = normalVec3(dir);
#else
		len = acgLine_GetBestFitLine(line->nodes, NULL, start, end, dir);
#endif
		crossVec3Up(dir, perp);
		normalVec3(perp);

		if(acg_d_pos)
		{
			if (distance3SquaredXZ(s_acgDebugPos, start)<SQR(3) || distance3SquaredXZ(s_acgDebugPos, end)<SQR(3))
			{
				int xxx = 0;
			}
		}

		node = line->nodes[eaSize(&line->nodes)/2];

		ZeroStruct(&ignore);
		ignore.curLine = line;
		copyVec3(perp, temp);

		acgGetPathGenNodePosition(node, node_pos);
		nodeHit = acgIntersectLine(node_pos, temp, -1, aiFindOppIgnorePerp, &ignore);

		if(!nodeHit || nodeHit->type!=line->nodes[0]->type)
		{
			scaleVec3(temp, -1, temp);
			nodeHit = acgIntersectLine(node_pos, temp, -1, aiFindOppIgnorePerp, &ignore);
		}
		acgGetPathGenNodePosition(nodeHit, node_pos);
		
		copyVec3(temp, line->perp);
		/*
		if(vec3IsZero(line->perp))
		{
		acgDelSaveLine(s_acgProcess.line);
		acgDestroyPathGenLine(line);
		eaRemoveFast(&s_acgProcess.lineList, i);
		}
		*/
		//devassert(!vec3IsZero(line->perp));
	}

	return 1;
}





// ------------------------------------------------------------------------------------------------------------------
static int acgFindPairs2(AICivilianPathGenNode *pNode, ACGFindPairData *pairData)
{
	Vec3 vNodePos, vHitNodePos;
	AICivilianPathGenNode *pHitNode;
	ACGFindPairIgnoreData ignore = {0};
	AICivilianPathGenPair *pPair = NULL;
	F32 fDistSQR, maxLegWidth, fMedianDist = 0;
	
	
	pHitNode = acgFindPerpNode(pNode, pairData, &ignore, &fMedianDist);
	if (!pHitNode || pHitNode->type != pNode->type || !pHitNode->line)
		return 0;

	acgGetPathGenNodePosition(pNode, vNodePos);
	acgGetPathGenNodePosition(pHitNode, vHitNodePos);
	fDistSQR = distance3Squared(vNodePos, vHitNodePos);
	maxLegWidth = acgType_GetMaxLegWidth(pNode->type);

	// do not factor in the median dist into the max leg width
	if (sqrtf(fDistSQR) - fMedianDist > maxLegWidth)
		return 0;
	
	// check the angles between the lines
	if (acgMath_GetAngleBetweenNormsAbs(pNode->line->perp, pHitNode->line->perp) > RAD(10.0f))
		return 0;
	/*
	{
		F32 fAngle = dotVec3(pNode->line->perp, pHitNode->line->perp);
		fAngle = ABS(fAngle);
		fAngle = acosf(CLAMP(fAngle, -1.f, 1.f));
		if (ABS(fAngle) > RAD(5.f)) // 
			return 0;
	}
	*/
	
	pPair = eaTail(&pairData->pairs);
	if (! pPair || pPair->pair_line != pHitNode->line)
	{
		pPair = acgPairAlloc();

		pPair->minDist = FLT_MAX;
		pPair->pair_line = pHitNode->line;
		pPair->pair_start = pHitNode;
		pPair->self_start = pNode;
		eaPush(&pairData->pairs, pPair);
	}

	// for medians, don't let any of the internal lines generate a leg
	if(eaSize(&ignore.hitNodes))
	{
		S32 i;
		for(i = 0; i < eaSize(&ignore.hitNodes)-1; i++)
		{
			if(ignore.hitNodes[i]->line && ignore.hitNodes[i]->type== pHitNode->type)
			{
				eaPushUnique(&pairData->hitLines, ignore.hitNodes[i]->line);
				ignore.hitNodes[i]->line->touched = s_acgProcess.state;  // Don't let it generate a leg
			}
		}
		eaDestroy(&ignore.hitNodes);
	}

	MIN1(pPair->minDist, fDistSQR);
	MAX1(pPair->medianDist, fMedianDist);

	pPair->pair_end = pHitNode;
	pPair->self_end = pNode;
	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgGeneratorFindOpposite(AICivilianPathGenLine *line, ACGFindPairData *pairData)
{
	devassert(pairData);
	ZeroStruct(pairData);

	if(eaSize(&line->nodes)==0)
		return;

	pairData->line = line;
	copyVec3(line->perp, pairData->perp);
	acgWalkLine(line, acgFindPairs2, pairData);
}

// ------------------------------------------------------------------------------------------------------------------
int cmpDist(const AICivilianPathGenLine **left, const AICivilianPathGenLine **right)
{
	Vec3 start, end;
	F32 left_len, right_len;

	acgGetPathGenNodePosition(eaHead(&(*left)->nodes), start);
	acgGetPathGenNodePosition(eaTail(&(*left)->nodes), end);
	left_len = distance3Squared(start, end);

	acgGetPathGenNodePosition(eaHead(&(*right)->nodes), start);
	acgGetPathGenNodePosition(eaTail(&(*right)->nodes), end);
	right_len = distance3Squared(start, end);

	return left_len < right_len ? 1 : -1;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgCreatePairs(Entity *debugger)
{
	int i;
	if(!s_acgProcess.pairs.sorted)
	{
		eaQSort(s_acgProcess.lineList, cmpDist);
		s_acgProcess.pairs.sorted = 1;
	}

	ANALYSIS_ASSUME(s_acgProcess.lineList);
	for(i=s_acgProcess.pairs.i; i<eaSize(&s_acgProcess.lineList); i++)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];
		ACGFindPairData data = {0};

		// for debug breaking 
		{
			Vec3 line_s, line_e;

			acgGetPathGenNodePosition(eaHead(&line->nodes), line_s);
			acgGetPathGenNodePosition(eaTail(&line->nodes), line_e);

			if(distance3SquaredXZ(line_s, s_acgDebugPos)<SQR(1) || distance3SquaredXZ(line_e, s_acgDebugPos)<SQR(1))
				printf("");
		}
		
		acgGeneratorFindOpposite(line, &data);
		copyVec3(data.perp, line->perp);

		eaPushEArray(&line->pairs, &data.pairs);

		if(acg_d_pairs)
		{
			int j;
			for(j=eaSize(&line->pairs)-1; j>=0; j--)
			{
				Vec3 start, end;
				AICivilianPathGenPair *pair = line->pairs[j];

				acgGetPathGenNodePosition(pair->self_start, start);
				acgGetPathGenNodePosition(pair->pair_start, end);
				wlAddClientLine(debugger, start, end, 0xFF0000FF);

				acgGetPathGenNodePosition(pair->self_end, start);
				acgGetPathGenNodePosition(pair->pair_end, end);
				wlAddClientLine(debugger, start, end, 0xFFFF0000);
			}
		}

		eaDestroy(&data.pairs);
	}
	s_acgProcess.pairs.i = i;

	if(i==eaSize(&s_acgProcess.lineList))
	{
		for(i=eaSize(&s_acgProcess.lineList)-1; i>=0; i--)
		{
			int j;
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];

			for(j=eaSize(&line->pairs)-1; j>=0; j--)
			{
				AICivilianPathGenPair *pair = line->pairs[j];

				if(pair->pair_line->touched==s_acgProcess.state)  // No penetrated lines can be used to make legs
				{
					eaRemoveFast(&line->pairs, j);
					acgPairFree(pair);
				}
			}
		}
		for(i=eaSize(&s_acgProcess.lineList)-1; i>=0; i--)
		{
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];

			if(line->touched==s_acgProcess.state)
			{
				acgDelSaveLine(line);
				eaRemoveFast(&s_acgProcess.lineList, i);
				acgDestroyPathGenLine(line);
			}
		}

		return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static F32 calcLegDir(const AICivilianPathLeg *leg, Vec3 dirOut, Vec3 perp)
{
	F32 len;
	subVec3(leg->end, leg->start, dirOut);
	len = normalVec3(dirOut);

	if(perp)
	{
		crossVec3Up(dirOut, perp);
		normalVec3(perp);
	}

	return len;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_RecalculateLegDir(AICivilianPathLeg *leg)
{
	leg->len = calcLegDir(leg, leg->dir, leg->perp);
}

// ------------------------------------------------------------------------------------------------------------------
#define MOCK_LEG_HEIGHT 2.0f
__forceinline static void acgLegToOBB(const AICivilianPathLeg *leg, Vec3 legMin, Vec3 legMax, Mat4 mtxLeg)
{
	F32 hwidth;

	hwidth = leg->width * 0.5f;
	setVec3(legMin, -hwidth, -MOCK_LEG_HEIGHT, 0.0f);
	setVec3(legMax, hwidth, MOCK_LEG_HEIGHT, leg->len);

	//identityMat4(mtxLeg); 
	orientMat3(mtxLeg, leg->dir);
	copyVec3(leg->start, mtxLeg[3]);
}


// ------------------------------------------------------------------------------------------------------------------
static void legToQuad(AICivilianPathLeg *leg, Vec3 p1, Vec3 p2, Vec3 p3, Vec3 p4)
{
	scaleAddVec3(leg->perp, leg->width/2, leg->start, p1);
	scaleAddVec3(leg->perp, leg->width/2, leg->end, p2);
	scaleAddVec3(leg->perp, -leg->width/2, leg->end, p3);
	scaleAddVec3(leg->perp, -leg->width/2, leg->start, p4);

	p1[1] += 3;
	p2[1] += 3;
	p3[1] += 3;
	p4[1] += 3;
}

// ------------------------------------------------------------------------------------------------------------------
static void legToLine(AICivilianPathLeg *leg, Vec3 p1, Vec3 p2)
{
	copyVec3(leg->start, p1);
	copyVec3(leg->end, p2);

	//p1[1] += 3.1;
	//p2[1] += 3.1;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool lineSegBoxCollision(const Vec3 start, const Vec3 end, const Vec3 min, const Vec3 max, Vec3 intersect )
{
	if (lineBoxCollision(start, end, min, max, intersect))
	{
		// check to see if the intersection point is on the line
		Vec3 vDir1, vDir2;

		subVec3(intersect, start, vDir1);
		subVec3(intersect, end, vDir2);

		if (dotVec3(vDir1, vDir2) <= 0.0f)
			return true;

		// the line segment may be completely in the box.
		return pointBoxCollision(start, min, max) == 1 && pointBoxCollision(end, min, max) == 1;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------------
static bool acgSnapPosToGround(int iPartitionIdx, Vec3 posInOut, F32 height, F32 depth)
{
	WorldCollCollideResults results = {0};
	Vec3 vWorldPosUp, vWorldPosDown;
	bool bHit;
	int tries = 1;

	copyVec3(posInOut, vWorldPosUp);
	copyVec3(posInOut, vWorldPosDown);
	vWorldPosUp[1] += height;
	vWorldPosDown[1] += depth;

	do {

		bHit = wcRayCollide(worldGetActiveColl(iPartitionIdx), vWorldPosUp, vWorldPosDown, WC_QUERY_BITS_AI_CIV, &results);
		if (bHit)
		{
			copyVec3(results.posWorldImpact, posInOut);
			return true;
		}

		if(results.errorFlags.noScene)
		{
			if(psdkSceneLimitReached())
				return false;

			// force an update and try again.
			wcForceSimulationUpdate();
			continue;
		}

		if(results.errorFlags.noCell)
			return false;

	} while(tries--);

	return false;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static void acgCastRay(int iPartitionIdx, WorldCollCollideResults *results, const Vec3 vStart, const Vec3 vEnd)
{
	S32 sanity = 1;

	do{
		wcRayCollide(worldGetActiveColl(iPartitionIdx), vStart, vEnd, WC_QUERY_BITS_AI_CIV, results);

		// in case the raycast fails...
		if(results->errorFlags.noScene)
		{
			if(psdkSceneLimitReached())
				continue;
			wcForceSimulationUpdate();
			continue;
		}
		if(results->errorFlags.noCell)
			continue;

		return;

	} while(sanity--);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void acgUtil_CastVerticalRay(int iPartitionIdx, WorldCollCollideResults *results, const Vec3 vCastPos, F32 fCastUpDist, F32 fCastDownDist)
{
	Vec3 vCastPosUp, vCastPosDown;

	copyVec3(vCastPos, vCastPosUp);
	copyVec3(vCastPos, vCastPosDown);
	vCastPosUp[1] += fCastUpDist;
	vCastPosDown[1] -= fCastDownDist;

	acgCastRay(iPartitionIdx, results, vCastPosUp, vCastPosDown);
}


// ------------------------------------------------------------------------------------------------------------------
// From the start to end position, returns true if the line intersects with any legs
static int acgTestRayVsLegs(EAICivilianLegType type, const Vec3 vStart, const Vec3 vEnd, const AICivilianPathLeg *leg_ignore)
{
	S32 i;
	for (i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		Vec4 v4Plane;
		Vec3 vToStart;
		F32 fDot;
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		if ((type != EAICivilianLegType_NONE && leg->type != type) || leg == leg_ignore)
			continue;

		// check if it is within the leg's bounds
		subVec3(vStart, leg->start, vToStart);
		fDot = dotVec3(vToStart, leg->dir);
		if (fDot >= 0.0f && fDot <= leg->len)
		{
			fDot = dotVec3(vToStart, leg->perp);
			if (ABS(fDot) < (leg->width * 0.5f))
			{
				// check the segment vs the plane.

				// note: the plane normal should be pre-calculated.
				//	check calcLegDir, and fix it up so it also generates the normal.
				crossVec3(leg->dir, leg->perp, v4Plane);
				normalVec3(v4Plane);
				v4Plane[3] = dotVec3(v4Plane, leg->start);
				if (intersectPlane(vStart, vEnd, v4Plane, vToStart))
					return true;
			}
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgUtil_CastVerticalVsLegs(EAICivilianLegType type, const Vec3 vCastPos, const AICivilianPathLeg *leg_ignore)
{
	Vec3 vCastPosUp, vCastPosDown;

	copyVec3(vCastPos, vCastPosUp);
	copyVec3(vCastPos, vCastPosDown);
	vCastPosUp[1] += 5.0f;
	vCastPosDown[1] -= 10.f;

	return acgTestRayVsLegs(type, vCastPosUp, vCastPosDown, leg_ignore);
}

// ------------------------------------------------------------------------------------------------------------------
static int acgPairDist(const AICivilianPathGenPair **left, const AICivilianPathGenPair **right)
{
	return (*left)->minDist<(*right)->minDist ? -1 : 1;
}

static __forceinline int acgDebug_LegNearDebugPos(const AICivilianPathLeg *leg)
{
	return distance3SquaredXZ(s_acgDebugPos, leg->start)<SQR(30) ||
		distance3SquaredXZ(s_acgDebugPos, leg->end)<SQR(30);

}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_ReverseLeg(AICivilianPathLeg *leg)
{
	Vec3 vTmp;
	AICivilianPathLeg *tmpLeg;
	AICivilianPathIntersection *tmpAcpi;

	// swap the leg start / end

	copyVec3(leg->start, vTmp);
	copyVec3(leg->end, leg->start);
	copyVec3(vTmp, leg->end);

	// calcLegDir(leg, leg->dir, leg->perp);
	acgLeg_RecalculateLegDir(leg);

	leg->bSkewed_Start = false;
	leg->fSkewedAngle_Start = 0.f;
	leg->fSkewedLength_Start = 0.f;

	// swap the next/prev
	tmpLeg = leg->next;
	leg->next = leg->prev;
	leg->prev = tmpLeg;

	tmpAcpi = leg->nextInt;
	leg->nextInt = leg->prevInt;
	leg->prevInt = tmpAcpi;
}

// ------------------------------------------------------------------------------------------------------------------
static const F32* acgLeg_GetStartEndPosAndDir(const AICivilianPathLeg *leg, int bStartPos, F32 *pfDirMod)
{
	if (bStartPos)
	{
		if (pfDirMod)
			*pfDirMod = -1.f;
		return leg->start;
	}
	else
	{
		if (pfDirMod)
			*pfDirMod = 1.f;
		return leg->end;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_ExtendLegXFeet(AICivilianPathLeg *leg, F32 fDist, bool bStartPos) 
{
	if (bStartPos)
	{
		scaleAddVec3(leg->dir, -fDist, leg->start, leg->start);
	}
	else
	{
		scaleAddVec3(leg->dir, fDist, leg->end, leg->end);
	}
	acgLeg_RecalculateLegDir(leg);
}

static void acgLeg_FindGroundSnapPos(int iPartitionIdx, AICivilianPathLeg *leg)
{
	if (leg->median_width == 0.f)
	{
		acgSnapPosToGround(iPartitionIdx, leg->start, 15, -200);
		acgSnapPosToGround(iPartitionIdx, leg->end, 15, -200);
	}
	else
	{
		F32 fLanePos = (leg->median_width * .5f) + 10.f;
		Vec3 vPt1, vPt2;

		// start pt
		scaleAddVec3(leg->perp, fLanePos, leg->start, vPt1);
		acgSnapPosToGround(iPartitionIdx, vPt1, 15, -200);

		scaleAddVec3(leg->perp, -fLanePos, leg->start, vPt2);
		acgSnapPosToGround(iPartitionIdx, vPt2, 15, -200);
		leg->start[1] = interpF32(.5f, vPt1[1], vPt2[1]) + 1.f;

		// end pt
		scaleAddVec3(leg->perp, fLanePos, leg->end, vPt1);
		acgSnapPosToGround(iPartitionIdx, vPt1, 15, -200);

		scaleAddVec3(leg->perp, -fLanePos, leg->end, vPt2);
		acgSnapPosToGround(iPartitionIdx, vPt2, 15, -200);
		leg->end[1] = interpF32(.5f, vPt1[1], vPt2[1]) + 1.f;
	}
	
}

// ------------------------------------------------------------------------------------------------------------------
static int acgLeg_CreateLegsFromPairs(int iPartitionIdx, Entity *debugger)
{
	S32 i; 

	for (i = 0; i < eaSize(&s_acgProcess.lineList); i++)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];
		EAICivilianType eType;
		S32 x;

		if (! eaSize(&line->nodes))
			continue;
		
		eType = eaHead(&line->nodes)->type;
		if (!acgType_DoesTypeCreateLegs(eType))
			continue;


		for(x = 0; x < eaSize(&line->pairs); x++)
		{
			AICivilianPathGenPair *pair = line->pairs[x];
			Vec3 vl1_st, vl1_end, vl2_st, vl2_end;
		
			acgGetPathGenNodePosition(pair->self_start, vl1_st);
			acgGetPathGenNodePosition(pair->self_end, vl1_end);
			
			acgGetPathGenNodePosition(pair->pair_start, vl2_st);
			acgGetPathGenNodePosition(pair->pair_end, vl2_end);

			if (distance3SquaredXZ(vl1_st, vl1_end) <= 6)
				continue;
			
			{
				AICivilianPathLeg *leg;
				F32 fWidth1, fWidth2;

				fWidth1 = distance3(vl1_st, vl2_st);
				fWidth2 = distance3(vl1_end, vl2_end);
				
				if(MAX(fWidth1, fWidth2) < 4.f)
					continue; // width is too low

				leg = acgPathLeg_Alloc();
				
				leg->type = eType;

				
				leg->width = MAX(fWidth1, fWidth2);
				leg->median_width = pair->medianDist;
				
				{
					Vec3 vLineMid1, vLineMid2, vMid;
					//Vec3 vLineDir1, vLineDir2;
					Vec3 vDir;
					F32 fLen1, fLen2, fLen;

					interpVec3(0.5f,  vl1_st, vl1_end, vLineMid1);
					interpVec3(0.5f,  vl2_st, vl2_end, vLineMid2);
					interpVec3(0.5f,  vLineMid1, vLineMid2, vMid);

					//subVec3(vl1_end, vl1_st, vLineDir1);
					//subVec3(vl2_end, vl2_st, vLineDir2);
					// addVec3(vLineDir1, vLineDir2, vDir);
					{
						AICivilianPathGenLine *pBestLine = NULL;
						Vec3 vLineHead, vLineTail;

						assert (pair->self_start->line);
						if (eaSize(&pair->self_start->line->nodes) > eaSize(&pair->pair_line->nodes))
						{
							pBestLine = pair->self_start->line;
						}
						else
						{
							pBestLine = pair->pair_line;
						}
						
#if 0
						acgGetPathGenNodePosition(eaHead(&pBestLine->nodes), vLineHead);
						acgGetPathGenNodePosition(eaTail(&pBestLine->nodes), vLineTail);
						subVec3(vLineTail, vLineHead, vDir);
						normalVec3(vDir);
#else
						acgLine_GetBestFitLine(pBestLine->nodes, NULL, vLineHead, vLineTail, vDir);
#endif
					}
					

					fLen1 = distance3(vl1_st, vl1_end);
					fLen2 = distance3(vl2_st, vl2_end);

					fLen = MAX(fLen1, fLen2);
					
					scaleAddVec3(vDir, fLen * .5f, vMid, leg->start);
					scaleAddVec3(vDir, -fLen * .5f, vMid, leg->end);
				}
							
				
				leg->len = calcLegDir(leg, leg->dir, leg->perp);

				//interpVec3(0.5f,  vl1_st, vl2_st, leg->start);
				//interpVec3(0.5f,  vl1_end, vl2_end, leg->end);
				acgLeg_FindGroundSnapPos(iPartitionIdx, leg);
				//acgSnapPosToGround(leg->start, 15, -200);
				//acgSnapPosToGround(leg->end, 15, -200);
				//if (leg->median_width > 0.f)
				{
					
				}
				
				leg->len = calcLegDir(leg, leg->dir, leg->perp);
				leg->normal[0] = acgMath_GetVec3Yaw(leg->dir);
				leg->normal[1] = leg->normal[0];

				if (acg_d_pos)
				{
					if (acgDebug_LegNearDebugPos(leg))
					{	
						S32 bbb = 0;
					}

				}

				eaPush(&s_acgProcess.legList, leg);
			}

		}
	}
	
	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
static S32 acgFindPathLegInACPI(const AICivilianPathIntersection *acpi, const AICivilianPathLeg *leg)
{
	S32 i;
	for (i = eaSize(&acpi->legIntersects) - 1; i >= 0; i--)
	{
		if(acpi->legIntersects[i]->leg == leg)
			return i;
	}

	return -1;
}


// ------------------------------------------------------------------------------------------------------------------
// this function may delete the acpi if it is orphaned
static bool acgRemoveLegFromIntersection(const AICivilianPathLeg *remLeg, AICivilianPathLeg *src, AICivilianPathIntersection **pacpi)
{
	S32 idx;
	AICivilianPathIntersection *acpi = *pacpi;

	devassert(src->nextInt == acpi || src->prevInt == acpi);

	idx = acgFindPathLegInACPI(acpi, remLeg);
	if (idx != -1)
	{
		S32 count;
		PathLegIntersect *pli = acpi->legIntersects[idx];
		// delete and remove this pli
		eaRemoveFast(&acpi->legIntersects, idx);
		acgPathLegIntersect_Free(pli);
		
		count = eaSize(&acpi->legIntersects);
		// if the acpi only has one other pli, delete the acpi
		if (count <= 1)
		{
			idx = acgFindPathLegInACPI(acpi, src);
			if (count == 0 || idx != -1)
			{
				// this ACPI only has one PLI and it is the leg itself
				// we can delete it
				if (src->prevInt == acpi)
				{
					src->prevInt = NULL;
				}
				else if (src->nextInt == acpi)
				{
					src->nextInt = NULL;
				}
				else
				{	
					S32 tmp;
					devassert(acpi->bIsMidIntersection);
					tmp = eaFindAndRemoveFast(&src->midInts, acpi);
					devassert(idx != -1);
				}

				eaFindAndRemoveFast(&s_acgPathInfo.intersects, acpi);
				*pacpi = NULL;
#if defined (LEG_DESTROY_PARANOID)
				{
					S32 x;
					for (x = 0; x < eaSize(&s_acgProcess.legList); x++)
					{
						AICivilianPathLeg *leg = s_acgProcess.legList[x];

						if (leg != remLeg)
						{
							devassert(leg->prevInt != acpi);
							devassert(leg->nextInt != acpi);
							devassert(eaFind(&leg->midInts, acpi) == -1);
						}
						
					}
				}
#endif
				acgPathIntersection_Free(acpi);
				
			}
		}

		return true;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------------
static void acgRemoveIntersectionLinksToLeg(AICivilianPathIntersection *acpi, const AICivilianPathLeg *remLeg)
{
	S32 x;

	if (acpi->bIsMidIntersection)
	{
		AICivilianPathLeg *leg;
		S32 idx;

		leg = (acpi->legIntersects[0]->leg != remLeg) ? acpi->legIntersects[0]->leg : acpi->legIntersects[1]->leg;

		idx = eaFind(&leg->midInts, acpi);
		devassert(idx != -1);

		// remove this mid intersection
		eaRemove(&leg->midInts, idx);

		// destroy the intersection
		eaFindAndRemoveFast(&s_acgPathInfo.intersects, acpi);
#if defined (LEG_DESTROY_PARANOID)
		{
			S32 i;
			for (i = 0; i < eaSize(&s_acgProcess.legList); i++)
			{
				AICivilianPathLeg *other_leg = s_acgProcess.legList[i];

				if (remLeg != other_leg)
				{
					devassert(other_leg->prevInt != acpi);
					devassert(other_leg->nextInt != acpi);
					devassert(eaFind(&other_leg->midInts, acpi) == -1);
				}
			}
		}
#endif
		acgPathIntersection_Free(acpi);
	}
	else
	{
		for (x = 0; x < eaSize(&acpi->legIntersects); x++)
		{
			AICivilianPathLeg *leg = acpi->legIntersects[x]->leg;

			if (leg == remLeg)
				continue;

			if (leg->prev == remLeg)
			{
				leg->prev = NULL;
			}
			else if (leg->next == remLeg)
			{
				leg->next = NULL;
			}
			else
			{
				if (leg->nextInt == acpi)
				{
					// check if the leg is attached to me by an intersection.
					if (acgRemoveLegFromIntersection(remLeg, leg, &leg->nextInt))
					{
						if (!leg->nextInt)
							return;
						x = -1;
					}
				}

				if (leg->prevInt == acpi)
				{
					// check if the leg is attached to me by an intersection.
					if (acgRemoveLegFromIntersection(remLeg, leg, &leg->prevInt))
					{
						if (!leg->prevInt)
							return;
						x = -1;
					}
				}
			}
		}

#if defined (LEG_DESTROY_PARANOID)
		{
		    S32 idx;
		    idx = acgFindPathLegInACPI(acpi, remLeg);
		    if (idx != -1)
		    {
			    int xxx = 0;
		    }
		}
#endif
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgDestroyLeg(AICivilianPathLeg **ppLeg, bool bFreeLeg)
{
	AICivilianPathLeg *remLeg = *ppLeg;

	if (remLeg)
	{
		// check the connections and make sure we remove all connections to this leg
		if (remLeg->next)
		{
			AICivilianPathLeg *nextLeg = remLeg->next;
			if (nextLeg->prev == remLeg)
			{
				nextLeg->prev = NULL;
			}
			else if (nextLeg->next == remLeg)
			{
				nextLeg->next = NULL;
			}
			else
			{
				// check if the leg is attached to me by an intersection.
				if (nextLeg->prevInt)
				{
					acgRemoveLegFromIntersection(remLeg, nextLeg, &nextLeg->prevInt);
				}
				if (nextLeg->nextInt)
				{
					acgRemoveLegFromIntersection(remLeg, nextLeg, &nextLeg->nextInt);
				}

			}
		}

		if (remLeg->prev)
		{
			AICivilianPathLeg *prevLeg = remLeg->prev;
			if (prevLeg->prev == remLeg)
			{
				prevLeg->prev = NULL;
			}
			else if (prevLeg->next == remLeg)
			{
				prevLeg->next = NULL;
			}
			else
			{
				// check if the leg is attached to me by an intersection.
				if (prevLeg->prevInt)
				{
					acgRemoveLegFromIntersection(remLeg, prevLeg, &prevLeg->prevInt);
				}
				if (prevLeg->nextInt)
				{
					acgRemoveLegFromIntersection(remLeg, prevLeg, &prevLeg->nextInt);
				}
			}
		}

		if (remLeg->nextInt)
		{
			acgRemoveIntersectionLinksToLeg(remLeg->nextInt, remLeg);
		}

		if (remLeg->prevInt)
		{
			acgRemoveIntersectionLinksToLeg(remLeg->prevInt, remLeg);
		}

		// delete all the mids
		if (remLeg->midInts)
		{
			S32 x;

			for (x = 0; x < eaSize(&remLeg->midInts); x++)
			{
				S32 i;
				AICivilianPathIntersection *acpi = remLeg->midInts[x];

				for (i = 0; i < eaSize(&acpi->legIntersects); i++)
				{
					PathLegIntersect *pli = acpi->legIntersects[i];

					if (pli->leg != remLeg)
					{
						if (acpi == pli->leg->nextInt)
						{
							pli->leg->nextInt = NULL;
						}
						else
						{
							devassert(acpi == pli->leg->prevInt);
							pli->leg->prevInt = NULL;
						}
						break;
					}
				}

#if defined (LEG_DESTROY_PARANOID)
				{
					for (i = 0; i < eaSize(&s_acgProcess.legList); i++)
					{
						AICivilianPathLeg *other_leg = s_acgProcess.legList[i];
	
						if (other_leg != remLeg)
						{
							devassert(other_leg->prevInt != acpi);
							devassert(other_leg->nextInt != acpi);
							devassert(eaFind(&other_leg->midInts, acpi) == -1);
						}
					}
				}
#endif

				eaFindAndRemoveFast(&s_acgPathInfo.intersects, acpi);
				acgPathIntersection_Free(acpi);
			}

			eaClear(&remLeg->midInts);
		}


//PARANOID
#if defined(LEG_DESTROY_PARANOID)
		// go through all the legs in the whole world and make sure there's nothing still referencing this leg
		//
		{
			S32 x;
			for (x = 0; x < eaSize(&s_acgProcess.legList); x++)
			{
				AICivilianPathLeg *leg = s_acgProcess.legList[x];
				S32 i;

				if (leg == remLeg)
					continue;

				devassert(leg->next != remLeg);
				devassert(leg->prev != remLeg);
				if (leg->prevInt)
				{
					devassert(acgFindPathLegInACPI(leg->prevInt, remLeg) == -1);
				}
				if (leg->nextInt)
				{
					devassert(acgFindPathLegInACPI(leg->nextInt, remLeg) == -1);
				}

				for (i = 0; i < eaSize(&leg->midInts); i++)
				{
					AICivilianPathIntersection *acpi = leg->midInts[i];
					devassert(acgFindPathLegInACPI(acpi, remLeg) == -1);
				}
			}
		}
#endif
//

		remLeg->next = NULL;
		remLeg->prev = NULL;
		remLeg->nextInt = NULL;
		remLeg->prevInt = NULL;
		eaDestroy(&remLeg->midInts);

		if (bFreeLeg)
		{
			acgPathLeg_Free(remLeg);
			*ppLeg = NULL;
		}
	}



}

// ------------------------------------------------------------------------------------------------------------------
F32 acgPointLegDistSquared(const Vec3 pt, const AICivilianPathLeg *leg, Vec3 leg_pt)
{
	return PointLineDistSquared(pt, leg->start, leg->dir, leg->len, leg_pt);
}

// ------------------------------------------------------------------------------------------------------------------
F32 acgLegLegDistSquared(const AICivilianPathLeg *leg, Vec3 leg_pt, const AICivilianPathLeg *other, Vec3 other_pt)
{
	return LineLineDistSquared(leg->start, leg->dir, leg->len, leg_pt, other->start, other->dir, other->len, other_pt);
}

// ------------------------------------------------------------------------------------------------------------------
static F32 acgLegLegDistSquaredXZ(const AICivilianPathLeg *leg, Vec3 leg_pt, const AICivilianPathLeg *other)
{
	F32 dist = LineLineDistSquaredXZ(leg->start, leg->end, leg_pt, other->start, other->end);
	if(leg_pt)
		leg_pt[1] = interpF32(distance3XZ(leg_pt, leg->start)/leg->len, leg->start[1], leg->end[1]);

	return dist;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgFindNearbyLegs(Vec3 pt, AICivilianPathLeg ***legsOut, F32 dist)
{
	int i;
	
	for(i=0; i<ARRAY_SIZE(s_acgPathInfo.legs); i++)
	{
		int j;
		for(j=0; j<eaSize(&s_acgPathInfo.legs[i]); j++)
		{
			AICivilianPathLeg *leg = s_acgPathInfo.legs[i][j];

			if(PointLineDistSquared(pt, leg->start, leg->dir, leg->len, NULL) < SQR(dist))
			{
				eaPush(legsOut, leg);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathLegFindNearbyLegs(AICivilianPathLeg *leg, AICivilianPathLeg ***legsOut, AICivilianPathLeg ***startOut, AICivilianPathLeg ***endOut, AICivilianPathLeg ***midOut, F32 dist)
{
	int i;

	for(i=0; i<eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *other_leg = s_acgProcess.legList[i];
		Vec3 leg_coll;

		if(other_leg->type != leg->type || other_leg->deleted)
			continue;
		if(other_leg == leg)
			continue;

		if(acgLegLegDistSquared(leg, leg_coll, other_leg, NULL) < SQR(dist))
		{
			if(legsOut)
				eaPush(legsOut, other_leg);
			else
			{
				if(distance3(leg_coll, leg->start)<SQR(1))
				{
					if(startOut)
						eaPush(startOut, other_leg);
				}
				else if(distance3(leg_coll, leg->end)<SQR(1))
				{
					if(endOut)
						eaPush(endOut, other_leg);
				}
				else if(midOut)
					eaPush(midOut, other_leg);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathLeg* acgPathFindNearestLeg(const Vec3 vPos, EAICivilianLegType type)
{
	int i;
	AICivilianPathLeg* pBestLeg = NULL;
	F32 fBestDistSq = FLT_MAX;

	for(i=0; i<eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *pLeg = s_acgProcess.legList[i];
		F32 fDistSq;
		
		if (type != EAICivilianLegType_COUNT && pLeg->type != type)
			continue;
		
		fDistSq = acgPointLegDistSquared(vPos, pLeg, NULL);

		if (fDistSq < fBestDistSq)
		{
			fBestDistSq = fDistSq;
			pBestLeg = pLeg;
		}
	}

	return pBestLeg;
}

// ------------------------------------------------------------------------------------------------------------------
int cmpLeg(const AICivilianPathLeg **left, const AICivilianPathLeg **right)
{
	return (*left)->len<(*right)->len ? -1 : 1;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLegMerge(AICivilianPathLeg *leg, const AICivilianPathLeg *other)
{
	Vec3 vLegMid, vOtherMid, vLegToOther;
	Vec3 vOldDir;
	// find out which leg is in front of the other one.

	// get the leg midpoints, then get the direction from the leg to the other leg
	interpVec3(0.5f, leg->start, leg->end, vLegMid);
	interpVec3(0.5f, other->start, other->end, vOtherMid);
	subVec3(vOtherMid, vLegMid, vLegToOther);

	copyVec3(leg->dir, vOldDir);

	if (dotVec3(leg->dir, other->dir) > 0.0f)
	{
		// legs going in the same direction
		if (dotVec3(vLegToOther, leg->dir) < 0.0f)
		{	// behind
			copyVec3(other->start, leg->start);
		}
		else
		{	// in front
			copyVec3(other->end, leg->end);
		}
	}
	else
	{
		// legs going in opposite directions

		if (dotVec3(vLegToOther, leg->dir) < 0.0f)
		{	// behind
			copyVec3(other->end, leg->start);
		}
		else
		{	// in front
			copyVec3(other->start, leg->end);
		}
	}

	leg->len = calcLegDir(leg, leg->dir, leg->perp);
	leg->width = MAX(leg->width, other->width);

	{
		F32 err1, err2; 

		leg->normal[2] = MAX(leg->normal[2], other->normal[2]);
		
		err1 = acgMath_GetAngleBetweenNormsAbs(leg->dir, vOldDir);
		err2 = acgMath_GetAngleBetweenNormsAbs(other->dir, vOldDir);

		leg->normal[2] += MAX(err1, err2);
	}
	

	leg->normal[0] = MIN(leg->normal[0], other->normal[0]);
	leg->normal[1] = MAX(leg->normal[1], other->normal[1]);

}

static void acgLegMergeDupes(AICivilianPathLeg *leg, const AICivilianPathLeg *other)
{
	F32 fDist1SQR, fDist2SQR;

	fDist1SQR = distance3Squared(leg->start, other->start);
	fDist2SQR = distance3Squared(leg->end, other->start);
	if (fDist1SQR < fDist2SQR)
	{// starts match
		interpVec3(.5f, leg->start, other->start, leg->start);
		interpVec3(.5f, leg->end, other->end, leg->end);
	}
	else 
	{// ends match
		interpVec3(.5f, leg->start, other->end, leg->start);
		interpVec3(.5f, leg->end, other->start, leg->end);
	}

	leg->len = calcLegDir(leg, leg->dir, leg->perp);
	leg->width = MAX(leg->width, other->width);
}

// ------------------------------------------------------------------------------------------------------------------
// loops over the legs in the acgProcess list and removes the ones that were marked as deleteed


static void acgProcess_RemoveDeletedLegsEx(bool bPrintDeletedLegs)
{
	S32 i, count = 0;
	bool bDeleted = false;
	for(i=eaSize(&s_acgProcess.legList)-1; i>=0; i--)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if(leg->deleted)
		{
			if (bPrintDeletedLegs)
			{
				count++;
				if (!bDeleted)
				{
					printf("\n\n    Deleting legs:");
					bDeleted = true;
				}

				printf("\n\t#%d. Start Position (%.0f, %.0f, %.0f)"
						"\n\t\tReason: %s ",
						count,
						leg->start[0], leg->start[1], leg->start[2],
						(leg->deleteReason ? leg->deleteReason : "No Reason Given"));
			}
			eaRemoveFast(&s_acgProcess.legList, i);
			eaPush(&s_acgProcess.deletedLegs, leg);
		}
	}

}
#define acgProcess_RemoveDeletedLegs()	acgProcess_RemoveDeletedLegsEx(false)

static void acgFreeDeletedLegsFromList()
{
	S32 i;
	for (i = eaSize(&s_acgProcess.legList) - 1; i >= 0; i--)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		if (leg->deleted)
		{
			eaRemoveFast(&s_acgProcess.legList, i);
			acgPathLeg_Free(leg);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
// Merges legs that are both starting and ending on the same point
#define DUPE_END_THRESHOLD	1.5f
static void acgLeg_MergeDupes(Entity *debugger)
{
	S32 i, x;

	for (i = eaSize(&s_acgProcess.legList) - 1; i >= 0; i--)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->deleted)
			continue;


		for (x = i - 1; x >= 0; x--)
		{
			AICivilianPathLeg *other_leg = s_acgProcess.legList[x];
			F32 fDistSQR1, fDistSQR2;

			if (other_leg->deleted)
				continue;
			if (other_leg->type != leg->type)
				continue;
			
			fDistSQR1 = distance3SquaredXZ(leg->start, other_leg->start);
			fDistSQR2 = distance3SquaredXZ(leg->end, other_leg->start);
			if (MIN(fDistSQR1, fDistSQR2) > SQR(DUPE_END_THRESHOLD))
			{
				continue;
			}

			fDistSQR1 = distance3SquaredXZ(leg->start, other_leg->end);
			fDistSQR2 = distance3SquaredXZ(leg->end, other_leg->end);
			if (MIN(fDistSQR1, fDistSQR2) > SQR(DUPE_END_THRESHOLD))
			{
				continue;
			}

			// these legs are right on top of each other, merge them
			acgLegMergeDupes(leg, other_leg);
			other_leg->deleted = s_acgProcess.state;
		}
	}

	acgFreeDeletedLegsFromList();
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgLeg_ShouldOverlappingLegsMerge(const AICivilianPathLeg *leg, const AICivilianPathLeg *other)
{
	// aligned, similar width, and have a point within each other

	if (acgMath_GetAngleBetweenNormsAbs(leg->dir, other->dir) > RAD(10.f))
		return false;

	{
		Vec3 vDir;
		F32 fDist;

		subVec3(other->start, leg->start, vDir);
		fDist = dotVec3(vDir, leg->perp);
		if (ABS(fDist) > 10.f)
			return false;
	}

	if (fabsf(leg->width - other->width) > 10.f)
		return false;

	return (acgPointInLeg(leg->start, other) || acgPointInLeg(leg->end, other) || 
			acgPointInLeg(other->start, leg) || acgPointInLeg(other->end, leg));
}

// ------------------------------------------------------------------------------------------------------------------
// Merges legs that are overlapping and are in the same direction
static void acgLeg_MergeOverlapping(Entity *debugger)
{
	S32 i, x;
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->deleted)
			continue;

		for (x = i - 1; x >= 0; x--)
		{
			AICivilianPathLeg *other_leg = s_acgProcess.legList[x];
			if (other_leg->deleted)
				continue;
			if (other_leg->type != leg->type)
				continue;

			if (acg_d_pos)
			{
				if (acgDebug_LegNearDebugPos(leg) || acgDebug_LegNearDebugPos(other_leg))
				{
					int xxx = 0;
				}
			}

			if (acgLeg_ShouldOverlappingLegsMerge(leg, other_leg))
			{
				acgLegMerge(leg, other_leg);
				other_leg->deleted = s_acgProcess.state;
			}
		}
	}

	acgFreeDeletedLegsFromList();

}

// ------------------------------------------------------------------------------------------------------------------
static F32 acgGetPostMergeDirection(const AICivilianPathLeg *leg, const AICivilianPathLeg *other, Vec3 vDirOut)
{
	AICivilianPathLeg tmpLeg;

	tmpLeg = *leg;

	acgLegMerge(&tmpLeg, other);

	copyVec3(tmpLeg.dir, vDirOut);

	return tmpLeg.normal[2];
}


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// ------------------------------------------------------------------------------------------------------------------
static bool acgLeg_ShouldLegsMerge(const AICivilianPathLeg *leg, const AICivilianPathLeg *other)
{
#define MERGE_PROXIMITY	10.0f
	if (distance3SquaredXZ(leg->start, other->start) > SQR(MERGE_PROXIMITY) && 
		distance3SquaredXZ(leg->start, other->end) > SQR(MERGE_PROXIMITY) && 
		distance3SquaredXZ(leg->end, other->end) > SQR(MERGE_PROXIMITY) && 
		distance3SquaredXZ(leg->end, other->start) > SQR(MERGE_PROXIMITY))
	{
		return false;
	}

	{
		Vec3 vMergedDir;
		F32 fAngle1, fAngle2;
		F32 err;
		F32 fMergeThreshold = acgType_GetMergeAngleThreshold(leg->type);
		
		err = acgGetPostMergeDirection(leg, other, vMergedDir);

		if (err > acgType_GetMaxErrorThreshold(leg->type))
			return false;

		fAngle1 = acgMath_GetAngleBetweenNormsAbs(leg->dir, vMergedDir);
		fAngle2 = acgMath_GetAngleBetweenNormsAbs(other->dir, vMergedDir);
		
		if (fAngle1 > fMergeThreshold && fAngle2 > fMergeThreshold)
			return false;
	

		/*
		fAngle1 = acgMath_GetVec3Yaw(vMergedDir);
		if (fAngle1 < leg->normal[0])
		{
			fDiff = acgMath_GetAngleDiff(fAngle1, leg->normal[0]);
			if (ABS(fDiff) > MERGED_ANGLE_THRESHOLD)
				return false;
		}
		if (fAngle1 > leg->normal[1])
		{
			fDiff = acgMath_GetAngleDiff(fAngle1, leg->normal[1]);
			if (ABS(fDiff) > MERGED_ANGLE_THRESHOLD)
				return false;
		}
		
		if (fAngle1 < other->normal[0])
		{
			fDiff = acgMath_GetAngleDiff(fAngle1, other->normal[0]);
			if (ABS(fDiff) > MERGED_ANGLE_THRESHOLD)
				return false;
		}
		
		if (fAngle1 > other->normal[1])
		{
			fDiff = acgMath_GetAngleDiff(fAngle1, other->normal[1]);
			if (ABS(fDiff) > MERGED_ANGLE_THRESHOLD)
				return false;
		}
		*/
	}


	//if (acgMath_GetAngleBetweenNormsAbs(leg->dir, other->dir) > RAD(2.f))
	//	return false;

	/*
	{
		Vec3 vDir;
		F32 fDist;

		subVec3(other->start, leg->start, vDir);
		fDist = dotVec3(vDir, leg->perp);
		if (ABS(fDist) > 4.f)
			return false;
	}
	*/

	if (fabsf(leg->width - other->width) > 4.f)
		return false;

	return true;
}


// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// from the given position, get the closest leg, ignoring the one given
static AICivilianPathLeg* acgLeg_GetClosestLeg(const Vec3 vPos, const AICivilianPathLeg *ignoreLeg)
{
	S32 i;
	F32 fBestDist = FLT_MAX, fDist1, fDist2;
	AICivilianPathLeg *pBestLeg = NULL;

	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->deleted || leg == ignoreLeg || leg->type != ignoreLeg->type)
			continue;

		fDist1 = distance3Squared(vPos, leg->start);
		fDist2 = distance3Squared(vPos, leg->end);
		if (fDist1 < fBestDist || fDist2 < fBestDist)
		{
			fBestDist = MIN(fDist1, fDist2);
			pBestLeg = leg;
		}
	}


	return pBestLeg;
}

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_MergeNearby2(Entity *debugger)
{
	S32 i;
	bool didMerge;
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		AICivilianPathLeg *pPotentialMergeLeg;
		if (leg->deleted)
			continue;

		didMerge = false;

		pPotentialMergeLeg = acgLeg_GetClosestLeg(leg->start, leg);
		if (pPotentialMergeLeg && acgLeg_ShouldLegsMerge(leg, pPotentialMergeLeg))
		{
			acgLegMerge(leg, pPotentialMergeLeg);	
			pPotentialMergeLeg->deleted = s_acgProcess.state;
			didMerge = true;
		}

		pPotentialMergeLeg = acgLeg_GetClosestLeg(leg->end, leg);
		if (pPotentialMergeLeg && acgLeg_ShouldLegsMerge(leg, pPotentialMergeLeg))
		{
			acgLegMerge(leg, pPotentialMergeLeg);	
			pPotentialMergeLeg->deleted = s_acgProcess.state;
			didMerge = true;
		}

		if (didMerge)
			i --;

		/*
		for (x = i - 1; x >= 0; x--)
		{
			AICivilianPathLeg *other_leg = s_acgProcess.legList[x];
			if (other_leg->deleted)
				continue;
			if (other_leg->type != leg->type)
				continue;
			
			

			if (acgLeg_ShouldLegsMerge(leg, other_leg))
			{
				acgLegMerge(leg, other_leg);
				other_leg->deleted = s_acgProcess.state;
			}
		}
		*/
	}


	acgFreeDeletedLegsFromList();
}


static const F32* acgLegGetClosestEndAndDirMod(const AICivilianPathLeg *leg, const Vec3 vPos, F32 *pfDirMod)
{
	F32 fDist1, fDist2;

	fDist1 = distance3SquaredXZ(vPos, leg->start);
	fDist2 = distance3SquaredXZ(vPos, leg->end);

	return acgLeg_GetStartEndPosAndDir(leg, (fDist1 < fDist2), pfDirMod);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_ExtendToLeg(AICivilianPathLeg *leg, const AICivilianPathLeg *other, bool bStart)
{
	const F32 *pvLegPos, *pvOtherPos;
	F32	fDirMod, fOtherDirMod;
	Vec3 vNewLegPos;
	
	pvLegPos = acgLeg_GetStartEndPosAndDir(leg, bStart, &fDirMod);
	
	pvOtherPos = acgLegGetClosestEndAndDirMod(other, pvLegPos, &fOtherDirMod);
	
	//
	//
	acgMath_LineLine2dIntersection(pvLegPos, leg->dir, pvOtherPos, other->perp, vNewLegPos);
	{
		F32 fDist;
		Vec3 vToNewLegPos;
		

		subVec3(vNewLegPos, pvOtherPos, vToNewLegPos);
		fDist = dotVec3(other->perp, vToNewLegPos);
		if (ABS(fDist) > 2.f)
		{
			F32 fPrevDist = distance3Squared(pvLegPos, pvOtherPos);
			F32 fNewDist;
			acgMath_LineLine2dIntersection(pvLegPos, leg->dir, pvOtherPos, other->dir, vNewLegPos);
			
			fNewDist = distance3Squared(pvOtherPos, vNewLegPos);
			if (fNewDist > fPrevDist)
				return; // new position is bad
		}
	}

	copyVec3(vNewLegPos, (F32*)pvLegPos);
	leg->len = calcLegDir(leg, leg->dir, leg->perp);
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgLeg_CanExtendToLeg(const AICivilianPathLeg *leg, const AICivilianPathLeg *other, bool bStart)
{
	const F32 *pvLegPos;
	F32	fDirMod, fAngle;
	Vec3 vOtherMidPos, vToOther;

	if (leg->len > other->len)
		return false;

	pvLegPos = acgLeg_GetStartEndPosAndDir(leg, bStart, &fDirMod);

	
	#define EXTENDTO_DISTANCE_THRESHOLD		40.f
	{
		F32 fDist1, fDist2;

		fDist1 = distance3SquaredXZ(pvLegPos, other->start);
		fDist2 = distance3SquaredXZ(pvLegPos, other->end);

		if (fDist1 > SQR(EXTENDTO_DISTANCE_THRESHOLD) && fDist2 > SQR(EXTENDTO_DISTANCE_THRESHOLD))
			return false;
	}
	
	// make sure it is in the expected direction
	interpVec3(0.5f, other->start, other->end, vOtherMidPos);
	subVec3(vOtherMidPos, pvLegPos, vToOther);
	if ((dotVec3(vToOther, leg->dir) * fDirMod) < 0)
		return false;

	fAngle = acgMath_GetAngleBetweenNormsAbs(leg->dir, other->dir);

	#define EXTENDTO_ANGLE_THRESHOLD	RAD(45.f)
	if (fAngle > EXTENDTO_ANGLE_THRESHOLD)
	{
		return false;
	}

	fAngle = acgMath_GetAngleBetween(vToOther, leg->dir);
	#define EXTENDTO_CENTERED_ANGLE_THRESHOLD	RAD(10.f)
	if (fAngle > EXTENDTO_CENTERED_ANGLE_THRESHOLD)
		return false;


	return true;
	
	
}


// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_ExtendToNearby(Entity *debugger)
{
	S32 i;
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		AICivilianPathLeg *pClosest;
		if (leg->deleted)
			continue;

		if (acg_d_pos)
		{
			if (acgDebug_LegNearDebugPos(leg))
			{
				int xxx = 0;
			}
		}

		pClosest = acgLeg_GetClosestLeg(leg->start, leg);
		if (pClosest && acgLeg_CanExtendToLeg(leg, pClosest, true))
		{
			acgLeg_ExtendToLeg(leg, pClosest, true);	
		}

		pClosest = acgLeg_GetClosestLeg(leg->end, leg);
		if (pClosest && acgLeg_CanExtendToLeg(leg, pClosest, false))
		{
			acgLeg_ExtendToLeg(leg, pClosest, false);	
		}

	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_RealignToNeighbors(Entity *debugger)
{
	S32 i;
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		AICivilianPathLeg *pClosestStart, *pClosestEnd;

		if (acg_d_pos)
		{
			if(acgDebug_LegNearDebugPos(leg))
			{
				int xxx = 0;
			}
		}

		#define SKEWED_LEN_THRESHOLD	50.f
		if (leg->len > SKEWED_LEN_THRESHOLD)
		{
			continue;
		}

		pClosestStart = acgLeg_GetClosestLeg(leg->start, leg);
		pClosestEnd = acgLeg_GetClosestLeg(leg->end, leg);
		if (pClosestStart && pClosestStart != pClosestEnd)
		{
			// if the angles of it's two neighbors are fairly close
			F32 fNeighborsAngleDiff;
			F32 fAngleDiffStart, fAngleDiffEnd;
			fNeighborsAngleDiff = acgMath_GetAngleBetweenNormsAbs(pClosestStart->dir, pClosestEnd->dir);
			fAngleDiffStart = acgMath_GetAngleBetweenNormsAbs(leg->dir, pClosestStart->dir);
			fAngleDiffEnd = acgMath_GetAngleBetweenNormsAbs(leg->dir, pClosestEnd->dir);

			// case #1
			// The neighboring legs are aligned, but the one in between is at a large difference of both
			// 
			if (fNeighborsAngleDiff < RAD(5.f))
			{
				if (fAngleDiffEnd > RAD(1.f) || fAngleDiffStart > RAD(1.f))
				{
					const F32 *pvStartLegPos, *pvEndLegPos;
					F32 fStartLegDirMod, fEndLegDirMod;
					F32 fDist, fBuff;
					Vec3 vLegsStartToEnd;
					pvStartLegPos = acgLegGetClosestEndAndDirMod(pClosestStart, leg->start, &fStartLegDirMod);
					pvEndLegPos = acgLegGetClosestEndAndDirMod(pClosestEnd, leg->end, &fEndLegDirMod);

					{
						F32 fAngle;
						Vec3 vLegToLeg;

						subVec3(pvStartLegPos, pvEndLegPos, vLegToLeg);

						fAngle = acgMath_GetAngleBetween(vLegToLeg, pClosestStart->dir);
						#define REALIGN_NEIGHBORS_ANGLE_THRESHOLD	RAD(10.f)
						if (fAngle > REALIGN_NEIGHBORS_ANGLE_THRESHOLD)
							continue;
					}


			
#if 0
					subVec3(pvEndLegPos, pvStartLegPos, vLegsStartToEnd);
					fDist = normalVec3(vLegsStartToEnd);
					if (fDist < leg->len)
					{
						leg->len = fDist * .8f;
					}
					
					fBuff = (fDist - leg->len) * .5f;
					scaleAddVec3(vLegsStartToEnd, fBuff, pvStartLegPos, leg->start);
					scaleAddVec3(vLegsStartToEnd, leg->len, leg->start, leg->end );

					leg->len = calcLegDir(leg, leg->dir, leg->perp);
#else
					{
						Vec3 vCenter;
						const F32 *pvLegDir;
						
						subVec3(pvEndLegPos, pvStartLegPos, vLegsStartToEnd);
						fDist = normalVec3(vLegsStartToEnd);
						if (leg->len + 2.f > fDist)
						{
							leg->len = fDist - 2.f;
						}

						fBuff = 0.f;// (fDist - leg->len) * .5f;
						
						interpVec3(0.5f, pvEndLegPos, pvStartLegPos, vCenter);
						
						if (pClosestStart->len > pClosestEnd->len)
						{
							pvLegDir = pClosestStart->dir;
						}
						else
						{
							pvLegDir = pClosestEnd->dir;
						}

						scaleAddVec3(pvLegDir, -0.5f * leg->len, vCenter, leg->start);
						scaleAddVec3(pvLegDir, 0.5f * leg->len, vCenter, leg->end );
						
						leg->len = calcLegDir(leg, leg->dir, leg->perp);
					}
#endif
				}
			}
			else
			{
				EWinding eNeighborWinding = acgMath_GetWinding(pClosestStart->dir, pClosestEnd->dir);
				EWinding eVsStartWinding = acgMath_GetWinding(pClosestStart->dir, leg->dir);
				EWinding eVsEndWinding = acgMath_GetWinding(leg->dir, pClosestEnd->dir);
				


			}

			
			
		}
		
	}

	acgFreeDeletedLegsFromList();
}

//static int acgLeg_RoadFixup_Can 

// Walk the PathGenNodes in a breadth-first search trying to find a node of the given type.
// From the given position.
static AICivilianPathGenNode* acgPathGenNode_FindTypeNearPos(const Vec3 vPos, EAICivilianType type)
{
	#define SEARCH_THRESHOLD_DISTANCE_SQ	(SQR(4.0f))

	GenNodeIndicies *pGenNode, genNode;
	S32 x;
	U32 uNodeId = 0;

	if (! acgGetGridBlockIndiciesFromWorldPos(vPos[0], vPos[2], &genNode))
		return NULL;

	pGenNode = malloc(sizeof(GenNodeIndicies));
	memcpy(pGenNode, &genNode, sizeof(GenNodeIndicies));

	eaClearEx(&s_acgProcess.inter.eaGenNodes, NULL);
	eaPush(&s_acgProcess.inter.eaGenNodes, pGenNode);

	for (x = 0; x < eaSize(&s_acgProcess.inter.eaGenNodes); x++)
	{
		S32 i;
		AICivilianPathGenNode **ppNode;

		pGenNode = s_acgProcess.inter.eaGenNodes[x];

		ppNode = acgIndiciesGetNodeReference(pGenNode);
		if (*ppNode)
		{
			if ((*ppNode)->type == type)
			{
				eaClearEx(&s_acgProcess.inter.eaGenNodes, NULL);
				return *ppNode;
			}
		}

		for( i = 0; i < ARRAY_SIZE(offsetsNoCorners); i++)
		{
			S32 idx;
			Vec3 vNodeWorldPos;

			acgIndiciesGetNeighbor(pGenNode, offsetsNoCorners[i][0], offsetsNoCorners[i][1], &genNode);

			acgGetGenNodeIndiciesPosition(&genNode, 0.0f, vNodeWorldPos);
			if (distance3SquaredXZ(vNodeWorldPos, vPos) > SEARCH_THRESHOLD_DISTANCE_SQ)
				continue; // too far

			if ((acgCheckForCivilianVolumeLegDisable(vNodeWorldPos, true) & DISABLE_ROAD))
			{
				eaClearEx(&s_acgProcess.inter.eaGenNodes, NULL);
				return NULL;
			}

			idx = eaFindCmp(&s_acgProcess.inter.eaGenNodes, &genNode, GenNodeIndiciesCompare);
			if (idx == -1)
			{
				GenNodeIndicies *pNewNode;
				pNewNode = malloc(sizeof(GenNodeIndicies));
				memcpy(pNewNode, &genNode, sizeof(GenNodeIndicies));
				eaPush(&s_acgProcess.inter.eaGenNodes, pNewNode);
			}
		}
	}

	eaClearEx(&s_acgProcess.inter.eaGenNodes, NULL);

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_DebugPrint(const AICivilianPathLeg *leg)
{
	const char *pszType;
	switch (leg->type)
	{
		xcase EAICivilianLegType_PERSON:
			pszType = "person";
		xcase EAICivilianLegType_CAR:
			pszType = "car";
		xcase EAICivilianLegType_TROLLEY:
			pszType = "trolley";
		xdefault:
			pszType = "unknown";
	}
	printf("\tLeg (%s) : St(%.0f, %.0f, %.0f) End(%.0f, %.0f, %.0f)",
		   //"\n\t\tReason: %s ",
		   pszType, 
		   leg->start[0], leg->start[1], leg->start[2],
		   leg->end[0], leg->end[1], leg->end[2] );

}
// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathGenNode* acgLeg_RoadFixup_FindIntersection(int iPartitionIdx, AICivilianPathLeg *pLeg, bool bStart, Vec3 vOutIsectPos)
{
	F32 fCurDist;
	Vec3 vCurPos;
	const F32 *pvPos;
	F32 fDir;

#define FIND_ISECT_STEP	1.0f
#define FIND_ISECT_DIST	200.f
	
	pvPos = acgLeg_GetStartEndPosAndDir(pLeg, bStart, &fDir);
	fCurDist = FIND_ISECT_STEP;

	if (vOutIsectPos)
		zeroVec3(vOutIsectPos);
	
	for (fCurDist = FIND_ISECT_STEP; fCurDist <= FIND_ISECT_DIST; fCurDist += FIND_ISECT_STEP)
	{
		scaleAddVec3(pLeg->dir, (fDir * fCurDist), pvPos, vCurPos);
		
		// first cast downwards to find a leg
		if (acgUtil_CastVerticalVsLegs(EAICivilianLegType_NONE, vCurPos, pLeg))
		{	// hit some other leg, did not find an intersection
			return false;
		}

		{
			WorldCollCollideResults results = {0};

			acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, 5.f, 10.f);
			if (results.hitSomething)
			{
				int type = aiCivGenClassifyResults(&results);
				if (type == EAICivilianLegType_NONE) // invalid surface
				{
					if (pLeg->median_width > 0.f)
						continue;

					return NULL;
				}
				if (type == EAICivilianLegType_CAR)
					continue; // skip over cars
				
				if (type == EAICivilianLegType_INTERSECTION)
				{
					AICivilianPathGenNode *pNode;
					if (vOutIsectPos)
						copyVec3(vCurPos, vOutIsectPos);
					pNode = acgPathGenNode_FindTypeNearPos(vCurPos, EAICivilianLegType_INTERSECTION);
					if (!pNode && g_bAICivVerbose)
					{
						printf(	"\n\tRoadFixups: "
								"Found intersection at (%.2f, %.2f, %.2f) but no PathGenNode found.", 
								vCurPos[0], vCurPos[1], vCurPos[2]);
					}

					return pNode;
				}
				else if (pLeg->median_width > 0.f)
				{ 
					if ((type == EAICivilianLegType_PERSON || type == EAICivilianLegType_TROLLEY))
						continue;

					// casting over invalid type
					return NULL;
				}
				else 
				{
					// casting over invalid type
					return NULL;
				}
			}
		}
	}
	
	return NULL;
}

static bool acgLeg_RoadFixup_GetAdjacentIntersectionLineDir( const Vec3 vPos, const Vec3 vDirection, F32 fOffset, Vec3 vOutDir)
{
	AICivilianPathGenNode *pOtherNode;
	Vec3 vSearchPos;

	scaleAddVec3(vDirection, fOffset, vPos, vSearchPos);

	pOtherNode = acgPathGenNode_FindTypeNearPos(vSearchPos, EAICivilianLegType_INTERSECTION);
	if (pOtherNode && pOtherNode->line)
	{
		Vec3 vStart, vEnd;
		acgLine_GetBestFitLine(pOtherNode->line->nodes, NULL, vStart, vEnd, vOutDir);
		return true;
	}

	return false;
}

static bool acgLeg_RoadFixup_GetIntersectionLine(AICivilianPathLeg *pLeg, 
												 const Vec3 vIsectionMidPos, 
												 AICivilianPathGenNode *pNode, 
												 Vec3 vIntersectionLine )
{
	Vec3 vStart, vEnd;

	if (!pNode->line)
	{
		// log an error
		return false;
	}

	// try getting the angle of the lines at the half leg positions
	// and average them in if they are within a threshold
	{
		F32 fOffset;
		Vec3 vAdjacentDir, vCrossWalkDir;
		bool bFoundAdjacent = false;
		// AICivilianPathGenNode *pNode;
						
		
		
		
		zeroVec3(vCrossWalkDir);
				
		fOffset = pLeg->width * .25f;
		do {
			if (acgLeg_RoadFixup_GetAdjacentIntersectionLineDir(vIsectionMidPos, 
																pLeg->perp, 
																fOffset, 
																vAdjacentDir))
			{
				// check to see if they are going in the same direction
				//if (acgMath_GetAngleBetweenNormsAbs(vAdjacentDir, vCrossWalkDir) < RAD(20.f))
				{
					if (bFoundAdjacent)
					{
						if (dotVec3(vAdjacentDir, vCrossWalkDir) < 0)
						{
							negateVec3(vAdjacentDir, vAdjacentDir);
						}
						interpVec3(0.5f, vAdjacentDir, vCrossWalkDir, vCrossWalkDir);
					}
					else
					{
						copyVec3(vAdjacentDir, vCrossWalkDir);
					}
					
					bFoundAdjacent = true;
				}
			}

			// should only do this loop twice
			fOffset = -fOffset;
		} while (fOffset < 0.f); 


		if (pLeg->median_width <= 0.f)
		{
			acgLine_GetBestFitLine(pNode->line->nodes, NULL, vStart, vEnd, vAdjacentDir);
			if (dotVec3(vAdjacentDir, vCrossWalkDir) < 0)
			{
				negateVec3(vAdjacentDir, vAdjacentDir);
			}
			interpVec3(0.5f, vAdjacentDir, vCrossWalkDir, vCrossWalkDir);
		}
		else if (! bFoundAdjacent)
		{
			acgLine_GetBestFitLine(pNode->line->nodes, NULL, vStart, vEnd, vCrossWalkDir);
		}

		copyVec3(vCrossWalkDir, vIntersectionLine);
		normalVec3(vIntersectionLine);
	}
	
	return true;
}

static F32 acgGetOverlapAreaApproximation(const AICivilianPathLeg *leg, const AICivilianPathLeg *otherLeg);

static bool acgLeg_AreLegsOverlapping(const AICivilianPathLeg *pLeg, const AICivilianPathLeg *pLeg2)
{
	F32 fAreaApprox;
	F32 fLegArea;

	fAreaApprox = acgGetOverlapAreaApproximation(pLeg, pLeg2);
	fLegArea = pLeg->len * pLeg->width;
	if (fAreaApprox > (fLegArea * .3f))
		return true;
		
	fAreaApprox = acgGetOverlapAreaApproximation(pLeg2, pLeg);
	fLegArea = pLeg2->len * pLeg2->width;
	if (fAreaApprox > (fLegArea * .3f))
		return true;

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_SkewLeg(AICivilianPathLeg *leg, const Vec3 vIntersectionLine, bool bStart)
{
	if (! bStart)
	{	// we only support skewing the start edge for now.
		acgLeg_ReverseLeg(leg);
	}

	leg->bSkewed_Start = true;
	leg->fSkewedAngle_Start = getVec3Yaw(vIntersectionLine);
	leg->fSkewedLength_Start = 0.f;
	{
		F32 fAngleOffset = acgMath_GetAngleDiff(getVec3Yaw(leg->perp), leg->fSkewedAngle_Start);
		F32 fCos = cosf(fAngleOffset);
		if (fCos != 0.f)
			leg->fSkewedLength_Start = (leg->width * .5f) / fCos;
	}
	//leg->fSkewedAngleOffset_Start = acgMath_GetAngleDiff(getVec3Yaw(leg->perp), getVec3Yaw(vIntersectionLine));
	//leg->fSkewedAngleOffset_Start = ABS(leg->fSkewedAngleOffset_Start);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_RoadFixup(int iPartitionIdx, Entity *debugger)
{
	// 
	FOR_EACH_IN_EARRAY(s_acgProcess.legList, AICivilianPathLeg, pLeg)
	{
		bool bStartPos = true;

		// this is only done for roads
		if (pLeg->type != EAICivilianLegType_CAR || pLeg->deleted)
			continue;	
		
		if (acg_d_pos && acgDebug_LegNearDebugPos(pLeg))
		{
			int bbb = 0;
		}

		// do this for the start o and end of the leg
		do {
			
			Vec3 vOutPos, vIntersectionLine;
			AICivilianPathGenNode *pNode;

			pNode = acgLeg_RoadFixup_FindIntersection(iPartitionIdx, pLeg, bStartPos, vOutPos);
			if (pNode)
			{// extend the leg to the intersection point
				{
					F32 fExtendLegDist;
					const F32 *pvLegPos = acgLeg_GetStartEndPosAndDir(pLeg, bStartPos, NULL);
					
					fExtendLegDist = distance3(pvLegPos, vOutPos);

					if (fExtendLegDist > 1.f)
					{
						AICivilianPathLeg *pClosestLeg = acgLeg_GetClosestLeg(pvLegPos, pLeg);
						bool bWereOverlapping = false;

						if (pClosestLeg)
						{
							bWereOverlapping = acgLeg_AreLegsOverlapping(pLeg, pClosestLeg);
						}
						
						acgLeg_ExtendLegXFeet(pLeg, fExtendLegDist - 1.f, bStartPos);

						pClosestLeg = acgLeg_GetClosestLeg(pvLegPos, pLeg);
						if (pClosestLeg)
						{
							if (acgLeg_AreLegsOverlapping(pLeg, pClosestLeg))
							{
								pClosestLeg->deleted = s_acgProcess.state;
								pClosestLeg->deleteReason = "Overlapped by extended road to intersection.";
							}
						}
					}
					
				}
				
				// get the line direction
				if (acgLeg_RoadFixup_GetIntersectionLine(pLeg, vOutPos, pNode, vIntersectionLine))
				{
					F32 fAngle = acgMath_GetAngleBetweenNormsAbs(vIntersectionLine, pLeg->perp);
					F32 deviation = (pLeg->width * .5f) * tanf(fAngle);
					if (deviation > 8.f || fAngle >= RAD(10.f))
					{
						// this is over the threshold, we'll need to angle the leg..
						acgLeg_SkewLeg(pLeg, vIntersectionLine, bStartPos);
						break;
					}
				}
			}
			
			// should only loop twice
			bStartPos = !bStartPos;
		} while (bStartPos == false);
	}
	FOR_EACH_END
	

	acgProcess_RemoveDeletedLegsEx(true);


}


// this is for the case on a wide road with a median and having a skewed intersection
// there is sometimes a small road that gets created in one of the lanes
// this function finds those and removes them
void acgLeg_SkewedRoadFixup()
{
	// 
	FOR_EACH_IN_EARRAY(s_acgProcess.legList, AICivilianPathLeg, pLeg)
	{
		bool bStartPos = true;

		if (acg_d_pos && acgDebug_LegNearDebugPos(pLeg))
		{
			int bbb = 0;
		}

		// this is only done for roads that have a skewed start
		if (pLeg->type != EAICivilianLegType_CAR || pLeg->deleted)
			continue;	// !pLeg->bSkewed_Start || 
		
		
		
		{
			const F32 *pvLegPos = acgLeg_GetStartEndPosAndDir(pLeg, true, NULL);
			Vec3 vSearchPos;
			AICivilianPathLeg *pClosestLeg;
			F32 fDist;
			F32 fOffset = (pLeg->width * .5);
			do {
				S32 x;
				fOffset = -fOffset;
				// there have sometimes been more than one leg 
				for (x = 0; x < 3; x++)
				{
					scaleAddVec3(pLeg->perp, fOffset, pvLegPos, vSearchPos);
					pClosestLeg = acgLeg_GetClosestLeg(vSearchPos, pLeg);
					if (!pClosestLeg)
						continue;
					if ((pClosestLeg->width * 1.75f) > pLeg->width)
						continue;

					fDist = acgPointLegDistSquared(pvLegPos, pClosestLeg, NULL);
					if (fDist > SQR(pLeg->width))
					{
						continue;
					}

					if (!acgMath_IsPointInLegColumn(pClosestLeg->start, pLeg) || 
						!acgMath_IsPointInLegColumn(pClosestLeg->end, pLeg) )
					{
						continue;
					}

					pClosestLeg->deleted = s_acgProcess.state;
					pClosestLeg->deleteReason = "Overlapped by extended road to intersection.";
				}
				

			} while(fOffset < 0.f);
			

			
		}
		


	}
	FOR_EACH_END


	acgProcess_RemoveDeletedLegsEx(true);
}

// ------------------------------------------------------------------------------------------------------------------
EConnectionPlug acgLeg_GetConnectionPlugType(AICivilianPathLeg *leg, AICivilianPathLeg *otherLeg, int *bIsIntersection)
{
	EConnectionPlug legPlug = EConnectionPlug_NONE;
	
	if (leg->prev == otherLeg )
	{
		*bIsIntersection = false;
		return EConnectionPlug_START;
	}
	else if	(leg->prevInt && acgFindPathLegInACPI(leg->prevInt, otherLeg) != -1)
	{
		*bIsIntersection = true;
		return EConnectionPlug_START;
	}
	else if (leg->next == otherLeg )
	{
		*bIsIntersection = false;
		return EConnectionPlug_END;
	}
	else if (leg->nextInt && acgFindPathLegInACPI(leg->nextInt, otherLeg) != -1)
	{
		*bIsIntersection = true;
		return EConnectionPlug_END;
	}
	else
	{
		S32 i;
		for (i = 0; i < eaSize(&leg->midInts); i++)
		{
			AICivilianPathIntersection *acpi = leg->midInts[i];
			if (acgFindPathLegInACPI(acpi, otherLeg) != -1)
			{
				*bIsIntersection = true;
				return EConnectionPlug_MID;
			}
		}
	}
	*bIsIntersection = false;
	return  EConnectionPlug_NONE;
}

// ------------------------------------------------------------------------------------------------------------------
bool acgACPI_ReplaceLegIn(AICivilianPathIntersection *acpi, AICivilianPathLeg *deleteLeg, AICivilianPathLeg *leg)
{
	S32 idx = acgFindPathLegInACPI(acpi, deleteLeg);
	if (idx == -1)
		return false;

	acpi->legIntersects[idx]->leg = leg;

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
bool acgLeg_DeleteLeg(AICivilianPathLeg *leg, AICivilianPathLeg *deleteLeg)
{
	int legIsIntersection = false, deleteIsIntersection = false;
	EConnectionPlug legPlug = acgLeg_GetConnectionPlugType(leg, deleteLeg, &legIsIntersection);
	EConnectionPlug deletePlug = acgLeg_GetConnectionPlugType(deleteLeg, leg, &deleteIsIntersection);
	
	if (legIsIntersection != deleteIsIntersection)
	{
		printf("\nDelete Leg: Bad leg configuration\n");
		return false;
	}
	if (legPlug == EConnectionPlug_MID || deletePlug == EConnectionPlug_MID)
	{
		printf("\nDoes not support mid intersection deletion\n");
		return false;
	}
	
	if (deletePlug == EConnectionPlug_START)
	{
		if (legIsIntersection)
		{
			acgACPI_ReplaceLegIn(deleteLeg->nextInt, deleteLeg, leg);

			if (legPlug == EConnectionPlug_START)
			{
				leg->prev = NULL;
				leg->prevInt = deleteLeg->nextInt;
			}
			else
			{
				leg->next = NULL;
				leg->nextInt = deleteLeg->nextInt;
			}
		}
		else 
		{
			AICivilianPathLeg *nextLeg = deleteLeg->next;
			if (nextLeg)
			{
				int nextIsIntersection = false;
				
				EConnectionPlug nextLegPlug = acgLeg_GetConnectionPlugType(nextLeg, deleteLeg, &nextIsIntersection);
				if (nextIsIntersection || nextLegPlug == EConnectionPlug_MID)
				{
					printf("Unexpected intersection\n");
					return false;
				}

				if (nextLegPlug == EConnectionPlug_START)
				{
					nextLeg->prev = leg;
				}
				else// if (nextLegPlug == EConnectionPlug_END)
				{
					nextLeg->next = leg;
				}
			}

			if (legPlug == EConnectionPlug_START)
			{
				leg->prev = nextLeg;
				leg->prevInt = NULL;
			}
			else
			{
				leg->next = nextLeg;
				leg->nextInt = NULL;
			}
		}
	}
	else //if (deletePlug == EConnectionPlug_START)
	{
		if (legIsIntersection)
		{
			acgACPI_ReplaceLegIn(deleteLeg->prevInt, deleteLeg, leg);

			if (legPlug == EConnectionPlug_START)
			{
				leg->prev = NULL;
				leg->prevInt = deleteLeg->prevInt;
			}
			else
			{
				leg->next = NULL;
				leg->nextInt = deleteLeg->prevInt;
			}
		}
		else if (deleteLeg->prev)
		{
			AICivilianPathLeg *prevLeg = deleteLeg->prev;
			if (prevLeg)
			{
				int prevIsIntersection = false;
				EConnectionPlug prevLegPlug = acgLeg_GetConnectionPlugType(prevLeg, deleteLeg, &prevIsIntersection);
				if (prevIsIntersection || prevLegPlug == EConnectionPlug_MID)
				{
					printf("Unexpected intersection\n");
					return false;
				}

				if (prevLegPlug == EConnectionPlug_START)
				{
					prevLeg->prev = leg;
				}
				else// if (prevLegPlug == EConnectionPlug_END)
				{
					prevLeg->next = leg;
				}
			}
			

			if (legPlug == EConnectionPlug_START)
			{
				leg->prev = prevLeg;
				leg->prevInt = NULL;
			}
			else
			{
				leg->next = prevLeg;
				leg->nextInt = NULL;
			}
		}
		
	}

	if (eaSize(&deleteLeg->midInts) > 0)
	{
		FOR_EACH_IN_EARRAY(deleteLeg->midInts, AICivilianPathIntersection, acpi)
		{
			acgACPI_ReplaceLegIn(acpi, deleteLeg, leg);
			eaPush(&leg->midInts, acpi);
		}
		FOR_EACH_END
		
		eaClear(&deleteLeg->midInts);
	}



	deleteLeg->deleted = s_acgProcess.state;
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
// find all legs that become overlapped by extending the given leg 
static AICivilianPathLeg** acgFindLegsOverlappingExtension(AICivilianPathLeg *extendedLeg, F32 fExtendAmount, bool bStart)
{
	AICivilianPathLeg **peaLegs = NULL;
	Vec3 vLegPt_Pre, vLegPt_Post;

	if (bStart)
	{
		copyVec3(extendedLeg->start, vLegPt_Pre);
		scaleAddVec3(extendedLeg->dir, -fExtendAmount, vLegPt_Pre, vLegPt_Post);
	}
	else
	{
		copyVec3(extendedLeg->end, vLegPt_Pre);
		scaleAddVec3(extendedLeg->dir, fExtendAmount, vLegPt_Pre, vLegPt_Post);
	}

	FOR_EACH_IN_EARRAY(s_acgProcess.legList, AICivilianPathLeg, pLeg)
	{
		if (pLeg->deleted || extendedLeg == pLeg)
			continue;
		
		if (acgMath_IsPointInLegColumn(vLegPt_Pre, pLeg) && 
				acgMath_IsPointInLegColumn(vLegPt_Post, pLeg))
		{
			Vec3 vDir_Pre, vDir_Post;
			Vec3 vTestPos;
			const F32 *pvLegPos = NULL;
			F32 fDirMod = 1.f;

			pvLegPos = acgLegGetClosestEndAndDirMod(pLeg, vLegPt_Pre, &fDirMod);
			scaleAddVec3(pLeg->dir, -fDirMod, pvLegPos, vTestPos);
			subVec3(vLegPt_Pre, vTestPos, vDir_Pre);
			subVec3(vLegPt_Post, vTestPos, vDir_Post);
			if (dotVec3(vDir_Post, vDir_Pre) < 0.f)
			{
				eaPush(&peaLegs, pLeg);
			}
		}
	}
	FOR_EACH_END

	return peaLegs;
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgLeg_FixupCarPaths_TruncateLeg(const AICivilianPathLeg *pLeg, bool bStart, AICivilianPathLeg *pOverlappedLeg) 
{
	bool bOverlappedStart = false;
	// get the opposite leg position and find out which end of the overlapped is closer
	const F32 *pvLegPos = (bStart) ? pLeg->end : pLeg->start;
	F32 *pvOverlappedPos; 
	F32 fDistStart = distance3(pOverlappedLeg->start, pvLegPos);
	F32 fDistEnd = distance3(pOverlappedLeg->end, pvLegPos);

	// find out which side of the overlapped leg we need to truncate
	if (fDistStart < fDistEnd)
	{
		bOverlappedStart = true;
		pvOverlappedPos = pOverlappedLeg->start;
	}
	else
	{
		bOverlappedStart = false;
		pvOverlappedPos = pOverlappedLeg->end;
	}
	
	// find out how much to truncate the leg
	pvLegPos = (bStart) ? pLeg->start : pLeg->end;
	
	fDistStart = distance3(pvOverlappedPos, pvLegPos) + 1;
	if (pOverlappedLeg->len - fDistStart < 15.f)
	{
		// this leg is going to be too short after truncating
		// it needs to be deleted
		return false;
	}

	acgLeg_ExtendLegXFeet(pOverlappedLeg, -fDistStart, bOverlappedStart);
	return true;

}

// ------------------------------------------------------------------------------------------------------------------
static void acgLeg_FixupCarPaths_ExtendLeg(int iPartitionIdx, AICivilianPathLeg *pLeg, F32 fExtendAmount, bool bStart)
{
	bool bWasDeletion = false;
	AICivilianPathLeg **peaOverlappedLegs;
	peaOverlappedLegs = acgFindLegsOverlappingExtension(pLeg, fExtendAmount, bStart);
	acgLeg_ExtendLegXFeet(pLeg, fExtendAmount, bStart);

	if (g_bAICivVerbose)
	{
		printf("\nExtending Leg by %.1f feet at %s:", 
					fExtendAmount, 
					(bStart) ? "start" : "end");
		acgLeg_DebugPrint(pLeg);
	}


	FOR_EACH_IN_EARRAY(peaOverlappedLegs, AICivilianPathLeg, pOverlapped)
	{
		bool bDeleteLeg = true;
		if (acgPointInLeg((bStart?pLeg->start:pLeg->end), pOverlapped))
		{	
			if (acgLeg_FixupCarPaths_TruncateLeg(pLeg, bStart, pOverlapped))
			{
				bDeleteLeg = false;
			}
		}

		if (bDeleteLeg)
		{
			bWasDeletion = true;
			// totally overlapped, remove this leg
			printf("\n\tRemoving overlapped leg:");
			acgDestroyLeg(&pOverlapped, false);
			acgLeg_DebugPrint(pOverlapped);
			pOverlapped->deleted = s_acgProcess.state;
			pOverlapped->deleteReason = "Overlapped leg by length fixup";
		}
	}
	FOR_EACH_END

	eaDestroy(&peaOverlappedLegs);

	if (bWasDeletion)
	{
		acgIntersection_ProcessLeg(iPartitionIdx, pLeg, bStart);
	}
}

// ------------------------------------------------------------------------------------------------------------------
#define MIN_LENGTH_FOR_STOPS		(30.f)
#define MIN_LENGTH_FOR_MID_TURNS	(45.f)

static void acgLeg_FixupCarPaths(int iPartitionIdx)
{
	FOR_EACH_IN_EARRAY(s_acgProcess.legList, AICivilianPathLeg, pLeg)
	{
		// this is only done for roads that have a skewed start
		if (pLeg->type != EAICivilianLegType_CAR || pLeg->deleted)
			continue;	

		if (acg_d_pos && acgDebug_LegNearDebugPos(pLeg))
		{
			int bbb = 0;
		}

		

		FOR_EACH_IN_EARRAY(pLeg->midInts, AICivilianPathIntersection, acpi)
		{
			PathLegIntersect *pIsectPli, *pThisPli;
			acgMidIntersectionGetLegAndIsectLeg(acpi, &pIsectPli, &pThisPli);
			assert(pThisPli->leg == pLeg);

			// for all mid intersections make sure that the intersection point is not
			// too close to the end or start position
			{
				F32 fDist = distance3(pIsectPli->intersect, pLeg->start);
				if (fDist < MIN_LENGTH_FOR_MID_TURNS)
				{
					F32 fExtendAmount = MIN_LENGTH_FOR_MID_TURNS - fDist;
					acgLeg_FixupCarPaths_ExtendLeg(iPartitionIdx, pLeg, fExtendAmount, true);
				}

				fDist = distance3(pIsectPli->intersect, pLeg->end);
				if (fDist < MIN_LENGTH_FOR_MID_TURNS)
				{
					F32 fExtendAmount = MIN_LENGTH_FOR_MID_TURNS - fDist;
					acgLeg_FixupCarPaths_ExtendLeg(iPartitionIdx, pLeg, fExtendAmount, false);
				}
			}
		}
		FOR_EACH_END


		if (pLeg->nextInt && (pLeg->nextInt->bIsMidIntersection || eaSize(&pLeg->nextInt->legIntersects) > 2))
		{
			F32 fExtendAmount = MIN_LENGTH_FOR_STOPS - pLeg->len;
			if (fExtendAmount > 0.f)
			{
				acgLeg_FixupCarPaths_ExtendLeg(iPartitionIdx, pLeg, fExtendAmount, true);
				continue;
			}
		} 
		if (pLeg->prevInt && (pLeg->prevInt->bIsMidIntersection || eaSize(&pLeg->prevInt->legIntersects) > 2))
		{
			S32 i;
			F32 fLenDist = pLeg->len;
			F32 fExtendAmount;
			if (pLeg->bSkewed_Start)
			{
				for (i = 1; i <= pLeg->max_lanes; ++i)
				{
					Vec3 vLanePos;
					F32 dist; 
					acgLeg_GetLanePos(pLeg, i, 0.f, false, vLanePos);
					dist = distance3(vLanePos, pLeg->end);
					if (dist < fLenDist)
						fLenDist = dist;
				}
			}

			fExtendAmount = MIN_LENGTH_FOR_STOPS - fLenDist;
			if (fExtendAmount > 0.f)
			{
				acgLeg_FixupCarPaths_ExtendLeg(iPartitionIdx, pLeg, fExtendAmount, false);
			}
		}



	}
	FOR_EACH_END


	acgProcess_RemoveDeletedLegs();

}

// ------------------------------------------------------------------------------------------------------------------
typedef struct ACGVisIgnore {
	int hitType;
	int firstType;
	AICivilianPathGenLine *line;
	EAICivilianLegType type;
} ACGVisIgnore;

static int acgVisibleIgnore(AICivilianPathGenNode *node, ACGVisIgnore *ignore)
{
	if(!ignore->firstType)
		ignore->firstType = node->type;

	if(ignore->firstType!=ignore->type)
	{
		// Started inside something
		if(node->type==ignore->type)
		{
			if(!ignore->hitType)
			{
				ignore->line = node->line;
				ignore->hitType = 1;
			}
			else if(node->line!=ignore->line)
				return 0;
		}

		return 1;
	}
	else
		return 0;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgCanLegCrossType(EAICivilianLegType t1, EAICivilianLegType t2)
{
	switch (t1)
	{
		xcase EAICivilianLegType_PERSON:
			return false;
		xcase EAICivilianLegType_CAR:
			if (t2 == EAICivilianLegType_PARKING)
				return true;
			return false;
		xcase EAICivilianLegType_PARKING:
			return false;
		xcase EAICivilianLegType_TROLLEY:
			return false;
		xcase EAICivilianLegType_INTERSECTION:
			return false;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
#define LEG_VISIBILITY_MAX_DIST (210.f)
static int acgLegIsVisible(int iPartitionIdx, const AICivilianPathLeg *leg, const AICivilianPathLeg *other_leg)
{
	int orient = calcLegOrient(leg, other_leg);
	int other_orient = calcLegOrient(other_leg, leg);
	Vec3 dir;
	F32 len, distSQR;
	Vec3 leg_pt, other_pt;

	distSQR = acgLegLegDistSquared(leg, leg_pt, other_leg, other_pt);
	subVec3(other_pt, leg_pt, dir);
	len = normalVec3(dir);

	if(distSQR <= 1)
		return true;
	if (distSQR > SQR(LEG_VISIBILITY_MAX_DIST))
		return false;

	if (acg_d_pos && (acgDebug_LegNearDebugPos(leg) || acgDebug_LegNearDebugPos(other_leg)))
	{
		int bbb = 0;
	}

	if (leg->median_width > 0.f && other_leg->median_width > 0.f)
	{
		// with two medians trying to connect, get the lane positions instead 
		// just in case there is stuff on the medians that can block LOS
		if (dotVec3(leg->dir, other_leg->dir) < 0.f)
		{// legs are facing opposite direction
			F32 offset = leg->median_width * 0.5f + 10.f;
			scaleAddVec3(leg->perp, offset, leg_pt, leg_pt);
			offset = -(other_leg->median_width * 0.5f + 10.f);
			scaleAddVec3(other_leg->perp, offset, other_pt, other_pt);
		}
		else
		{
			F32 offset = leg->median_width * 0.5f + 10.f;
			scaleAddVec3(leg->perp, offset, leg_pt, leg_pt);
			offset = other_leg->median_width * 0.5f + 10.f;
			scaleAddVec3(other_leg->perp, offset, other_pt, other_pt);
		}
	}

	{
		S32 x;
		// loop through all the legs and see if the line would cross over any other box.
		// I'm not allowing any other leg penetration.
		for (x = 0; x < eaSize(&s_acgProcess.legList); x++)
		{
			AICivilianPathLeg *test_leg = s_acgProcess.legList[x];
			Vec3 legMin, legMax, localPt1, localPt2, tmp;
			Mat4 mtxLeg;

			if (test_leg->deleted)
				continue;

			if (test_leg == leg || 
				test_leg == other_leg ||
				acgCanLegCrossType(leg->type, test_leg->type))
			{
				continue;
			}

			acgLegToOBB(test_leg, legMin, legMax, mtxLeg);

			subVec3(leg_pt, mtxLeg[3], tmp);
			mulVecMat3Transpose(tmp, mtxLeg, localPt1);
			subVec3(other_pt, mtxLeg[3], tmp);
			mulVecMat3Transpose(tmp, mtxLeg, localPt2);

			if (lineSegBoxCollision(localPt1, localPt2, legMin, legMax, tmp))
				return false;
		}
	}


	{
		
		// check to see if the line would go through any disable volumes 
		FOR_EACH_IN_EARRAY(g_civSharedState.eaCivilianVolumes, WorldVolumeEntry, pVolume)
		{
			const WorldVolumeElement *volumeElement;
			const WorldCivilianVolumeProperties *pCivVolume = pVolume->server_volume.civilian_volume_properties;
			Vec3 vIsect;
			
			volumeElement = eaGet(&pVolume->elements, 0);
			
			if (! pCivVolume || !volumeElement)
				continue;

			if ( (pCivVolume->disable_sidewalks && leg->type == EAICivilianLegType_PERSON) ||
				 (pCivVolume->disable_roads && leg->type == EAICivilianLegType_CAR))
			{
				if (acgLineSegVsBox(leg_pt, other_pt, volumeElement->local_min, 
									volumeElement->local_max, 
									pVolume->base_entry.bounds.world_matrix, vIsect))
				{
					return false;
				}
			}
		}
		FOR_EACH_END
	}
	
	
	{
		Vec3 vCastSt, vCastEnd;
		WorldCollCollideResults results = {0};
		S32 sanity = 1;

		copyVec3(leg_pt, vCastSt);
		copyVec3(other_pt, vCastEnd);
		vCastSt[1] += 2.5f;
		vCastEnd[1] += 2.5f;

		acgCastRay(iPartitionIdx, &results, vCastSt, vCastEnd);
		
		if (results.hitSomething)
			return false;
	}
	
	// cast rays along the proposed intersection and make sure we are above the material type expected
	{
		F32 fInterp = .2f;
		while(fInterp <= 1.0f)
		{
			Vec3 vPos;
			int type;
			WorldCollCollideResults results = {0};

			interpVec3(fInterp, leg_pt, other_pt, vPos);
			
			acgUtil_CastVerticalRay(iPartitionIdx, &results, vPos, 10.0f, 20.0f);
			
			type = aiCivGenClassifyResults(&results);
			
			if (! acgType_CanTypeIntersectOverType(leg->type, type))
			{
				if (! (leg->median_width > 0.f && type == EAICivilianLegType_PERSON))
				{
					return false;
				}
			}
			

			fInterp += .2f;
		}

	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgProcessLegsRemoveLowDistance(Entity *debugger)
{
	static AICivilianPathLeg **legs;
	static AICivilianPathIntersection **acpis;
	static StashTable visitedLegs = 0;
	static StashTable visitedACPIs = 0;
	ANALYSIS_ASSUME(s_acgProcess.legList);

	if(!visitedLegs)
		visitedLegs = stashTableCreateAddress(100);
	if(!visitedACPIs)
		visitedACPIs = stashTableCreateAddress(100);

	for(; s_acgProcess.dist.i<eaSize(&s_acgProcess.legList); s_acgProcess.dist.i++)
	{
		F32 dist = 0;
		int i = s_acgProcess.dist.i;
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if(leg->deleted)
			continue;
		if (! acgType_DoesCheckMinDistance(leg->type))
			continue;
		//if(leg->touched==s_acgProcess.state) // Some other leg already tested this one
		//	continue;

		if(leg->bIsForcedLeg)
			continue;

		eaClear(&legs);
		eaClear(&acpis);
		stashTableClear(visitedLegs);
		stashTableClear(visitedACPIs);

		eaPush(&legs, leg);

		while(eaSize(&legs) || eaSize(&acpis))
		{
			int j;
			AICivilianPathLeg *curLeg = eaPop(&legs);
			AICivilianPathIntersection *intersect;

			if(!curLeg)
			{
				intersect = eaPop(&acpis);
				if(!stashAddressAddInt(visitedACPIs, intersect, 1, 0))
					continue;

				for(j=0; j<eaSize(&intersect->legIntersects); j++)
					eaPush(&legs, intersect->legIntersects[j]->leg);

				continue;
			}

			if(!stashAddressAddInt(visitedLegs, curLeg, 1, 0))
				continue;

			if (curLeg->bIsForcedLeg)
			{
				dist = ACG_MINIMUM_CIRCUIT_DIST;
				break;
			}
			
			/*
			if(curLeg->touched==s_acgProcess.state)  // Some other leg already tested this one
			{
				dist = ACG_MINIMUM_CIRCUIT_DIST+1;  // Fake it
				break;
			}
			*/

			dist += curLeg->len;

			if(dist>=ACG_MINIMUM_CIRCUIT_DIST)
				break;

			if(curLeg->next)
				eaPush(&legs, curLeg->next);
			if(curLeg->prev)
				eaPush(&legs, curLeg->prev);

			if(curLeg->nextInt)
				eaPush(&acpis, curLeg->nextInt);

			if(curLeg->prevInt)
				eaPush(&acpis, curLeg->prevInt);

			eaPushEArray(&acpis, &curLeg->midInts);
		}

		if(dist<ACG_MINIMUM_CIRCUIT_DIST)
		{
			StashTableIterator iter;
			StashElement elem;
			int j;

			stashGetIterator(visitedLegs, &iter);
			while(stashGetNextElement(&iter, &elem))
			{
				AICivilianPathLeg *curLeg = stashElementGetKey(elem);

				curLeg->deleted = s_acgProcess.state;
				curLeg->deleteReason = "Minimum circuit distance";

				curLeg->nextInt = NULL;
				curLeg->prevInt = NULL;
				eaDestroy(&curLeg->midInts);
			}
			stashGetIterator(visitedACPIs, &iter);
			while(stashGetNextElement(&iter, &elem))
			{
				AICivilianPathIntersection *acpi = stashElementGetKey(elem);

				for(j=0; j<eaSize(&acpi->legIntersects); j++)
				{
					PathLegIntersect *pli = acpi->legIntersects[j];

					devassert(pli->leg->nextInt==NULL && pli->leg->prevInt==NULL && pli->leg->midInts==NULL);
				}

				eaFindAndRemoveFast(&s_acgPathInfo.intersects, acpi);
				
				acgPathIntersection_Free(acpi);
			}

#ifdef CIVILIAN_PARANOID
			// This means some intersection is getting overwritten or not added to the leg
			// and will cause intersections to connect to leg 0.  Bad.
			for(j=0; j<eaSize(&g_aiCivilianState.intersects); j++)
			{
				AICivilianPathIntersection *acpi = g_aiCivilianState.intersects[j];
				int k;

				for(k=0; k<eaSize(&acpi->legIntersects); k++)
				{
					AICivilianPathLeg *test = acpi->legIntersects[k]->leg;
					devassert(!test->deleted || stashAddressFindInt(visitedLegs, test, NULL));
					devassert(!test->deleted || stashAddressFindInt(visitedACPIs, acpi, NULL));
				}
			}
#endif
		}
	}

	if(s_acgProcess.dist.i==eaSize(&s_acgProcess.legList))
	{
		acgProcess_RemoveDeletedLegs();
		return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
bool acgDetermineGroundCoPlanarLegs(int iPartitionIdx, Entity *debugger)
{
#define GATHER_COPLANAR_STATS

	const F32 COPLANAR_TOLERANCE = 4.0f / 12.0f; // 2 inches
	const F32 ERROR_TOLERANCE_PERCENT = 0.095f;
	int i;

#if defined (GATHER_COPLANAR_STATS)
	struct
	{
		F32 averageErrorOnPassedLegs;
		F32 percentLegsCoPlanar;
		F32 numLegsTested;
		bool hasPassed;
	} coplanar_stats = {0};
#endif


	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		devassert(!leg->deleted);

		// at every unit leg position, cast a ray, and if the point we hit
		// is not co-planar with the leg, then we have a high performance leg
		// and mark it as such
		{
			const F32 	fHalfWidth = leg->width * 0.5f;
			const F32 	fTotalLegArea = leg->len * leg->width - leg->len * leg->median_width;
			F32	fHalfMedianWidth = leg->median_width * 0.5f;

			bool 	bLegCoPlanar = true;
			Vec3	vPos, vLegNormal;
			F32		fNumToleranceInfractions;

			F32		fWidthDist = -fHalfWidth + 0.5f;

			fNumToleranceInfractions = 0;

			if (leg->median_width > 0.0f)
			{
				// if the leg has a median, we are going to make sure the start and end are snapped
				// to the road and not the median.
				Vec3 pt;

				scaleAddVec3(leg->perp, fHalfMedianWidth + 1.0f, leg->start, pt);
				if (acgSnapPosToGround(iPartitionIdx, pt, 5, -100))
				{
					leg->start[1] = pt[1];
				}

				scaleAddVec3(leg->perp, fHalfMedianWidth + 1.0f, leg->end, pt);
				if (acgSnapPosToGround(iPartitionIdx, pt, 5, -100))
				{
					leg->end[1] = pt[1];
				}

				acgLeg_RecalculateLegDir(leg);
				// leg->len = calcLegDir(leg, leg->dir, leg->perp);
			}


			crossVec3(leg->dir, leg->perp, vLegNormal);
			normalVec3(vLegNormal);

			while(fWidthDist <= fHalfWidth && bLegCoPlanar)
			{
				F32		fLenDist = 0.5f;
				copyVec3(leg->start, vPos);
				scaleAddVec3(leg->perp, fWidthDist, leg->start, vPos);

				while(fLenDist <= leg->len)
				{
					Vec3	vWorldPosUp, vWorldPosDown, vDir;
					F32 	fPlaneDist;
					WorldCollCollideResults results = {0};
					bool bHit;

					copyVec3(vPos, vWorldPosUp);
					copyVec3(vPos, vWorldPosDown);
					vWorldPosUp[1] += 5.0f;
					vWorldPosDown[1] -= 10.0f;

					bHit = wcRayCollide(worldGetActiveColl(iPartitionIdx), vWorldPosUp, vWorldPosDown, WC_QUERY_BITS_AI_CIV, &results);

					if(results.errorFlags.noScene)
					{
						if(psdkSceneLimitReached())
							continue;
						wcForceSimulationUpdate();
						continue;
					}
					if(results.errorFlags.noCell)
						continue;

					if (bHit)
					{
						// get the direction from the impact position to the leg start
						subVec3(leg->start, results.posWorldImpact, vDir);

						fPlaneDist = dotVec3(vDir, vLegNormal);
						if (fabs(fPlaneDist) > COPLANAR_TOLERANCE)
						{
							// outside of our tolerance, mark as not hit
							bHit = false;
						}
					}

					if (!bHit)
					{
						fNumToleranceInfractions += 1.0f;
						if (fNumToleranceInfractions/fTotalLegArea > ERROR_TOLERANCE_PERCENT)
						{
							bLegCoPlanar = false;
							break;
						}
					}

					fLenDist += 1.0f;
					addVec3(vPos, leg->dir, vPos);
				}

				// todo: do we care about the leftover of the length / width?
				fWidthDist += 1.0f;


				if (fHalfMedianWidth != 0)
				{	// skip over the median
					if (fWidthDist >= -(fHalfMedianWidth - 0.5f))
					{
						fWidthDist = fHalfMedianWidth + 0.5f;
						fHalfMedianWidth = 0.0f; // we've skipped over the median, so we can set it to 0 now.
					}
				}
			}


			leg->bIsGroundCoplanar = bLegCoPlanar;


#if defined (GATHER_COPLANAR_STATS)
			coplanar_stats.numLegsTested++;

			// gather stats
			if (bLegCoPlanar)
			{
				F32 error = fNumToleranceInfractions/fTotalLegArea;
				if (!coplanar_stats.hasPassed)
				{
					coplanar_stats.hasPassed = true;
					coplanar_stats.averageErrorOnPassedLegs = error;
				}
				else
				{
					coplanar_stats.averageErrorOnPassedLegs += error;
					coplanar_stats.averageErrorOnPassedLegs *= 0.5f;
				}

				coplanar_stats.percentLegsCoPlanar++;
			}
#endif

		}
	}

#if defined (GATHER_COPLANAR_STATS)
	if (coplanar_stats.numLegsTested > 0)
	{
		coplanar_stats.percentLegsCoPlanar = coplanar_stats.percentLegsCoPlanar / coplanar_stats.numLegsTested;
	}

#endif

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
F32 calcNodeLineDist(AICivilianPathGenLine *line, AICivilianPathGenNode *node)
{
	Vec3 start, dir;
	F32 line_dist, dist;
	Vec3 node_pos;

	acgGetPathGenNodePosition(node, node_pos);
	acgGetPathGenNodePosition(eaHead(&line->nodes), start);
	acgGetPathGenNodePosition(eaTail(&line->nodes), dir);
	subVec3(dir, start, dir);
	line_dist = normalVec3(dir);

	dist = PointLineDistSquaredXZ(node_pos, start, dir, line_dist, NULL);

	return dist;
}

// ------------------------------------------------------------------------------------------------------------------
int calcLegOrient(const AICivilianPathLeg *leg, const AICivilianPathLeg *other_leg)
{
	if(pointLineDistSquared(leg->start, other_leg->start, other_leg->end, NULL) <
		pointLineDistSquared(leg->end, other_leg->start, other_leg->end, NULL))
		return ALF_PREV;
	else
		return ALF_NEXT;
}



// ------------------------------------------------------------------------------------------------------------------
static void acgConnectLegs(AICivilianPathLeg *leg, AICivilianPathLeg *other)
{
	if(calcLegOrient(leg, other))
		leg->prev = other;
	else
		leg->next = other;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgMakeLeg(AICivilianPathLeg *leg, AICivilianPathLeg *other, Vec3 startOut, Vec3 endOut)
{
	int start_leg = calcLegOrient(leg, other);
	int start_other = calcLegOrient(other, leg);

	copyVec3(start_leg ? leg->start : leg->end, startOut);
	copyVec3(start_other ? other->start : other->end, endOut);
}


// ------------------------------------------------------------------------------------------------------------------
int acgIntersectionLegsSimilar(AICivilianPathLeg *leg, AICivilianPathLeg *other)
{
	return distance3Squared(leg->start, other->start)<SQR(0.1) && distance3Squared(leg->end, other->end)<SQR(0.1) ||
			distance3Squared(leg->start, other->end)<SQR(0.1) && distance3Squared(leg->end, other->start)<SQR(0.1);
}

// ------------------------------------------------------------------------------------------------------------------
int cmpDistLeg(const AICivilianPathLeg *leg, const AICivilianPathLeg **left, const AICivilianPathLeg **right)
{
	F32 dl = acgLegLegDistSquared(leg, NULL, (*left), NULL);
	F32 dr = acgLegLegDistSquared(leg, NULL, (*right), NULL);

	return dl < dr ? -1 : 1;
}

// ------------------------------------------------------------------------------------------------------------------
static int acgConnection_ProcessImplicitConnection(Entity *debugger)
{
	int i;
	AICivilianPathLeg **tempLegsStart = NULL, **tempLegsEnd = NULL;

	// for each leg, find all the nearby legs and attempt to directly connect them together

	for(i=0; i<eaSize(&s_acgProcess.legList); i++)
	{
		int j;
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		devassert(!leg->deleted);

		eaSetSize(&tempLegsStart, 0);  eaSetSize(&tempLegsEnd, 0);
		acgPathLegFindNearbyLegs(leg, NULL, &tempLegsStart, &tempLegsEnd, NULL, 10+leg->width/2);
		
		if(!leg->doneStart)
		{
			for(j=0; j<eaSize(&tempLegsStart); j++)
			{
				Vec3 ocoll;
				AICivilianPathLeg *other = tempLegsStart[j];
				F32 eff_width = (1 - fabs(dotVec3(leg->dir, other->dir))) * leg->width/2;

				if(acgLegLegDistSquared(leg, NULL, other, ocoll) < eff_width + 1 &&
					(nearSameVec3(other->start, ocoll) || nearSameVec3(other->end, ocoll)))
				{
					// Connect them!
					int orient = calcLegOrient(other, leg);

					if(orient && other->doneStart || !orient && other->doneEnd)
						continue;

					leg->doneStart = 1;
					leg->prev = other;

					if(orient)
					{
						devassert(!other->doneStart);
						other->doneStart = 1;
						other->prev = leg;
					}
					else
					{
						devassert(!other->doneEnd);
						other->doneEnd = 1;
						other->next = leg;
					}
				}
			}
		}

		if(!leg->doneEnd)
		{
			for(j=0; j<eaSize(&tempLegsEnd); j++)
			{
				Vec3 ocoll;
				AICivilianPathLeg *other = tempLegsEnd[j];
				F32 eff_width = (1 - fabs(dotVec3(leg->dir, other->dir))) * leg->width/2;

				if(acgLegLegDistSquared(leg, NULL, other, ocoll) < eff_width + 1 &&
					(nearSameVec3(other->start, ocoll) || nearSameVec3(other->end, ocoll)))
				{
					// Connect them!
					int orient = calcLegOrient(other, leg);

					if(orient && other->doneStart || !orient && other->doneEnd)
						continue;

					leg->doneEnd = 1;
					leg->next = other;

					if(orient)
					{
						devassert(!other->doneStart);
						other->doneStart = 1;
						other->prev = leg;
					}
					else
					{
						devassert(!other->doneEnd);
						other->doneEnd = 1;
						other->next = leg;
					}
				}
			}
		}
	}

	for(i=eaSize(&s_acgProcess.legList)-1; i>=0; i--)
	{
		s_acgProcess.legList[i]->doneStart = 0;
		s_acgProcess.legList[i]->doneEnd = 0;
	}

	eaDestroy(&tempLegsStart);
	eaDestroy(&tempLegsEnd);

	return 1;
}


// ------------------------------------------------------------------------------------------------------------------
#define FLOOD_CAST_DIST_UP		20.0f
#define FLOOD_CAST_DIST_DOWN	30.0f
#define NON_INTERSECTION_TYPE	(1)

#define INTERSECTION_TEST_DIST	(50.0f)
#define INTERSECTION_SEARCH_DIST (400.0f)
static U32 s_iIntersectionID;



static bool acgFloodFill_IsNodeFloodable(AICivilianPathGenNode *pNode, U32 iCurIntersectionID)
{
	U32 nodeAsU32;

	nodeAsU32 = PTR_TO_U32(pNode);
	if (nodeAsU32 >= iCurIntersectionID)
	{
		if (nodeAsU32 > s_iIntersectionID)
			return (pNode && (pNode->type == EAICivilianLegType_TROLLEY || pNode->type == EAICivilianLegType_INTERSECTION));

		return false;
	}
	if (nodeAsU32 == NON_INTERSECTION_TYPE)
		return false;

	return true;
}

static void acgFloodFill_FreeIfAllocedNode(AICivilianPathGenNode **ppNode, U32 iCurIntersectionID)
{
	U32 nodeAsU32;

	nodeAsU32 = PTR_TO_U32(*ppNode);
	if (nodeAsU32 > s_iIntersectionID)
	{
		if (*ppNode && ((*ppNode)->type == EAICivilianLegType_TROLLEY || (*ppNode)->type == EAICivilianLegType_INTERSECTION))
		{
			acgNodeFree(*ppNode);
			*ppNode = NULL;
		}
	}
}

static EAICivilianLegType acgFloodFill_GetNodeType(AICivilianPathGenNode *pNode)
{
	U32 nodeAsU32;

	nodeAsU32 = PTR_TO_U32(pNode);
	if (nodeAsU32 > s_iIntersectionID)
	{
		return (pNode) ? pNode->type : EAICivilianLegType_NONE;
	}

	return EAICivilianLegType_NONE;
}

static void acgFloodFillAreaForIntersections(int iPartitionIdx, Entity *debugger, U32 iCurIntersectionID, const Vec3 vStartPos)
{
	

	WorldCollCollideResults results = {0};
	AICivilianPathGenNode **ppNodeRef;
	F32 fYCoord = vStartPos[1];
	GenNodeIndicies nodeIndicies, *pGenNodeIndicies;

	GenNodeIndicies **eaOpenList = NULL;
	
	if (acgGetGridBlockIndiciesFromWorldPos(vStartPos[0], vStartPos[2], &nodeIndicies))
	{
		ppNodeRef = acgIndiciesGetNodeReference(&nodeIndicies);

		if (! acgFloodFill_IsNodeFloodable(*ppNodeRef, iCurIntersectionID))
			return;
	}

	{
		pGenNodeIndicies = malloc(sizeof(GenNodeIndicies)); 
		memcpy(pGenNodeIndicies, &nodeIndicies, sizeof(GenNodeIndicies));
	}
	

	eaPush(&eaOpenList, pGenNodeIndicies);
	while( pGenNodeIndicies = eaPop(&eaOpenList) )
	{
		U32 x;
		Vec3 vNodePos;
		S32 disableFlag;
		EAICivilianLegType curType;

		/*
		pNode = acgGeneratorGetNode(	pGenNodeIndicies->grid_x, pGenNodeIndicies->grid_z, 
												pGenNodeIndicies->block_x, pGenNodeIndicies->block_z, false);
		*/

		acgGetGenNodeIndiciesPosition(pGenNodeIndicies, fYCoord, vNodePos);

		disableFlag = acgCheckForCivilianVolumeLegDisable(vNodePos, false);
		if (disableFlag & DISABLE_ROAD)
		{
			// ignore any that are in a disable road volume
			free(pGenNodeIndicies);
			continue;
		}

		acgUtil_CastVerticalRay(iPartitionIdx, &results, vNodePos, FLOOD_CAST_DIST_UP, FLOOD_CAST_DIST_DOWN);

		curType = aiCivGenClassifyResults(&results);

		if (!acgIsTypeIntersection(&results) && curType != EAICivilianLegType_TROLLEY )
		{
			free(pGenNodeIndicies);
			continue;
		}


		if (acg_d_intersectionMaterial && 
			distance3SquaredXZ(s_acgDebugPos, vNodePos) < SQR(100.0f))
		{
			Vec3 vDebugPos;
			copyVec3(vNodePos, vDebugPos);
			vDebugPos[1] += 2.0f;
			wlAddClientPoint(debugger, vDebugPos, 0xFFFF0000);
		}

		{
			ppNodeRef = acgIndiciesGetNodeReference(pGenNodeIndicies);
			

			acgFloodFill_FreeIfAllocedNode(ppNodeRef, iCurIntersectionID);
			

			*ppNodeRef = U32_TO_PTR(iCurIntersectionID);
		}

		// add the four corners to be processed
		for (x = 0; x < ARRAY_SIZE(offsetsNoCorners); x++)
		{
			GenNodeIndicies *pNeighbor;
			AICivilianPathGenNode *pNeighborNode;
			
			pNeighborNode = acgIndiciesGetNeighbor(	pGenNodeIndicies, 
								offsetsNoCorners[x][0], offsetsNoCorners[x][1], 
								&nodeIndicies);

			if (! acgFloodFill_IsNodeFloodable(pNeighborNode, iCurIntersectionID))
				continue;

			if (curType == EAICivilianLegType_TROLLEY)
			{
				EAICivilianLegType eNeighborType;
				acgGetGenNodeIndiciesPosition(&nodeIndicies, fYCoord, vNodePos);
				acgUtil_CastVerticalRay(iPartitionIdx, &results, vNodePos, FLOOD_CAST_DIST_UP, FLOOD_CAST_DIST_DOWN);
				eNeighborType = aiCivGenClassifyResults(&results);
				if (eNeighborType != EAICivilianLegType_INTERSECTION)
					continue; 
			}

			pNeighbor = malloc(sizeof(GenNodeIndicies));
			devassert(pNeighbor);
			memcpy(pNeighbor, &nodeIndicies, sizeof(GenNodeIndicies));

			eaPush(&eaOpenList, pNeighbor);
		}

		free(pGenNodeIndicies);
	}

	eaDestroy(&eaOpenList);
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static U32 getNodeIntersectionTag(const AICivilianPathLeg *leg, bool bStart)
{
	F32 fDist;
	const F32 *pvPos;
	Vec3 vPos;
	AICivilianPathGenNode *pNode;
	U32 ret;

	if (bStart)
	{
		fDist = -INTERSECTION_TEST_DIST;
		pvPos = leg->start;
	}
	else
	{
		fDist = INTERSECTION_TEST_DIST;
		pvPos = leg->end;
	}

	scaleAddVec3(leg->dir, fDist, pvPos, vPos);
	pNode = acgGeneratorGetNodeByWorld(vPos[0], vPos[2], false);
	ret = PTR_TO_U32(pNode);
	if (ret >= s_iIntersectionID)
		ret = 0;

	return ret;
}

static void acgIntersection_CreateCrosswalk(int iPartitionIdx, const AICivilianPathLeg *pRoadLeg, bool bStart);

// ------------------------------------------------------------------------------------------------------------------
// we need to fix up these legs and make sure they extend to the very edge of the intersection
static void acgIntersectionMaterial_ExtendLegToIntersection(int iPartitionIdx, AICivilianPathLeg *leg, U32 intersectionId, bool bStart)
{
	F32 fStepDir;
	Vec3 vCurPos;
	S32 sanity = (S32)INTERSECTION_TEST_DIST; 
	AICivilianPathGenNode *pNode;
	U32 nodeAsU32;

	if (bStart)
	{
		copyVec3(leg->start, vCurPos);
		fStepDir = -1.0f;
	}
	else
	{
		copyVec3(leg->end, vCurPos);
		fStepDir = 1.0f;
	}

	do 
	{
		scaleAddVec3(leg->dir, fStepDir, vCurPos, vCurPos);

		// check if there is an intersection volume
		{
			WorldVolumeEntry *intersectionVolume = acgCheckForCivilianIntersectionVolume(vCurPos);
			if (intersectionVolume != NULL)
			{
				nodeAsU32 = PTR_TO_U32(intersectionVolume);
				if (nodeAsU32 == intersectionId)
					break;
			}
		}

		pNode = acgGeneratorGetNodeByWorld(vCurPos[0], vCurPos[2], false);
		nodeAsU32 = PTR_TO_U32(pNode);
		if (nodeAsU32 == intersectionId)
		{
			break;
		}
	} while (sanity--);

	// move the leg position to the vCurPos
	if (bStart)
	{
		//copyVec3(vCurPos, leg->start);
		scaleAddVec3(leg->dir, 1.25f, vCurPos, leg->start);
	}
	else
	{
		//copyVec3(vCurPos, leg->end);
		scaleAddVec3(leg->dir, -1.25f, vCurPos, leg->end);
	}
	acgLeg_RecalculateLegDir(leg);

	acgIntersection_CreateCrosswalk(iPartitionIdx, leg, bStart);
	//leg->len = calcLegDir(leg, leg->dir, leg->perp);
}

// ------------------------------------------------------------------------------------------------------------------
// Uses just raycasts, walks forward 
static bool acgUtil_WalkToFindMaterial(int iPartitionIdx, const Vec3 vStartPos, const Vec3 vDir, F32 fStep, F32 fMaxDist, 
									   EAICivilianLegType desiredType, Vec3 vOutpos,
									   AICivilianPathLeg *pTestLegsIgnoreleg)
{
	Vec3 vDirNorm;
	F32 fCurDist = 0.0f;
	Vec3 vCurPos;

	copyVec3(vDir, vDirNorm);
	normalVec3(vDirNorm);


	while(fCurDist <= fMaxDist)
	{
		scaleAddVec3(vDirNorm, fCurDist, vStartPos, vCurPos);

		if (pTestLegsIgnoreleg)
		{
			Vec3 vStart, vEnd;
			
			copyVec3(vCurPos, vStart);
			copyVec3(vCurPos, vEnd);
			vStart[1] += 10.0f;
			vEnd[1] -= 20.0f;
			if (acgTestRayVsLegs(EAICivilianLegType_CAR, vStart, vEnd, pTestLegsIgnoreleg))
				return false;
		}
		

		{	
			WorldCollCollideResults results = {0};
			EAICivilianLegType type;
			
			acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, 10.f, 20.f);
			type = aiCivGenClassifyResults(&results);
			if (type == desiredType)
			{
				copyVec3(results.posWorldImpact, vOutpos);
				return true;
			}

		}
		fCurDist += fStep;
	}



	return false;
}


// ------------------------------------------------------------------------------------------------------------------
static U32 acgIntersectionMaterial_FindIntersection(int iPartitionIdx, AICivilianPathLeg *leg, bool bPostFill, bool bStart, Vec3 pvIsectPos, F32 fStep)
{
	F32 fCurDist = 0.0f;
	Vec3 vCurPos;
	const F32 *pvPos;
	F32 fDir;

	if (bStart)
	{
		fDir = -1.0f;
		pvPos = leg->start;
	}
	else
	{
		fDir = 1.0f;
		pvPos = leg->end;
	}

	if (pvIsectPos) 
		zeroVec3(pvIsectPos);


	while(fCurDist <= INTERSECTION_TEST_DIST)
	{
		Vec3 vStart, vEnd;
		AICivilianPathGenNode *pNode;
		U32 ret;

		scaleAddVec3(leg->dir, (fCurDist * fDir), pvPos, vCurPos);
		
		copyVec3(vCurPos, vStart);
		copyVec3(vCurPos, vEnd);
		vStart[1] += 10.0f;
		vEnd[1] -= 20.0f;
		
		if (acgTestRayVsLegs(EAICivilianLegType_CAR, vStart, vEnd, leg))
		{
			return 0;
		}

		if (!bPostFill)
		{	
			// before the flood fill, we're testing vs the ground material
			
			// ignore anything in a disable road volume or intersection (intersections we ignore for the flood fill
			if ((acgCheckForCivilianVolumeLegDisable(vCurPos, true) & DISABLE_ROAD))
				return 0;
			
			{
				WorldCollCollideResults results = {0};

				acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, FLOOD_CAST_DIST_UP, FLOOD_CAST_DIST_DOWN);
				if (acgIsTypeIntersection(&results))
				{
					pNode = acgGeneratorGetNodeByWorld(vCurPos[0], vCurPos[2], false);
					ret = PTR_TO_U32(pNode);
					if (ret == 0)
					{
						if (pvIsectPos) 
							copyVec3(vCurPos, pvIsectPos);
						return 1;
					}

					if (ret < s_iIntersectionID)
					{	// this was already floodfilled, return false so we don't refill
						return 0;
					}

					//otherwise we hit an edge-node. ignore
				}
			}
		}
		else
		{
			// check if there is an intersection volume
			{
				WorldVolumeEntry *intersectionVolume = acgCheckForCivilianIntersectionVolume(vCurPos);
				if (intersectionVolume != NULL)
				{
					if (pvIsectPos) 
						copyVec3(vCurPos, pvIsectPos);

					return PTR_TO_U32(intersectionVolume);
				}
			}

			pNode = acgGeneratorGetNodeByWorld(vCurPos[0], vCurPos[2], false);
			ret = PTR_TO_U32(pNode);
			if (ret > 0 && ret <= s_iIntersectionID)
			{
				bool bParanoid = true;
				// there seems to be a case where the node and the raycast do not match, 
				// adding a paranoid raycast to determine if there really is intersection at this location
				if (bParanoid)
				{
					WorldCollCollideResults results = {0};

					acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, FLOOD_CAST_DIST_UP, FLOOD_CAST_DIST_DOWN);
					if (acgIsTypeIntersection(&results))
					{
						bParanoid = false;
					}
					else
					{
						
					}
				}

				if (!bParanoid)
				{
					if (pvIsectPos) 
						copyVec3(vCurPos, pvIsectPos);
					return ret;
				}
				
				
			}
		}
	
		
		fCurDist += fStep;
	}

	

	return 0;
}


// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionMaterial_CheckForIntersections(int iPartitionIdx, AICivilianPathLeg *leg, bool bStart)
{
	U32 intersectionId = acgIntersectionMaterial_FindIntersection(iPartitionIdx, leg, true, bStart, NULL, 1.0f);
		
	eaClear(&s_acgProcess.int1.eaLegList);
	eaClear(&s_acgProcess.int1.eaStartLegIntersections);
	eaClear(&s_acgProcess.int1.eaEndLegIntersections);

	if (intersectionId > 0)
	{
		// find all other legs in the area.
		// check if their
		acgPathLegFindNearbyLegs(leg, &s_acgProcess.int1.eaLegList, NULL, NULL, NULL, INTERSECTION_SEARCH_DIST);
		// check if any of the nearby legs found has an end touching this intersection
		{
			S32 x;
			for (x = 0; x < eaSize(&s_acgProcess.int1.eaLegList); x++)
			{
				AICivilianPathLeg *other_leg = s_acgProcess.int1.eaLegList[x];

				if (acg_d_pos && acgDebug_LegNearDebugPos(other_leg))
				{
					int bbb = 0;
				}

				if (other_leg->prev == NULL && !other_leg->doneStart)
				{
					U32 otherIntersectionId = acgIntersectionMaterial_FindIntersection(iPartitionIdx, other_leg, true, true, NULL, 1.0f);
					if (otherIntersectionId == intersectionId)
					{
						eaPush(&s_acgProcess.int1.eaStartLegIntersections, other_leg);
						continue;
					}
				}

				if (other_leg->next == NULL && !other_leg->doneEnd)
				{
					U32 otherIntersectionId = acgIntersectionMaterial_FindIntersection(iPartitionIdx, other_leg, true, false, NULL, 1.0f);
					if (otherIntersectionId == intersectionId)
					{
						eaPush(&s_acgProcess.int1.eaEndLegIntersections, other_leg);
						continue;
					}
				}
			}
		}

		if (eaSize(&s_acgProcess.int1.eaStartLegIntersections) || eaSize(&s_acgProcess.int1.eaEndLegIntersections))
		{
			// we found some legs to make an intersection
			AICivilianPathIntersection *acpi = acgPathIntersection_Alloc();
			PathLegIntersect *pli;
			S32 x;
			
			pli = acgPathLegIntersect_Alloc();
			pli->leg = leg;
			eaPush(&acpi->legIntersects, pli);
			
			if (bStart)
			{ 
				leg->prevInt = acpi;
				leg->doneStart = true;
			}
			else
			{
				leg->nextInt = acpi;
				leg->doneEnd = true;
			}

			for (x = 0; x < eaSize(&s_acgProcess.int1.eaStartLegIntersections); x++)
			{
				AICivilianPathLeg *other_leg = s_acgProcess.int1.eaStartLegIntersections[x];
				
				if (acg_d_pos && acgDebug_LegNearDebugPos(other_leg))
				{
					int bbb = 0;
				}

				pli = acgPathLegIntersect_Alloc();
				pli->leg = other_leg;
				eaPush(&acpi->legIntersects, pli);
				
				other_leg->prevInt = acpi;
				other_leg->doneStart = true;
			}

			for (x = 0; x < eaSize(&s_acgProcess.int1.eaEndLegIntersections); x++)
			{
				AICivilianPathLeg *other_leg = s_acgProcess.int1.eaEndLegIntersections[x];

				if (acg_d_pos && acgDebug_LegNearDebugPos(other_leg))
				{
					int bbb = 0;
				}

				pli = acgPathLegIntersect_Alloc();
				pli->leg = other_leg;
				eaPush(&acpi->legIntersects, pli);

				other_leg->nextInt = acpi;
				other_leg->doneEnd = true;
			}
			

			// add the intersection to the list
			eaPush(&s_acgPathInfo.intersects, acpi);

			// Take all the legs and extend them to make sure they are flush with the intersection
			acgIntersectionMaterial_ExtendLegToIntersection(iPartitionIdx, leg, intersectionId, bStart);

			for (x = 0; x < eaSize(&s_acgProcess.int1.eaStartLegIntersections); x++)
			{
				AICivilianPathLeg *other_leg = s_acgProcess.int1.eaStartLegIntersections[x];
				acgIntersectionMaterial_ExtendLegToIntersection(iPartitionIdx, other_leg, intersectionId, true);
			}

			for (x = 0; x < eaSize(&s_acgProcess.int1.eaEndLegIntersections); x++)
			{
				AICivilianPathLeg *other_leg = s_acgProcess.int1.eaEndLegIntersections[x];
				acgIntersectionMaterial_ExtendLegToIntersection(iPartitionIdx, other_leg, intersectionId, false);
			}

		}
	}
}


static void acgIntersectionCrooked_CorrectLegs(int iPartitionIdx, Entity *debugger);

#define RETAIN_INTERSECTION_HOLDER ((U32)-1)
// ------------------------------------------------------------------------------------------------------------------
// This runs before the normal acgProcessIntersections, this will connect all legs that connect to the same intersection
// AT the moment, this step pertains only to AI_CIV_CAR, but it can be used for AI_CIV_PERSON types if needed
static void acgConnection_ProcessIntersectionsByMaterial(int iPartitionIdx, Entity *debugger)
{
	S32 i;

	s_iIntersectionID = 2;

	// flood fill intersections
	for (i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		Vec3 vTestPos;

		if (leg->type != EAICivilianLegType_CAR)
			continue;

		if (acg_d_pos && acgDebug_LegNearDebugPos(leg))
		{
			int xxx = 0;
		}
		
		if (leg->prev == NULL)
		{
			if (acgIntersectionMaterial_FindIntersection(iPartitionIdx, leg, false, true, vTestPos, 1.0f))
			{
				// flood fill this area 
				acgFloodFillAreaForIntersections(iPartitionIdx, debugger, s_iIntersectionID, vTestPos);
				s_iIntersectionID++;
			}
		}

		if (leg->next == NULL)
		{
			if (acgIntersectionMaterial_FindIntersection(iPartitionIdx, leg, false, false, vTestPos, 1.0f))
			{
				// flood fill this area 
				acgFloodFillAreaForIntersections(iPartitionIdx, debugger, s_iIntersectionID, vTestPos);
				s_iIntersectionID++;
			}
		}
	}

	// all intersections have been flood-filled.
	// now we can connect all legs that are sharing the same intersection
	for (i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->type != EAICivilianLegType_CAR)
			continue;

		if (acg_d_pos && acgDebug_LegNearDebugPos(leg))
		{
			int xxx = 0;
		}

		if (leg->prev == NULL && !leg->doneStart)
		{
			acgIntersectionMaterial_CheckForIntersections(iPartitionIdx, leg, true);
		}

		if (leg->next == NULL && !leg->doneEnd)
		{
			acgIntersectionMaterial_CheckForIntersections(iPartitionIdx, leg, false);			
		}

	}

	eaDestroy(&s_acgProcess.int1.eaLegList);
	eaDestroy(&s_acgProcess.int1.eaStartLegIntersections);
	eaDestroy(&s_acgProcess.int1.eaEndLegIntersections);

	// 
	if (0)
	{
		acgIntersectionCrooked_CorrectLegs(iPartitionIdx, debugger);
	}

	// now clean up the blocks 
	{
		S32 numNodeBlocks;
		U32 nodeAsU32;

		numNodeBlocks = (s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1) *
						(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]+1);

		for (i = 0; i < numNodeBlocks; i++)
		{
			AICivilianPathGenNodeBlock *block = s_acgNodeBlocks.aiCivGenNodeBlocks[i];
			if (block)
			{
				S32 x;
				for (x = 0; x < NODE_BLOCK_SIZE; x++)
				{
					S32 z;
					for (z = 0; z < NODE_BLOCK_SIZE; z++)
					{
						nodeAsU32 = PTR_TO_U32(block->nodes[x][z]);
						if ((nodeAsU32 > 0 && nodeAsU32 <= s_iIntersectionID) || 
							(nodeAsU32 == RETAIN_INTERSECTION_HOLDER))
						{
							block->nodes[x][z] = NULL;
						}
					}
				}
			}
		}
	}

}


// ------------------------------------------------------------------------------------------------------------------
static bool acgIntersectionCrooked_IsNodeOnEdge(GenNodeIndicies *pNodeIndicies)
{
	S32 i;
	
	for(i = 0; i < ARRAY_SIZE(offsetsNoCorners); i++)
	{
		U32 nodeAsU32;
		GenNodeIndicies neighborIndicies;
		
		AICivilianPathGenNode *neighbor = acgIndiciesGetNeighbor(pNodeIndicies, offsetsNoCorners[i][0], offsetsNoCorners[i][1], &neighborIndicies);
		
		nodeAsU32 = PTR_TO_U32(neighbor);
		if (nodeAsU32 != RETAIN_INTERSECTION_HOLDER && (nodeAsU32 == 0 || nodeAsU32 > s_iIntersectionID))
			return true;
	}
	
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionCrooked_RetainEdgeNodes(Entity *debugger)
{
	S32 i, numNodeBlocks;
	U32 nodeAsU32;

	numNodeBlocks = (s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1) *
					(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]+1);

	for (i = 0; i < numNodeBlocks; i++)
	{
		S32 x, z;
		AICivilianPathGenNodeBlock *block = s_acgNodeBlocks.aiCivGenNodeBlocks[i];
		if (! block)
			continue;

		for (x = 0; x < NODE_BLOCK_SIZE; x++)
		{
			for (z = 0; z < NODE_BLOCK_SIZE; z++)
			{
				AICivilianPathGenNode *pNode = block->nodes[x][z];
				nodeAsU32 = PTR_TO_U32(pNode);
				if (nodeAsU32 > 0 && nodeAsU32 <= s_iIntersectionID)
				{
					GenNodeIndicies nodeIndicies;
					nodeIndicies.grid_x = block->grid_x;
					nodeIndicies.grid_z = block->grid_z; 
					nodeIndicies.block_x = x;
					nodeIndicies.block_z = z;
					if (! acgIntersectionCrooked_IsNodeOnEdge(&nodeIndicies))
					{
						block->nodes[x][z] = U32_TO_PTR(RETAIN_INTERSECTION_HOLDER);
					}
					else if (acg_d_intersectionCorrecting)
					{	// debug drawing
						Vec3 vNodePos;
						acgGetGenNodeIndiciesPosition(&nodeIndicies, s_acgDebugPos[1], vNodePos);

						//if (distance3SquaredXZ(s_acgDebugPos, vNodePos) < SQR(100.0f))
						{
							AICivilianPathLeg *pLeg;

							pLeg = acgPathFindNearestLeg(vNodePos, EAICivilianLegType_CAR);
							if (pLeg)
							{
								vNodePos[1] = interpF32(0.5f, pLeg->start[1], pLeg->end[1]);
							}
							vNodePos[1] += 2.0f;
							wlAddClientPoint(debugger, vNodePos, 0xFFFF0000);
						}
					}
				}
			}
		}
	}
}

typedef enum EMainOffset
{
	EMainOffset_NONE = 0,
	EMainOffset_CLOCKWISE,
	EMainOffset_CCW,
	EMainOffset_COUNT
} EMainOffset;

#define RECURSE_DEPTH_MAX 8

// ------------------------------------------------------------------------------------------------------------------
// idx should be in the range [0 - 2] 
static void acgIntersectionCrooked_GetOffset(S32 mainOffsetIdx, EMainOffset eOffset, S32 *piOffX, S32 *piOffZ)
{
	devassert(mainOffsetIdx >= 0 && mainOffsetIdx < ARRAY_SIZE(offsetsCorners));
	
	if (eOffset == EMainOffset_CLOCKWISE)
	{
		mainOffsetIdx++;
		if (mainOffsetIdx >= ARRAY_SIZE(offsetsCorners))
			mainOffsetIdx = 0;
	}
	else if (eOffset == EMainOffset_CCW)
	{
		mainOffsetIdx--;
		if (mainOffsetIdx < 0)
			mainOffsetIdx = ARRAY_SIZE(offsetsCorners) - 1;
	}

	*piOffX = offsetsCorners[mainOffsetIdx][0];
	*piOffZ = offsetsCorners[mainOffsetIdx][1];
}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionCrooked_CollectLineNodes(GenNodeIndicies *idx, U32 isectionID, S32 mainOffsetIdx, S32 count)
{
	S32 i;
	Vec3 vNodePos;

	// push the node's 2d position onto our list
	acgGetGenNodeIndiciesPosition(idx, 0.0f, vNodePos);
	vNodePos[1] = vNodePos[2];
	eafPush2(&s_acgProcess.inter.eaNodeLinePos, vNodePos);
	
	if (count > RECURSE_DEPTH_MAX)
		return;

	for( i = 0; i < EMainOffset_COUNT; i++)
	{
		S32 iOffX, iOffZ;
		GenNodeIndicies neighborIdx;
		AICivilianPathGenNode *pNeighbor;

		acgIntersectionCrooked_GetOffset(mainOffsetIdx, i, &iOffX, &iOffZ);
		
		pNeighbor = acgIndiciesGetNeighbor(idx, iOffX, iOffZ, &neighborIdx);
		if (isectionID == PTR_TO_U32(pNeighbor))
		{
			acgIntersectionCrooked_CollectLineNodes(&neighborIdx, isectionID, mainOffsetIdx, count+1);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionCrooked_FindFurthestPoint(const F32 *eavec2, const Vec2 vBasePos, Vec2 vFurthest)
{
	S32 x, numPts;
	F32 fFurthestDistSq = -FLT_MAX;
	
	numPts = eafSize(&eavec2) / 2;

	fFurthestDistSq = -FLT_MAX;
	for (x = 0; x < numPts; x++)
	{
		Vec2 v2Pt;
		F32 fDistSq;

		v2Pt[0] = eavec2[x*2];
		v2Pt[1] = eavec2[x*2 + 1];

		fDistSq = distance2Squared(vBasePos, v2Pt);
		if (fDistSq > fFurthestDistSq)
		{
			fFurthestDistSq = fDistSq;
			copyVec2(v2Pt, vFurthest);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgIntersectionCrooked_GetIntersectionIDLineDir(const Vec3 vNodePos, Vec3 vDir)
{
	GenNodeIndicies indicies;
	AICivilianPathGenNode *pNode = acgGeneratorGetNodeByWorld(vNodePos[0], vNodePos[2], false);
	U32 isectionID = PTR_TO_U32(pNode);
	S32 i;
	
	eafClear(&s_acgProcess.inter.eaNodeLinePos);

	acgGetGridBlockIndiciesFromWorldPos(vNodePos[0], vNodePos[2], &indicies);
	
	zeroVec3(vDir);

	
	for(i=0; i<ARRAY_SIZE(offsetsCorners); i++)
	{
		GenNodeIndicies neighborIdx;
		AICivilianPathGenNode *pNeighbor = acgIndiciesGetNeighbor(&indicies, offsetsCorners[i][0], offsetsCorners[i][1], &neighborIdx);
		if (isectionID == PTR_TO_U32(pNeighbor))
		{
			acgIntersectionCrooked_CollectLineNodes(&neighborIdx, isectionID, i, 1);
		}
	}

	// we've collected the nodes, get the extents of the line and then get the direction 
	if (eafSize(&s_acgProcess.inter.eaNodeLinePos))
	{
		Vec2 v2NodePos;
		Vec2 v2BestPosFirst, v2BestPosSecond, v2Dir;

		v2NodePos[0] = vNodePos[0];
		v2NodePos[1] = vNodePos[2];

		acgIntersectionCrooked_FindFurthestPoint(s_acgProcess.inter.eaNodeLinePos, v2NodePos, v2BestPosFirst);
		acgIntersectionCrooked_FindFurthestPoint(s_acgProcess.inter.eaNodeLinePos, v2BestPosFirst, v2BestPosSecond);
		

		subVec2(v2BestPosFirst, v2BestPosSecond, v2Dir);
		vDir[0] = v2Dir[0];
		vDir[1] = 0.0f;
		vDir[2] = v2Dir[1];
		normalVec3(vDir);

		if (acg_d_intersectionCorrecting)
		{
			Entity *ent = entFromEntityRefAnyPartition(aiCivDebugRef);
			if (ent)
			{
				Vec3 vNode1, vNode2;
				AICivilianPathLeg *pLeg;

				pLeg = acgPathFindNearestLeg(vNodePos, EAICivilianLegType_CAR);
				setVec3(vNode1, v2BestPosFirst[0], s_acgDebugPos[1]+1, v2BestPosFirst[1]);
				setVec3(vNode2, v2BestPosSecond[0], s_acgDebugPos[1]+1, v2BestPosSecond[1]);
				if (pLeg)
				{
					vNode1[1] = interpF32(0.5f, pLeg->start[1], pLeg->end[1]) + 2.f;
					vNode2[1] = interpF32(0.5f, pLeg->start[1], pLeg->end[1]) + 2.f;
				}

				wlAddClientLine(ent, vNode1, vNode2, 0xFFFF0000);
			}
		}

		return true;
	}
	

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionCrooked_GetNewLegCenter(int iPartitionIdx, const AICivilianPathLeg *leg, const Vec3 vPerpDirection, bool bStart, Vec3 vCenter)
{
	Vec3 vStartPos, vCurPos, vEndPos1, vEndPos2;
	F32 fStep, fCurDist;
	bool bHasMedian = false;
	S32 medianCount;
	S32 sanity;
	

	
#define CENTER_FIND_START_OFFSET (3.0f)
	if (bStart)
	{
		scaleAddVec3(leg->dir, CENTER_FIND_START_OFFSET, leg->start, vStartPos);
	}
	else
	{
		scaleAddVec3(leg->dir, -CENTER_FIND_START_OFFSET, leg->end, vStartPos);
	}

	if (leg->median_width != 0.0f)
	{
		bHasMedian = true;
	}

	if (bHasMedian)
	{
		// find the median
		// check if we are starting on a median.
		WorldCollCollideResults results = {0};

		fCurDist = 1.0f;
		sanity = 50;
		do {
			
			scaleAddVec3(vPerpDirection, fCurDist, vStartPos, vCurPos);
			acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, 10.0f, 20.0f);
			if (EAICivilianLegType_CAR != aiCivGenClassifyResults(&results))
			{
				copyVec3(vCurPos, vStartPos);
				break;
			}

			scaleAddVec3(vPerpDirection, -fCurDist, vStartPos, vCurPos);
			acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, 10.0f, 20.0f);
			if (EAICivilianLegType_CAR != aiCivGenClassifyResults(&results))
			{
				copyVec3(vCurPos, vStartPos);
				break;
			}

			fCurDist += 1.0f;
		} while(--sanity);

		
		
	}

	medianCount = 0;
	fStep = 1.0f;
	fCurDist = 0.0f;
	sanity = 100;
	do 
	{
		WorldCollCollideResults results = {0};
		scaleAddVec3(vPerpDirection, fCurDist, vStartPos, vCurPos);
		acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, 10.0f, 20.0f);
		
		if (EAICivilianLegType_CAR != aiCivGenClassifyResults(&results))
		{
			if (bHasMedian)
			{
				if (medianCount == 1) 
					break;
			}
			else
			{
				break;
			}
		}
		else if (bHasMedian)
		{
			medianCount = 1;
		}



		fCurDist += fStep;
	} while (--sanity);
	copyVec3(vCurPos, vEndPos1);

	medianCount = 0;
	fStep = -1.0f;
	fCurDist = 0.0f;
	sanity = 100;
	do 
	{
		WorldCollCollideResults results = {0};
		scaleAddVec3(vPerpDirection, fCurDist, vStartPos, vCurPos);
		acgUtil_CastVerticalRay(iPartitionIdx, &results, vCurPos, 10.0f, 20.0f);

		if (EAICivilianLegType_CAR != aiCivGenClassifyResults(&results))
		{
			if (bHasMedian)
			{
				if (medianCount == 1) 
					break;
			}
			else
			{
				break;
			}
		}
		else if (bHasMedian)
		{
			medianCount = 1;
		}
		fCurDist += fStep;
	} while (--sanity);
	copyVec3(vCurPos, vEndPos2);
	
	interpVec3(0.5f, vEndPos1, vEndPos2, vCenter);
}


// ------------------------------------------------------------------------------------------------------------------
static U32 acgIntersectionCrooked_FindIntersection(AICivilianPathLeg *leg, bool bStart, Vec3 vIsectPos)
{
	#define SEARCH_THRESHOLD_DISTANCE_SQ	(SQR(4.0f))

	GenNodeIndicies *pGenNode, genNode;
	S32 x;
	U32 uNodeId = 0;
	const F32 *pvStartPos;
	
	if (bStart) pvStartPos = leg->start;
	else pvStartPos = leg->end;
	
	if (! acgGetGridBlockIndiciesFromWorldPos(pvStartPos[0], pvStartPos[2], &genNode))
		return 0;
	
	pGenNode = malloc(sizeof(GenNodeIndicies));
	memcpy(pGenNode, &genNode, sizeof(GenNodeIndicies));
	eaPush(&s_acgProcess.inter.eaGenNodes, pGenNode);

	for (x = 0; x < eaSize(&s_acgProcess.inter.eaGenNodes); x++)
	{
		S32 i;
		AICivilianPathGenNode** ppPathGenNode;
		
		pGenNode = s_acgProcess.inter.eaGenNodes[x];
		
		ppPathGenNode = acgIndiciesGetNodeReference(pGenNode);
		
		uNodeId = PTR_TO_U32((*ppPathGenNode));
		if (uNodeId > 0 && uNodeId <= s_iIntersectionID)
		{
			acgGetGenNodeIndiciesPosition(pGenNode, 0.0f, vIsectPos);
			break;
		}

		for( i = 0; i < ARRAY_SIZE(offsetsNoCorners); i++)
		{
			S32 idx;
			Vec3 vNodeWorldPos;

			acgIndiciesGetNeighbor(pGenNode, offsetsNoCorners[i][0], offsetsNoCorners[i][1], &genNode);
			
			acgGetGenNodeIndiciesPosition(&genNode, 0.0f, vNodeWorldPos);
			if (distance3SquaredXZ(vNodeWorldPos, pvStartPos) > SEARCH_THRESHOLD_DISTANCE_SQ)
				continue; // too far
			
			if ((acgCheckForCivilianVolumeLegDisable(vNodeWorldPos, true) & DISABLE_ROAD))
				return -1;
						
			idx = eaFindCmp(&s_acgProcess.inter.eaGenNodes, &genNode, GenNodeIndiciesCompare);
			if (idx == -1)
			{
				GenNodeIndicies *pNewNode;
				pNewNode = malloc(sizeof(GenNodeIndicies));
				memcpy(pNewNode, &genNode, sizeof(GenNodeIndicies));
				eaPush(&s_acgProcess.inter.eaGenNodes, pNewNode);
			}
		}

		
	}
	

	eaClearEx(&s_acgProcess.inter.eaGenNodes, NULL);
	return uNodeId;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersection_CreateCrosswalk(int iPartitionIdx, const AICivilianPathLeg *pRoadLeg, bool bStart)
{
	AICivilianPathLeg	*pXWalk = NULL;
	Vec3 vXWalkCenterPos;
	const F32 *pvLegPos;
	F32 fDist;

	pvLegPos = acgLeg_GetStartEndPosAndDir(pRoadLeg, bStart, &fDist);
	
	fDist *= s_acg_fCrosswalkDefaultWidth * 0.5f + 1.f;

	scaleAddVec3(pRoadLeg->dir, fDist, pvLegPos, vXWalkCenterPos);
	{
		Vec3 vStartCast, vEndCast;
		copyVec3(vXWalkCenterPos, vStartCast);
		copyVec3(vXWalkCenterPos, vEndCast);

		vStartCast[1] += 10;
		vEndCast[1] -= 10;

		if (acgTestRayVsLegs(EAICivilianLegType_PERSON, vStartCast, vEndCast, NULL))
		{
			// there is already a crosswalk here, maybe a forced volume...
			return; 
		}
	}

	pXWalk = acgPathLeg_Alloc();
	eaPush(&s_acgProcess.legList, pXWalk);
	
	pXWalk->type = EAICivilianLegType_PERSON;
	pXWalk->bIsCrosswalk = true;
	pXWalk->bForcedLegAsIs = true;
	pXWalk->bIsForcedLeg = true;
	pXWalk->width = s_acg_fCrosswalkDefaultWidth;
	
	scaleAddVec3(pRoadLeg->dir, fDist, pvLegPos, vXWalkCenterPos);
	
	{
		F32 fCrosswalkHalfLength;
		Vec3 vCrossWalkDir;

		if (bStart && pRoadLeg->bSkewed_Start)
		{
			fCrosswalkHalfLength = acgLeg_GetSkewedLaneLength(pRoadLeg);

			fCrosswalkHalfLength = ABS(fCrosswalkHalfLength);
			acgLeg_GetSkewedLaneDirection(pRoadLeg, vCrossWalkDir);
			
		}
		else
		{
			fCrosswalkHalfLength = pRoadLeg->width * 0.5f;
			copyVec3(pRoadLeg->perp, vCrossWalkDir);
		}
		scaleAddVec3(vCrossWalkDir, fCrosswalkHalfLength, vXWalkCenterPos, pXWalk->start);
		scaleAddVec3(vCrossWalkDir, -fCrosswalkHalfLength, vXWalkCenterPos, pXWalk->end);
		acgLeg_RecalculateLegDir(pXWalk);
	}
	

	// extend the crosswalk to the sidewalk
	{
		Vec3 vDir, vExtendToPos;
		bool bRet;

		copyVec3(pXWalk->dir, vDir);

		// end pos
		bRet = acgUtil_WalkToFindMaterial(	iPartitionIdx,
											pXWalk->end, 
											vDir, 
											0.5f, 15.f, 
											EAICivilianLegType_PERSON, 
											vExtendToPos, NULL);
		if (bRet)
		{
			scaleAddVec3(vDir, 1.f, vExtendToPos, pXWalk->end);
		}

		// start pos
		scaleVec3(vDir, -1.f, vDir);		
		bRet = acgUtil_WalkToFindMaterial(	iPartitionIdx,
											pXWalk->start, 
											vDir, 
											0.5f, 15.f, 
											EAICivilianLegType_PERSON, 
											vExtendToPos, NULL);
		if (bRet)
		{
			scaleAddVec3(vDir, 1.f, vExtendToPos, pXWalk->start);
			//copyVec3(vExtendToPos, pXWalk->start);
		}

		acgLeg_RecalculateLegDir(pXWalk);

	}
	

}


// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionCrooked_CheckAndCorrectIfNeeded(int iPartitionIdx, AICivilianPathLeg *leg, bool bStart)
{
	Vec3 vNodePos, vPerpDirection, vCenterPos;
	AICivilianPathLeg *pIntersectionLeg = NULL;
	
	U32 intersectionId = acgIntersectionCrooked_FindIntersection(leg, bStart, vNodePos);

	if (intersectionId == 0)
	{
		F32 *pvPos = (bStart) ? leg->start : leg->end;
		printf("\n\tIC: Could not find intersection node at pos: (%.1f, %.1f, %.1f)", pvPos[0], pvPos[1], pvPos[2]);
		// acgIntersection_CreateCrosswalk(leg, bStart);
		return;
	}

	if (intersectionId <= s_iIntersectionID)
	{
		// get the direction from the node line direction
		acgIntersectionCrooked_GetIntersectionIDLineDir(vNodePos, vPerpDirection);
	}
	else
	{
		// 
		return;
	}

	if (acg_d_pos && acgDebug_LegNearDebugPos(leg))
	{
		int bbb = 0;
	}
	
	acgIntersectionCrooked_GetNewLegCenter(iPartitionIdx, leg, vPerpDirection, bStart, vCenterPos);

	pIntersectionLeg = leg;

	// check to see if this leg is perpendicular enough to the intersection
	{
		Vec3 vLegDirXZ;
		F32 fAngle, fAngleDiff;

		copyVec3XZ(leg->dir, vLegDirXZ);
		vLegDirXZ[1] = 0.f;
		normalVec3(vLegDirXZ);

		fAngle = dotVec3(vLegDirXZ, vPerpDirection);
		fAngle = acosf(CLAMP(fAngle, -1.f, 1.f));

		fAngleDiff = fAngle - HALFPI;
		fAngleDiff = ABS(fAngleDiff);
		
	#define ANGLE_THRESHOLD	(RAD(9.f))
		if (fAngleDiff > ANGLE_THRESHOLD)
		{
			F32 *pvPos = (bStart) ? leg->start : leg->end;
			printf("\n\tIC: Leg needs adjustment (%.1f degrees off): (%.1f, %.1f, %.1f)", DEG(fAngleDiff), pvPos[0], pvPos[1], pvPos[2]);

			{
		#define NEW_LEG_DEFAULT_LENGTH 16
		#define OLD_LEG_LEN_THRESHOLD  70
		#define OLD_LEG_TRUNCATE_AMOUNT 60
				AICivilianPathLeg  *newLeg;
				Vec3 vLegDirection;
				F32 fNewLength;
				AICivilianPathIntersection *acpi;
				
				crossVec3Up(vPerpDirection, vLegDirection);
				if (dotVec3(vLegDirection, leg->dir) < 0.f)
				{
					scaleVec3(vLegDirection, -1.f, vLegDirection);
				}

				fNewLength = MIN(leg->len, NEW_LEG_DEFAULT_LENGTH);


				newLeg = acgPathLeg_Alloc();
				eaPush(&s_acgProcess.legList, newLeg);
				pIntersectionLeg = newLeg;

				newLeg->type = leg->type;
				newLeg->median_width = leg->median_width;
				newLeg->width = leg->width;

				// initialize the new leg
				if (bStart)
				{
					// set the new leg's positions
					copyVec3(vCenterPos, newLeg->start);
					scaleAddVec3(vLegDirection, fNewLength, newLeg->start, newLeg->end);
					
					// exchange the intersection
					newLeg->prevInt = leg->prevInt;
					acpi = leg->prevInt;
					leg->prevInt = NULL;
				}
				else
				{
					copyVec3(vCenterPos, newLeg->end);
					scaleAddVec3(vLegDirection, -fNewLength, newLeg->end, newLeg->start);

					newLeg->nextInt = leg->nextInt;
					acpi = leg->nextInt;
					leg->nextInt = NULL;
				}

				// change the PathLegIntersect on this intersection to the new leg
				{
					PathLegIntersect *pli;
					S32 idx = acgFindPathLegInACPI(acpi, leg);
					devassert(idx != -1);
					pli = acpi->legIntersects[idx];
					pli->leg = newLeg;
				}
				
				//newLeg->len = calcLegDir(newLeg, newLeg->dir, newLeg->perp);
				acgLeg_RecalculateLegDir(newLeg);

				// truncate the old leg
				if (leg->len < OLD_LEG_LEN_THRESHOLD)
				{
					leg->deleted = s_acgProcess.state;
					leg->deleteReason = "Crooked leg, too short";
					return;
				}

				if (bStart)
				{
					scaleAddVec3(leg->dir, OLD_LEG_TRUNCATE_AMOUNT, leg->start, leg->start );
				}
				else
				{
					scaleAddVec3(leg->dir, -OLD_LEG_TRUNCATE_AMOUNT, leg->end, leg->end );
				}
				//leg->len = calcLegDir(leg, leg->dir, leg->perp);
				acgLeg_RecalculateLegDir(leg);
				
				
			}

		}


	}

	// 
	// create a crosswalk here
	acgIntersection_CreateCrosswalk(iPartitionIdx, pIntersectionLeg, bStart);


}

// ------------------------------------------------------------------------------------------------------------------
// This corrects any legs that connect with an intersection but are not near perpendicular to it
static void acgIntersectionCrooked_CorrectLegs(int iPartitionIdx, Entity *debugger)
{
	S32 i;
	// first take the flood-filled nodeblocks and find the edges
	acgIntersectionCrooked_RetainEdgeNodes(debugger);

	for (i = eaSize(&s_acgProcess.legList) - 1; i >= 0; i--)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->type != EAICivilianLegType_CAR)
			continue;

		if (leg->prevInt)
		{
			acgIntersectionCrooked_CheckAndCorrectIfNeeded(iPartitionIdx, leg, true);
		}

		if (leg->nextInt)
		{
			acgIntersectionCrooked_CheckAndCorrectIfNeeded(iPartitionIdx, leg, false);
		}
		
		if (leg->deleted)
		{
			acgDestroyLeg(&leg, false);
		}
	}
	
	eafDestroy(&s_acgProcess.inter.eaNodeLinePos);
	eaDestroy(&s_acgProcess.inter.eaGenNodes);
	acgProcess_RemoveDeletedLegs();
}

// ------------------------------------------------------------------------------------------------------------------
#define END_CLOSE_DIST_SQ	(SQR(1.0f))

static void acgIntersection_DeterminePlugsViaDist(const AICivilianPathLeg *pLeg, EConnectionPlug *peLeg, 
													const AICivilianPathLeg *pOtherLeg, EConnectionPlug *peOtherLeg)
{
	Vec3 vClosestLegPt, vClosestOtherPt;
	F32 fDistSq;

	fDistSq = acgLegLegDistSquared(pLeg, vClosestLegPt, pOtherLeg, vClosestOtherPt);
	
	if (distance3SquaredXZ(vClosestLegPt, pLeg->start) <= END_CLOSE_DIST_SQ)
	{
		*peLeg = EConnectionPlug_START;
	}
	else if (distance3SquaredXZ(vClosestLegPt, pLeg->end) <= END_CLOSE_DIST_SQ)
	{
		*peLeg = EConnectionPlug_END;
	}
	else
	{
		*peLeg = EConnectionPlug_MID;
	}


	if (distance3SquaredXZ(vClosestOtherPt, pOtherLeg->start) <= END_CLOSE_DIST_SQ)
	{
		*peOtherLeg = EConnectionPlug_START;
	}
	else if (distance3SquaredXZ(vClosestOtherPt, pOtherLeg->end) <= END_CLOSE_DIST_SQ)
	{
		*peOtherLeg = EConnectionPlug_END;
	}
	else
	{
		*peOtherLeg = EConnectionPlug_MID;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgIntersection_CanLegConnectToIntersection(int iPartitionIdx, const AICivilianPathLeg *pLeg, const AICivilianPathLeg *pOtherLeg, 
														const AICivilianPathIntersection *acpi)
{
	S32 x, num;
	if (acpi->bIsMidIntersection) // cannot connect onto a mid intersection
		return false; 
	
	num = eaSize(&acpi->legIntersects);
	for (x = 0; x < num; x++)
	{
		PathLegIntersect *pli = acpi->legIntersects[x];

		if (pli->leg == pOtherLeg)
			continue; // ignore the other leg, we assume we've already checked visibility

		if(!acgLegIsVisible(iPartitionIdx, pLeg, pli->leg) || !acgLegIsVisible(iPartitionIdx, pli->leg, pLeg))
			return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgIntersection_CanLegsConnect(int iPartitionIdx, const AICivilianPathLeg *pLeg, EConnectionPlug eExpectedLegPlug, const AICivilianPathLeg *pOtherLeg)
{
	EConnectionPlug eLegPlug, eOtherLegPlug;
	AICivilianPathIntersection *legAcpi = NULL;
	AICivilianPathIntersection *otherLegAcpi = NULL;

	// 1. check if the legs can see each other.
	// 2. if the other leg already has a connection on the desired plug, 
	//		check to see if all the legs can see each other

	if (pLeg->bIsCrosswalk && pOtherLeg->bIsCrosswalk)
		return false; // don't let two crosswalks connect

	if(!acgLegIsVisible(iPartitionIdx, pLeg, pOtherLeg) || !acgLegIsVisible(iPartitionIdx, pOtherLeg, pLeg))
		return false;

	// find out which end these two will be connected
	acgIntersection_DeterminePlugsViaDist(pLeg, &eLegPlug, pOtherLeg, &eOtherLegPlug );
	
	if (eExpectedLegPlug != eLegPlug)
		return false; // only the expected plugs 

	if (eLegPlug == EConnectionPlug_START)
	{
		legAcpi = pLeg->prevInt;
	}
	else if (eLegPlug == EConnectionPlug_END)
	{
		legAcpi = pLeg->nextInt;
	}

	// check if the other leg has a intersection, and if it does
	// we'll need to check if this leg can see each other leg in that intersection.
	if (eOtherLegPlug == EConnectionPlug_START)
	{
		otherLegAcpi = pOtherLeg->prevInt;
	}
	else if (eOtherLegPlug == EConnectionPlug_END)
	{
		otherLegAcpi = pOtherLeg->nextInt;
	}

	if (legAcpi && otherLegAcpi)
	{
		if (legAcpi->bIsMidIntersection)
			return false;

		FOR_EACH_IN_EARRAY(legAcpi->legIntersects, PathLegIntersect, pli) 
			if (! acgIntersection_CanLegConnectToIntersection(iPartitionIdx, pli->leg, NULL, otherLegAcpi))
				return false;
		FOR_EACH_END
	}
	else if (legAcpi)
	{
		return acgIntersection_CanLegConnectToIntersection(iPartitionIdx, pOtherLeg, pLeg, legAcpi);
	}
	else if (otherLegAcpi)
	{
		return acgIntersection_CanLegConnectToIntersection(iPartitionIdx, pLeg, pOtherLeg, otherLegAcpi);
	}

	
	// allow EConnectionPlug_MID
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersection_CombineIntersections(AICivilianPathLeg *pLeg, AICivilianPathIntersection **ppLegIsect,
												 AICivilianPathLeg *pOtherLeg, AICivilianPathIntersection **ppOtherLegIsect)
{
	if (*ppLegIsect && *ppOtherLegIsect)
	{
		// merge the two isects
		
		AICivilianPathIntersection *acpiMergeTo;
		AICivilianPathIntersection *acpiToDelete;

		if ( *ppLegIsect == *ppOtherLegIsect || 
			 (*ppLegIsect)->bIsMidIntersection || (*ppOtherLegIsect)->bIsMidIntersection)
			return; // already connected, same intersection

		acpiMergeTo = *ppLegIsect;
		acpiToDelete = *ppOtherLegIsect;

		{
			S32 x;
			// move the leg intersects from the delete one over to the merge one
			x = eaSize(&acpiToDelete->legIntersects) - 1;
			do {
				PathLegIntersect *pli = acpiToDelete->legIntersects[x];
				if (pli->leg != pLeg)
				{
					if (pli->leg->nextInt == acpiToDelete)
						pli->leg->nextInt = acpiMergeTo;
					if (pli->leg->prevInt == acpiToDelete)
						pli->leg->prevInt = acpiMergeTo;

					eaPush(&acpiMergeTo->legIntersects, pli);
				}
				else
				{	// do not duplicate the 
					acgPathLegIntersect_Free(pli);
				}
			} while (--x >= 0);
		} 
		
		//eaPushEArray(&acpiMergeTo->legIntersects, &acpiToDelete->legIntersects);
		eaClear(&acpiToDelete->legIntersects);

		eaFindAndRemoveFast(&s_acgPathInfo.intersects, acpiToDelete);
		acgPathIntersection_Free(acpiToDelete);
	}
	else if (*ppLegIsect)
	{
		if (!(*ppLegIsect)->bIsMidIntersection)
		{
			PathLegIntersect *pli = acgPathLegIntersect_Alloc();

			pli->leg = pOtherLeg;
			eaPush(&(*ppLegIsect)->legIntersects, pli);

			*ppOtherLegIsect = *ppLegIsect;
		}
		
	}
	else if (*ppOtherLegIsect)
	{
		if (!(*ppOtherLegIsect)->bIsMidIntersection)
		{
			PathLegIntersect *pli = acgPathLegIntersect_Alloc();

			pli->leg = pLeg;
			eaPush(&(*ppOtherLegIsect)->legIntersects, pli);

			*ppLegIsect = *ppOtherLegIsect;
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgIntersection_ConnectLegs(AICivilianPathLeg *pLeg, S32 bStart, AICivilianPathLeg *pOtherLeg)
{
	EConnectionPlug eLegPlug, eOtherLegPlug;
	AICivilianPathIntersection **ppLegIsect, **ppOtherLegIsect = NULL;
	
	
	acgIntersection_DeterminePlugsViaDist(pLeg, &eLegPlug, pOtherLeg, &eOtherLegPlug );
	devassert( ((bStart) ? EConnectionPlug_START : EConnectionPlug_END) == eLegPlug);

	if (bStart) ppLegIsect = &pLeg->prevInt;
	else ppLegIsect = &pLeg->nextInt;
	
	if (eOtherLegPlug == EConnectionPlug_START)
	{
		ppOtherLegIsect = &pOtherLeg->prevInt;
	}
	else if (eOtherLegPlug == EConnectionPlug_END)
	{
		ppOtherLegIsect = &pOtherLeg->nextInt;
	}
	else 
	{
		AICivilianPathIntersection *acpi;
		PathLegIntersect *pli;
		// mid intersection

		if (*ppLegIsect)
		{	// this leg already has an intersection of another type
			// cannot connect
			return;
		}

		devassert(eOtherLegPlug == EConnectionPlug_MID);

		acpi = acgPathIntersection_Alloc();
		eaPush(&s_acgPathInfo.intersects, acpi);

		acpi->bIsMidIntersection = true;
		// add the leg intersect pos first
		pli = acgPathLegIntersect_Alloc();
		pli->leg = pLeg;
		acgLegLegDistSquared(pLeg, NULL, pOtherLeg, pli->intersect);
		eaPush(&acpi->legIntersects, pli);

		// add the edge leg next
		pli = acgPathLegIntersect_Alloc();
		pli->leg = pOtherLeg;
		eaPush(&acpi->legIntersects, pli);

		// 
		*ppLegIsect = acpi;
		eaPush(&pOtherLeg->midInts, acpi);
		return;
	}



	if (*ppOtherLegIsect || *ppLegIsect)
	{
		acgIntersection_CombineIntersections(pLeg, ppLegIsect, pOtherLeg, ppOtherLegIsect);
	}
	else	
	// make a new connection
	{
		AICivilianPathIntersection *acpi;
		PathLegIntersect *pli;

		acpi = acgPathIntersection_Alloc();
		eaPush(&s_acgPathInfo.intersects, acpi);

		pli = acgPathLegIntersect_Alloc();
		pli->leg = pLeg;
		eaPush(&acpi->legIntersects, pli);

		pli = acgPathLegIntersect_Alloc();
		pli->leg = pOtherLeg;
		eaPush(&acpi->legIntersects, pli);

		*ppLegIsect = acpi;
		*ppOtherLegIsect = acpi;
	}

}
 
// ------------------------------------------------------------------------------------------------------------------
static void acgIntersection_ProcessLeg(int iPartitionIdx, AICivilianPathLeg *pLeg, S32 bStart)
{
	S32 x, numLegs;
	F32 fBestDistSq = FLT_MAX;
	F32 fSecondBestDistSq = FLT_MAX;
	AICivilianPathLeg *pBestLeg = NULL;
	AICivilianPathLeg *pSecondBestLeg = NULL;

	EConnectionPlug eDesiredPlug = (bStart) ? EConnectionPlug_START : EConnectionPlug_END;
	
	eaClear(&s_acgProcess.isect.eaNearbyLegs);
	// find the closest leg to this leg
	acgPathLegFindNearbyLegs(pLeg, NULL, ((bStart) ? &s_acgProcess.isect.eaNearbyLegs : NULL), 
										 ((!bStart) ? &s_acgProcess.isect.eaNearbyLegs : NULL), NULL, ACG_INTERSECT_DIST);

	// find the closest leg that we can see
	numLegs = eaSize(&s_acgProcess.isect.eaNearbyLegs);
	for (x = 0; x < numLegs; x++)
	{
		F32 fDistSq;
		Vec3 vClosestPos;
		AICivilianPathLeg *pOtherLeg = s_acgProcess.isect.eaNearbyLegs[x];

		fDistSq = acgLegLegDistSquared(pLeg, vClosestPos, pOtherLeg, NULL);
		if(fDistSq < fBestDistSq || fDistSq < fSecondBestDistSq)
		{
			if(acgIntersection_CanLegsConnect(iPartitionIdx, pLeg, eDesiredPlug, pOtherLeg))
			{
				if (fDistSq < fBestDistSq)
				{
					if (pBestLeg)
					{
						fSecondBestDistSq = fBestDistSq;
						pSecondBestLeg = pBestLeg;
					}

					fBestDistSq = fDistSq;
					pBestLeg = pOtherLeg;
				}
				else if (fDistSq < fSecondBestDistSq)
				{
					fSecondBestDistSq = fDistSq;
					pSecondBestLeg = pOtherLeg;
				}
			}
		}
	}

	if (pBestLeg)
	{
		acgIntersection_ConnectLegs(pLeg, bStart, pBestLeg);
	}

	if (pSecondBestLeg && (fSecondBestDistSq - fBestDistSq) <= SQR(15.f) )
	{
		if(acgIntersection_CanLegsConnect(iPartitionIdx, pLeg, eDesiredPlug, pSecondBestLeg))
		{
			acgIntersection_ConnectLegs(pLeg, bStart, pSecondBestLeg);
		}
	}


}

// ------------------------------------------------------------------------------------------------------------------
// Greedy leg intersection 
static void acgProcessIntersections(int iPartitionIdx)
{
	S32 i;
	static AICivilianPathLeg **eaNearbyLegs = NULL;

	for (i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		S32 bStart = false;
		AICivilianPathLeg *pLeg = s_acgProcess.legList[i];
	
		if (acg_d_pos && acgDebug_LegNearDebugPos(pLeg))
		{
			int bbb = 0;
		}

		if (!pLeg->prev)
		{	
			if (!pLeg->prevInt || (pLeg->prevInt && !pLeg->prevInt->bIsMidIntersection))
				acgIntersection_ProcessLeg(iPartitionIdx, pLeg, true);
		}

		if (!pLeg->next)
		{	// 
			if (!pLeg->nextInt || (pLeg->nextInt && !pLeg->nextInt->bIsMidIntersection))
				acgIntersection_ProcessLeg(iPartitionIdx, pLeg, false);
		}
		
	}

	eaDestroy(&s_acgProcess.isect.eaNearbyLegs);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgMidIntersectionGetLegAndIsectLeg(const AICivilianPathIntersection *mid_acpi, PathLegIntersect **intPli, PathLegIntersect **legPli)
{
	S32 i = 0;

	devassert( mid_acpi->bIsMidIntersection && eaSize(&mid_acpi->legIntersects) == MAX_PATHLEG_MID_INTERSECTIONS);

	*intPli = NULL;
	*legPli = NULL;

	if (! vec3IsZero(mid_acpi->legIntersects[0]->intersect))
	{
		*intPli = mid_acpi->legIntersects[0];
		*legPli = mid_acpi->legIntersects[1];
	}
	else
	{
		*legPli = mid_acpi->legIntersects[0];
		*intPli = mid_acpi->legIntersects[1];
	}
}

// ------------------------------------------------------------------------------------------------------------------
// returns true if the MidIntersection is to be deleted
static void acgIntersectionClipMidIntersectiion(AICivilianPathIntersection *acpi,
												PathLegIntersect *pli_isect, PathLegIntersect *pli_edge,
												int start)
{
	Vec3 vIsectToPt;
	F32 fDist;
	AICivilianPathLeg *clipLeg = pli_edge->leg;
	F32 *pvLegPos;
	AICivilianPathIntersection **ppIntersection;
	AICivilianPathLeg **ppConnectedLeg;

	if (start)
	{
		pvLegPos = clipLeg->start;
		ppIntersection = &clipLeg->prevInt;
		ppConnectedLeg = &clipLeg->prev;
	}
	else
	{
		pvLegPos = clipLeg->end;
		ppIntersection = &clipLeg->nextInt;
		ppConnectedLeg = &clipLeg->next;
	}

	if (*ppIntersection || *ppConnectedLeg)
		return;

	if (clipLeg->len < ACG_ISECT_MID_DANGLING_THRESHOLD * 2.5f)
		return;

	subVec3(pvLegPos, pli_isect->intersect, vIsectToPt);
	fDist = dotVec3(clipLeg->dir, vIsectToPt);

	if (fabs(fDist) > ACG_ISECT_MID_DANGLING_THRESHOLD)
		return;

	// clip the edge's leg
	pvLegPos[0] = pli_isect->intersect[0];
	pvLegPos[2] = pli_isect->intersect[2];

	if (pli_edge->leg->type == EAICivilianLegType_CAR)
	{
		fDist = (start) ? clipLeg->width : -clipLeg->width;
		fDist *= 0.75f;
		scaleAddVec3(clipLeg->dir, fDist, pvLegPos, pvLegPos);
	}
	

	// recompute the leg's len
	acgLeg_RecalculateLegDir(clipLeg);

	// remove the mid intersection from the list
	eaFindAndRemove(&clipLeg->midInts, acpi);
	// covert the intersection to be a non-mid
	acpi->bIsMidIntersection = false;

	// set the intersection
	*ppIntersection = acpi;
}


// ------------------------------------------------------------------------------------------------------------------
static void acgIntersectionClipDanglingMidIntersections()
{
	S32 i;

	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		S32 x;

		if(distance3SquaredXZ(leg->start, s_acgDebugPos)<SQR(4) ||
			distance3SquaredXZ(leg->end, s_acgDebugPos)<SQR(4))
			printf("");

		for (x = eaSize(&leg->midInts) - 1; x >= 0; x--)
		{
			AICivilianPathIntersection *acpi = leg->midInts[x];
			PathLegIntersect *pli_isect, *pli_edge;

			devassert(acpi->bIsMidIntersection);

			acgMidIntersectionGetLegAndIsectLeg(acpi, &pli_isect, &pli_edge);
			devassert(pli_edge && pli_isect);

			acgIntersectionClipMidIntersectiion(acpi, pli_isect, pli_edge, 1);
			acgIntersectionClipMidIntersectiion(acpi, pli_isect, pli_edge, 0);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgClipHardTurn_ClipLeg(AICivilianPathLeg *leg, const AICivilianPathLeg *otherLeg)
{
	if (leg->next == otherLeg)
	{
		F32 hwidth = -leg->width * 0.45f;
		scaleAddVec3(leg->dir, hwidth, leg->end, leg->end);
	}
	else
	{
		F32 hwidth = leg->width * 0.45f;
		scaleAddVec3(leg->dir, hwidth, leg->start, leg->start);
	}
	leg->len = distance3(leg->end, leg->start);
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgClipHardTurn_ValidateLeg(const AICivilianPathLeg *leg, const AICivilianPathLeg *otherLeg)
{
	if (leg->len < leg->width)
	{
#if defined(ACG_DEBUGPRINT)
		printf("\n\tClipHardTurn: Leg not long enough, at pos:(%.1f, %.1f, %.1f)", leg->start[0], leg->start[1], leg->start[2]);
#endif
		return false;
	}

	if (leg->next == otherLeg)
	{
		if (leg->nextInt != NULL)
		{
		#if defined(ACG_DEBUGPRINT)
			printf("\n\tClipHardTurn: Could not clip due to conflicting intersection:(%.1f, %.1f, %.1f)", leg->end[0], leg->end[1], leg->end[2]);
		#endif
			return false;
		}
	}
	else
	{
		if (leg->prevInt != NULL)
		{
		#if defined(ACG_DEBUGPRINT)
			printf("\n\tClipHardTurn: Could not clip due to conflicting intersection:(%.1f, %.1f, %.1f)", leg->start[0], leg->start[1], leg->start[2]);
		#endif
			return false;
		}
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgClipHardTurn_IfNeeded(AICivilianPathLeg *leg, AICivilianPathLeg *otherLeg)
{
	F32 fDot = dotVec3(leg->dir, otherLeg->dir);
	

	if (ABS(fDot) < cosf(RAD(70.0f)))
	{
		// these legs need to be clipped and then reconnected via an intersection

		if (!acgClipHardTurn_ValidateLeg(leg, otherLeg))
			return;
		if (!acgClipHardTurn_ValidateLeg(otherLeg, leg))
			return;
				
		{
			const F32 *pvPos = (leg->next == otherLeg) ? leg->end : leg->start;
			printf("\n\tClipHardTurn: modifying leg at pos:(%.1f, %.1f, %.1f)", pvPos[0], pvPos[1], pvPos[2]);
		}
		
		acgClipHardTurn_ClipLeg(leg, otherLeg);
		acgClipHardTurn_ClipLeg(otherLeg, leg);
		// change into an intersection from an implicit next/prev
		{
			PathLegIntersect *pli;	
			AICivilianPathIntersection *acpi = acgPathIntersection_Alloc();
			eaPush(&s_acgPathInfo.intersects, acpi);

			pli = acgPathLegIntersect_Alloc();
			pli->leg = leg;
			eaPush(&acpi->legIntersects, pli);

			pli = acgPathLegIntersect_Alloc();
			pli->leg = otherLeg;
			eaPush(&acpi->legIntersects, pli);

			if (leg->next == otherLeg)
			{
				leg->next = NULL;
				leg->nextInt = acpi;
			}
			else
			{
				leg->prev = NULL;
				leg->prevInt = acpi;
			}

			if (otherLeg->next == leg)
			{
				otherLeg->next = NULL;
				otherLeg->nextInt = acpi;
			}
			else
			{
				otherLeg->prev = NULL;
				otherLeg->prevInt = acpi;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
// only applies to roads, at the moment. This will clip legs that are directly connected via
// next/prev leg that meet at a high threshold (near 70 degrees). 
// The clipped legs will pushed back slightly and instead connected via an intersection (prevInt/nextInt)
// this is for the car pathing to know to smooth the turn 
static void acgClipHardTurns()
{
	S32 i;

	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		
		//if (leg->type != AI_CIV_CAR)
		//	continue;

		if (leg->next)
		{
			acgClipHardTurn_IfNeeded(leg, leg->next);
		}
		if (leg->prev)
		{
			acgClipHardTurn_IfNeeded(leg, leg->prev);
		}

	}

}

// ------------------------------------------------------------------------------------------------------------------
#define LEG_DIST_SQ SQR(100.0f)
static bool acgIsIntersectionReplaceCandidate(const AICivilianPathLeg *leg, const AICivilianPathIntersection *acpi)
{
	if (acpi && !acpi->bIsMidIntersection)
	{
		if ((leg->prevInt == acpi && leg->prev != NULL) || (leg->nextInt == acpi && leg->next != NULL))
			return false; // can only do this if the direct connections are not set

		if (eaSize(&acpi->legIntersects) == 2)
		{
			S32 idx = acgFindPathLegInACPI(acpi, leg);
			if (idx != -1)
			{
				// test the distance
				const AICivilianPathLeg *otherLeg;
				const F32 *leg1Pos;
				const F32 *leg2Pos;
				F32 distSQ;

				idx = !idx;
				otherLeg = acpi->legIntersects[idx]->leg;
				devassert(otherLeg->prevInt == acpi || otherLeg->nextInt == acpi);
				
				if ((otherLeg->prevInt == acpi && otherLeg->prev != NULL) || 
					(otherLeg->nextInt == acpi && otherLeg->next != NULL))
					return false; // can only do this if the direct connections are not set
	
				if (acg_d_pos && (acgDebug_LegNearDebugPos(leg) || acgDebug_LegNearDebugPos(otherLeg)))
				{
					int xxx = 0;
				}


				leg1Pos = (leg->prevInt == acpi) ? leg->start : leg->end;
				leg2Pos = (otherLeg->prevInt == acpi) ? otherLeg->start : otherLeg->end;
				
				distSQ = distance3Squared(leg1Pos, leg2Pos);
				if (distSQ <= LEG_DIST_SQ)
				{
					F32 fDot = dotVec3(leg->dir, otherLeg->dir);
					return (ABS(fDot) >= cos(RAD(30.0f)));
				}
			}
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgReplaceSingleIntersection(AICivilianPathLeg *leg, AICivilianPathIntersection *acpi)
{
	AICivilianPathLeg *otherLeg;
	S32 idx = acgFindPathLegInACPI(acpi, leg);

	idx = !idx;
	otherLeg = acpi->legIntersects[idx]->leg;

	if (leg->prevInt == acpi)
	{
		leg->prev = otherLeg;
		leg->prevInt = NULL;
	}
	else
	{
		leg->next = otherLeg;
		leg->nextInt = NULL;
	}
	

	if (otherLeg->prevInt == acpi)
	{
		otherLeg->prev = leg;
		otherLeg->prevInt = NULL;
	}
	else
	{
		otherLeg->next = leg;
		otherLeg->nextInt = NULL;
	}

	eaFindAndRemoveFast(&s_acgPathInfo.intersects, acpi);
	
	acgPathIntersection_Free(acpi);
}

// ------------------------------------------------------------------------------------------------------------------
// this step will take all prev/next intersections that only connect two legs and are close enough together
// and just make the leg's directly connected 
static void acgReplaceSingleIntersections()
{
	S32 i; 

	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		
		if (leg->type == EAICivilianLegType_PERSON)
			continue;
		
		
		if (acgIsIntersectionReplaceCandidate(leg, leg->prevInt))
		{
			acgReplaceSingleIntersection(leg, leg->prevInt);
		}
		
		if (acgIsIntersectionReplaceCandidate(leg, leg->nextInt))
		{
			acgReplaceSingleIntersection(leg, leg->nextInt);
		}

	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AILegFlow acgGetFlowFromLegToOther(const AICivilianPathLeg *leg, const AICivilianPathLeg *otherLeg)
{
	if (leg->prev == otherLeg)
		return ALF_PREV;

	if (leg->next == otherLeg)
		return ALF_NEXT;

	if (leg->prevInt)
	{
		if (acgFindPathLegInACPI(leg->prevInt, otherLeg) != -1)
			return ALF_PREV;
	}

	if (leg->nextInt)
	{
		if (acgFindPathLegInACPI(leg->nextInt, otherLeg) != -1)
			return ALF_NEXT;
	}

	// we should never see this happen, other leg was not connected to us
	devassert(0);
	return ALF_MID;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool acgIsLegReversingDirection(const AICivilianPathLeg *leg, const AICivilianPathLeg *otherLeg, AILegFlow flow)
{
	AILegFlow otherFlow;
	otherFlow = acgGetFlowFromLegToOther(otherLeg, leg);
	return otherFlow == flow;
}



// ------------------------------------------------------------------------------------------------------------------
static void acgCorrectOneWayStreet(const AICivilianPathLeg *leg, AICivilianPathLeg *otherLeg, AILegFlow flow, AICivilianPathLeg ***peaOpenLegList)
{
	if (otherLeg->bIsOneWay && otherLeg->doneStart == false)
	{
		// make sure we don't reverse going to this leg
		if (acgIsLegReversingDirection(leg, otherLeg, flow))
		{
			// reverse this leg
			acgLeg_ReverseLeg(otherLeg);
		}

		otherLeg->doneStart = true;
		eaPush(peaOpenLegList, otherLeg);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgAlignNeighborsToOneWayStreet(AICivilianPathLeg *leg)
{
	AICivilianPathLeg **eaOpenLegList = NULL;

	// walk this one-way street 
	eaPush(&eaOpenLegList, leg);
	while(eaSize(&eaOpenLegList))
	{
		leg = eaPop(&eaOpenLegList);

		if (leg->next)
		{
			acgCorrectOneWayStreet(leg, leg->next, ALF_NEXT, &eaOpenLegList);
		}

		if (leg->prev)
		{
			acgCorrectOneWayStreet(leg, leg->prev, ALF_PREV, &eaOpenLegList);
		}

		if (leg->nextInt && !leg->nextInt->bIsMidIntersection)
		{
			AICivilianPathIntersection *acpi = leg->nextInt;
			if (eaSize(&acpi->legIntersects) == 2)
			{
				S32 idx = acgFindPathLegInACPI(acpi, leg);
				if (idx != -1)
				{
					idx = !idx;
					acgCorrectOneWayStreet(leg, acpi->legIntersects[idx]->leg, ALF_NEXT, &eaOpenLegList);
				}
			}
		}

		if (leg->prevInt && !leg->prevInt->bIsMidIntersection)
		{
			AICivilianPathIntersection *acpi = leg->prevInt;
			if (eaSize(&acpi->legIntersects) == 2)
			{
				S32 idx = acgFindPathLegInACPI(acpi, leg);
				if (idx != -1)
				{
					idx = !idx;
					acgCorrectOneWayStreet(leg, acpi->legIntersects[idx]->leg, ALF_PREV, &eaOpenLegList);
				}
			}
		}
	}

	eaDestroy(&eaOpenLegList);
}

#define LANE_WIDTH	20.0f
// ------------------------------------------------------------------------------------------------------------------
static void acgDestroyDeadEndOneWayStreets()
{
	S32 i;

	// determine the one-way streets
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->type != EAICivilianLegType_CAR)
		{
			continue;
		}

		// TODO: I need to get the lane width for the cars so I can see if a street is going to be one-way
		leg->lane_width = LANE_WIDTH;
		acgLeg_CalculateMaxLanes(leg);
	}
	

	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->type != EAICivilianLegType_CAR)
			continue;
				
		if (leg->bIsOneWay == false || leg->deleted)
			continue;

		if ((leg->next == NULL && leg->nextInt == NULL) || (leg->prev == NULL && leg->prevInt == NULL) )
		{
			// this one-way leg dead ends. We'll need to destroy it and all the one-way legs connected to it.
			leg->deleted = s_acgProcess.state;
			leg->deleteReason = "Dead end one-way";
			acgDestroyLeg(&leg, false);

		#if defined(ACG_DEBUGPRINT)
			printf("\n\tDeleted dead-end one way leg at pos:(%.1f, %.1f, %.1f)", leg->start[0], leg->start[1], leg->start[2]);
		#endif
			// lazy and exhaustive. Reset our iteration to find any other one-way legs that are dead ending
			// There shouldn't be many of these roads, though.
			i = 0;
		}
	}
	
	

	acgProcess_RemoveDeletedLegs();
}

// ------------------------------------------------------------------------------------------------------------------
// after this function is finished, all continuous one-way streets will flow in the same direction
static void acgCorrectOneWayStreets()
{
	S32 i;

	// determine the one-way streets
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];

		if (leg->type != EAICivilianLegType_CAR)
		{
			continue;
		}

		// using the doneStart flag to signify that the one-way street was processed already
		leg->doneStart = false;
	}
	
	
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		
		if (leg->type != EAICivilianLegType_CAR || leg->bIsOneWay == false || leg->doneStart == true)
		{
			continue;
		}

		leg->doneStart = true;
		
		acgAlignNeighborsToOneWayStreet(leg);
	}

	// reset the doneStart flag
	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		leg->doneStart = false;
	}
	//
	
	// now that all the one-way streets are flowing together, 
	// look for nearby one-way signs and have them face the correct way
	{
		#define ACG_ONE_WAY_SEARCHDIST	300.0f
		#define ACG_ONE_WAY_PERP		100.0f

		SavedTaggedGroups *pTagData = acgGetSavedTaggedGroups();
		EArray32Handle	eaTrafficSearchTypes = NULL;

		ea32Push(&eaTrafficSearchTypes, ETagGleanObjectType_TRAFFIC_ONEWAY_SIGN);

		for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
		{
			AICivilianPathLeg *leg = s_acgProcess.legList[i];
			TaggedObjectData* pOneWaySign;
			Vec3 vMid;

			if (leg->type != EAICivilianLegType_CAR || leg->bIsOneWay == false || leg->doneStart == true)
			{
				continue;
			}

			interpVec3(0.5f, leg->start, leg->end, vMid);
						
			pOneWaySign = wlTGDFindNearestObject(pTagData, vMid, ACG_ONE_WAY_SEARCHDIST, eaTrafficSearchTypes);
			if (pOneWaySign)
			{
				// make sure the sign is within a given perp distance 
				F32 fDist;
				Vec3 vToSign;
				Mat3 mtxSign;
				subVec3(pOneWaySign->vPos, vMid, vToSign);
				fDist = dotVec3(leg->perp, vToSign);
				if (ABS(fDist) > ACG_ONE_WAY_PERP)
				{
					continue;
				}

				quatToMat(pOneWaySign->qRot, mtxSign);
				if (dotVec3(mtxSign[0], leg->dir) > 0.0f)
				{
					// the one way sign is telling us to reverse the direction
					// reverse the leg and then align all the leg neighbors to this leg
					acgLeg_ReverseLeg(leg);
					leg->doneStart = true;
					acgAlignNeighborsToOneWayStreet(leg);
				}
			}
		}

		ea32Destroy(&eaTrafficSearchTypes);
	}
		
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void acgIntersectionPointMinMax(const Vec3 pt, const AICivilianPathLeg *leg, Vec3 min, Vec3 max)
{
	Vec3 tmpPt;
	F32 halfWidth = leg->width * 0.5f;

	scaleAddVec3(leg->perp, halfWidth, pt, tmpPt);
	MINVEC3(tmpPt, min, min);
	MAXVEC3(tmpPt, max, max);
	scaleAddVec3(leg->perp, -halfWidth, pt, tmpPt);
	MINVEC3(tmpPt, min, min);
	MAXVEC3(tmpPt, max, max);
}


// ------------------------------------------------------------------------------------------------------------------
static void acgCalculateIntersectionBoundingVolumes(Entity *debugger)
{
	const F32 BOUNDING_HEIGHT_PADDING = 1.0f;
	S32 i;

	//
	for (i = 0; i < eaSize(&s_acgPathInfo.intersects); i++)
	{
		AICivilianPathIntersection *acpi = s_acgPathInfo.intersects[i];

		setVec3same(acpi->min, FLT_MAX);
		setVec3same(acpi->max, -FLT_MAX);

		if (acpi->bIsMidIntersection)
		{
			S32 x;
			// mid intersections
			PathLegIntersect *pli_isect = NULL; // The leg that intersects the other leg in the middle
			PathLegIntersect *pli_edge = NULL;
			F32 *end_pt = NULL;

			for (x = 0; x < eaSize(&acpi->legIntersects); x++)
			{
				PathLegIntersect *pli =  acpi->legIntersects[x];

				if (!pli_isect && !vec3IsZero(pli->intersect))
				{
					// The leg that intersects the other leg in the middle
					pli_isect = pli;
					if (pli_isect->leg->nextInt == acpi)
					{
						end_pt = pli_isect->leg->end;
					}
					else
					{
						devassert(pli_isect->leg->prevInt == acpi);
						end_pt = pli_isect->leg->start;
					}
				}
				else
				{
					pli_edge = pli;
				}
			}
			devassert(end_pt && pli_isect && pli_edge);

			// this PathLegIntersect has the intersection point
			// and we'll use this leg's end/start point as the other
			acgIntersectionPointMinMax(pli_isect->intersect, pli_edge->leg, acpi->min, acpi->max);
			acgIntersectionPointMinMax(end_pt, pli_isect->leg, acpi->min, acpi->max);

		}
		else
		{
			// end to end intersections
			S32 x;

			for (x = 0; x < eaSize(&acpi->legIntersects); x++)
			{
				PathLegIntersect *pli =  acpi->legIntersects[x];
				F32 *isect_pt = NULL;

				if (pli->leg->nextInt == acpi)
				{
					isect_pt = pli->leg->end;
				}
				else
				{
					devassert(pli->leg->prevInt == acpi);
					isect_pt = pli->leg->start;
				}

				acgIntersectionPointMinMax(isect_pt, pli->leg, acpi->min, acpi->max);
			}
		}

		acpi->min[1] -= BOUNDING_HEIGHT_PADDING;
		acpi->max[1] += BOUNDING_HEIGHT_PADDING;

	}

}

// ------------------------------------------------------------------------------------------------------------------
#define TRAFFIC_OBJECT_SEARCH_DIST	(200.0f)
static void acgClassifyIntersections(Entity *debugger)
{
	S32 i;
	SavedTaggedGroups *pTagData = acgGetSavedTaggedGroups();
		

	EArray32Handle	eaTrafficSearchTypes = NULL;

	ea32Push(&eaTrafficSearchTypes, ETagGleanObjectType_TRAFFIC_STOPSIGN);
	ea32Push(&eaTrafficSearchTypes, ETagGleanObjectType_TRAFFIC_LIGHT);
		
	for (i = 0; i < eaSize(&s_acgPathInfo.intersects); i++)
	{
		AICivilianPathIntersection *acpi = s_acgPathInfo.intersects[i];
		EAICivilianLegType type = acpi->legIntersects[0]->leg->type;

		if (type == EAICivilianLegType_PERSON)
			continue;

		if (acpi->bIsMidIntersection)
		{
			// for now, any mid intersections are considered stop sign/cross traffic
			// rrp- todo: if this is a stop light, we're going to have to split the leg on the mid intersection
			// and then create a 3-way intersection. 
			acpi->isectionType = EIntersectionType_SIDESTREET_STOPSIGN;
		}
		else
		{
			S32 numLegsInIntersection = eaSize(&acpi->legIntersects);

			/*
			if (numLegsInIntersection <= 2)
			{
				acpi->isectionType = EIntersectionType_NONE;
			}
			else 
			*/
			{
				TaggedObjectData *pClosestObject;
				bool bTrafficLight = false;
				Vec3 vMid;

				interpVec3(0.5f, acpi->min, acpi->max, vMid);
				
				pClosestObject = wlTGDFindNearestObject(pTagData, vMid, TRAFFIC_OBJECT_SEARCH_DIST, eaTrafficSearchTypes);
				if (pClosestObject)
				{
					bTrafficLight = (pClosestObject->eType == ETagGleanObjectType_TRAFFIC_LIGHT);
				}

				if (numLegsInIntersection == 2)
				{
					acpi->isectionType = (bTrafficLight) ? EIntersectionType_2WAY_STOPLIGHT : EIntersectionType_NONE;
					
				}
				else if (numLegsInIntersection != 4)
				{
					acpi->isectionType = (bTrafficLight) ? EIntersectionType_3WAY_STOPLIGHT : EIntersectionType_STOPSIGN;
				}
				else
				{
					acpi->isectionType = (bTrafficLight) ? EIntersectionType_4WAY_STOPLIGHT : EIntersectionType_STOPSIGN;
				}	
			}
			
		}

	}

	ea32Destroy(&eaTrafficSearchTypes);

}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathLeg* acgCrosswalk_BestRoad(AICivilianPathLeg *leg)
{
	int i;
	F32 fClosestDistSq = FLT_MAX;
	AICivilianPathLeg *pBestLeg = NULL;

	for(i = 0; i < eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *other_leg = s_acgProcess.legList[i];
		F32 fDistSq;

		if(other_leg->type != EAICivilianLegType_CAR)
			continue;
			
		fDistSq = acgLegLegDistSquared(leg, NULL, other_leg, NULL);
		if(fDistSq < fClosestDistSq)
		{
			// may need to add some angle thresholds. Only perpendicular legs to the xwalk should be considered.
			fClosestDistSq = fDistSq;
			pBestLeg = other_leg;
		}
	}

#define CROSSWALK_ROAD_DISTTHRESHOLDSQ SQR(40.0f)
	// check this afterwards, instead of setting it initially so we can see what the closest actually was
	if (fClosestDistSq > CROSSWALK_ROAD_DISTTHRESHOLDSQ)
		return NULL;
		
	return pBestLeg;
}


// ------------------------------------------------------------------------------------------------------------------
// for each crosswalk, finds the nearest road
static void acgCrosswalk_Fixup()
{
	S32 i; 

	for(i = eaSize(&s_acgProcess.legList) - 1; i >= 0; i--)
	{
		AICivilianPathLeg *pCrosswalkLeg = s_acgProcess.legList[i];

		if (pCrosswalkLeg->bIsCrosswalk == false)
			continue;

		if ((!pCrosswalkLeg->next && !pCrosswalkLeg->nextInt) || (!pCrosswalkLeg->prev && !pCrosswalkLeg->prevInt))
		{
			// this xwalk is unconnected on one end, we're going to have to remove it
			acgDestroyLeg(&pCrosswalkLeg, false);
			pCrosswalkLeg->deleted = s_acgProcess.state;
			pCrosswalkLeg->deleteReason = "Xwalk missing connection";
			eaRemove(&s_acgProcess.legList, i);
			continue;
		}
		
		
		// find the nearest road that this intersects with
		pCrosswalkLeg->pCrosswalkNearestRoad = acgCrosswalk_BestRoad(pCrosswalkLeg);

		if (!pCrosswalkLeg->pCrosswalkNearestRoad)
		{	// no road nearby, so make this a regular sidewalk
			pCrosswalkLeg->bIsCrosswalk = false;
			continue;
		}
		else
		{
			// add the crosswalk to the leg 
			eaPush(&pCrosswalkLeg->pCrosswalkNearestRoad->eaCrosswalkLegs, pCrosswalkLeg);
			
			// check to see if the road is part of an intersection 
			// and if it is, it will be based on some traffic logic
			{
				Vec3 vClosestPt;
				AICivilianPathLeg *pRoad = pCrosswalkLeg->pCrosswalkNearestRoad;
				AICivilianPathIntersection *acpi = NULL;
				F32 fThresholdDistToEndSq = SQR(pCrosswalkLeg->width * 1.25f);
				
				acgLegLegDistSquared(pCrosswalkLeg, NULL, pRoad, vClosestPt);
				
				if (distance3Squared(vClosestPt, pRoad->start) <= fThresholdDistToEndSq)
				{
					acpi = pRoad->prevInt;
				}
				else if (distance3Squared(vClosestPt, pRoad->end) <= fThresholdDistToEndSq)
				{
					acpi = pRoad->nextInt;
				}
				
				if (acpi && acpi->isectionType >= EIntersectionType_marker_STOPLIGHT)
				{
					PathLegIntersect *pli;
					S32 idx = acgFindPathLegInACPI(acpi, pRoad);

					devassert(idx != -1);
					pli =  acpi->legIntersects[idx];

					pli->pCrosswalkLeg = pCrosswalkLeg;
					// special case, of two-way stop lights aren't controlled by stop lights, they're always on
					if (acpi->isectionType != EIntersectionType_2WAY_STOPLIGHT)
						pCrosswalkLeg->bIsXingStopLight = true;
				}
			}

			if (!pCrosswalkLeg->bIsXingStopLight)
			{
				
				// if we aren't associated with a stop light
				// orient the crosswalk to make sure that the perp is in the same direction as the road leg
				if (dotVec3(pCrosswalkLeg->perp, pCrosswalkLeg->pCrosswalkNearestRoad->dir) < 0)
				{
					Vec3 vtmp;
					AICivilianPathLeg *tmpLeg;
					AICivilianPathIntersection *tmpInt;

					copyVec3(pCrosswalkLeg->start, vtmp);
					copyVec3(pCrosswalkLeg->end, pCrosswalkLeg->start);
					copyVec3(vtmp, pCrosswalkLeg->end);
					
					calcLegDir(pCrosswalkLeg, pCrosswalkLeg->dir, pCrosswalkLeg->perp);

					tmpLeg = pCrosswalkLeg->next;
					pCrosswalkLeg->next = pCrosswalkLeg->prev;
					pCrosswalkLeg->prev = tmpLeg;

					tmpInt = pCrosswalkLeg->nextInt;
					pCrosswalkLeg->nextInt = pCrosswalkLeg->prevInt;
					pCrosswalkLeg->prevInt = tmpInt;
				}

			}

			// 
			{
				Vec3 vStart, vEnd, vIsectPosStart, vIsectPosEnd;
				AICivilianPathLeg *pRoad = pCrosswalkLeg->pCrosswalkNearestRoad;
				
				aiCivLeg_GetLegCornerPoints(pRoad, true, vStart, vEnd);
				zeroVec3(vIsectPosStart);
				zeroVec3(vIsectPosEnd);
				
				acgLineLine2dIntersection(vStart, pRoad->dir, pCrosswalkLeg->start, pCrosswalkLeg->dir, vIsectPosStart);
				acgLineLine2dIntersection(vEnd, pRoad->dir, pCrosswalkLeg->start, pCrosswalkLeg->dir, vIsectPosEnd);

				if (distance3SquaredXZ(pCrosswalkLeg->start, vIsectPosStart) < 
						distance3SquaredXZ(pCrosswalkLeg->start, vIsectPosEnd))
				{
					Vec3 vDir;
					subVec3(vIsectPosStart, pCrosswalkLeg->start, vDir);
					pCrosswalkLeg->crosswalkRoadStartDist = dotVec3(pCrosswalkLeg->dir, vDir);
				}
				else
				{
					Vec3 vDir;
					subVec3(vIsectPosEnd, pCrosswalkLeg->start, vDir);
					pCrosswalkLeg->crosswalkRoadStartDist = dotVec3(pCrosswalkLeg->dir, vDir);
				}

				if (pCrosswalkLeg->crosswalkRoadStartDist < 0)
				{
					pCrosswalkLeg->crosswalkRoadStartDist = 0.f;
				}
			}


		}

		//
		
		//
	}


}

// ------------------------------------------------------------------------------------------------------------------
// AICivilianPathPoint 
// ------------------------------------------------------------------------------------------------------------------


GamePatrolRoute** acgPathPoints_GetPatrolRoutes(const char *pszBaseName)
{
	GamePatrolRoute** eaPatrols = NULL;
	char *pString = NULL;
	S32 i = 0;
	S32 missed = 0;
	
	do {
		GamePatrolRoute *pRoute;
		estrPrintf(&pString, "%s%d", pszBaseName, i);
		pRoute = patrolroute_GetByName(pString, NULL);
		if (pRoute)
		{
			missed = 0;
			eaPush(&eaPatrols, pRoute);
		}
		else
		{
			missed++;
		}

		++i;
	} while(missed < 2);


	estrDestroy(&pString);
	return eaPatrols;
}

// ------------------------------------------------------------------------------------------------------------------
void acgPathPoints_CreatePathsFromPatrolRoutes(GamePatrolRoute** eaPatrols)
{
	// for each of the patrols that we found, create the AICivPathPoint version
	FOR_EACH_IN_EARRAY(eaPatrols, GamePatrolRoute, pRoute)
	{
		AICivilianPathPoint *pCurPoint = NULL, *pPrevPoint = NULL;
		S32 i, count;

		if (!pRoute->pWorldRoute || !pRoute->pWorldRoute->properties)
			continue;

		// 
		count = eaSize(&pRoute->pWorldRoute->properties->patrol_points);
		if (count <= 1)
			continue; // there need to be at least 2 path points to add them 

		for (i = 0; i < count; i++)
		{
			WorldPatrolPointProperties *pPatrolPoint = pRoute->pWorldRoute->properties->patrol_points[i];

			pCurPoint = StructAlloc(parse_AICivilianPathPoint);
			eaPush(&s_acgProcess.eaPathPoints, pCurPoint);

			copyVec3(pPatrolPoint->pos, pCurPoint->vPos);

			if (pPrevPoint)
			{
				pPrevPoint->pNextPathPoint = pCurPoint;
				pCurPoint->pPrevPathPoint = pPrevPoint;
			}
			pPrevPoint = pCurPoint;
		}
	}
	FOR_EACH_END

}



// ------------------------------------------------------------------------------------------------------------------
static bool acgPathPoint_IsLeaf(const AICivilianPathPoint *pPathPoint)
{
	return !pPathPoint->pNextPathPoint || !pPathPoint->pPrevPathPoint;
}

static bool acgPathPoint_HeadIsLeaf(const AICivilianPathPoint *pPathPoint)
{
	return acgPathPoint_IsLeaf(pPathPoint) && !pPathPoint->pPrevPathPoint;
}

static bool acgPathPoint_TailIsLeaf(const AICivilianPathPoint *pPathPoint)
{
	return acgPathPoint_IsLeaf(pPathPoint) && !pPathPoint->pNextPathPoint;
}
// ------------------------------------------------------------------------------------------------------------------
#define TROLLEY_PATROL_ROUTE_BASENAME	"Trolley_"

// Returns a list of all leaf pathpoints that are within the specified distance
static void acgPathPoints_FindAllLeafPathPoints(const Vec3 vPos, F32 fSearchDist, AICivilianPathPoint ***peaPathPoints)
{
	eaClear(peaPathPoints);

	FOR_EACH_IN_EARRAY(s_acgProcess.eaPathPoints, AICivilianPathPoint, pPathPoint)
	{
		if (!acgPathPoint_IsLeaf(pPathPoint) || pPathPoint->deleted)
			continue;
		
		if (distance3SquaredXZ(vPos, pPathPoint->vPos) < SQR(fSearchDist))
		{
			eaPush(peaPathPoints, pPathPoint);
		}
	}
	FOR_EACH_END

}


// ------------------------------------------------------------------------------------------------------------------
static S32 acgPathPoints_FindPathPointCount(const AICivilianPathPoint *pPathPoint)
{
	S32 count = 0;
	devassert(pPathPoint);
	// make sure that the path point be a leaf. if not, walk till we find one
	if (!acgPathPoint_IsLeaf(pPathPoint))
	{	// walk to the end of the path
		while (pPathPoint->pNextPathPoint)
			pPathPoint = pPathPoint->pNextPathPoint;
		
	}

	if (pPathPoint->pNextPathPoint)
	{	// walk forwards
		while (pPathPoint)
		{
			pPathPoint = pPathPoint->pNextPathPoint;
			count ++;
		}
	}
	else
	{
		// walk backwards
		while (pPathPoint)
		{
			pPathPoint = pPathPoint->pPrevPathPoint;
			count ++;
		}
	}

	return count;
}

// ------------------------------------------------------------------------------------------------------------------
void acgPathPoints_GetLeafPathDir(const AICivilianPathPoint *pPathPoint, Vec3 vDir)
{
	if(pPathPoint->pNextPathPoint)
	{
		subVec3(pPathPoint->pNextPathPoint->vPos, pPathPoint->vPos, vDir);
		normalVec3(vDir);
	}
	else if(pPathPoint->pPrevPathPoint)
	{
		subVec3(pPathPoint->vPos, pPathPoint->pPrevPathPoint->vPos, vDir);
		normalVec3(vDir);
	}
	else
	{
		zeroVec3(vDir);
	}
}


// ------------------------------------------------------------------------------------------------------------------
static bool acgPathPoints_ShouldReversePathForMerge(const AICivilianPathPoint *pMergeTo, const AICivilianPathPoint *pMergeFrom)
{
	return (acgPathPoint_HeadIsLeaf(pMergeTo) && !acgPathPoint_TailIsLeaf(pMergeFrom)) ||
		   (acgPathPoint_TailIsLeaf(pMergeTo) && !acgPathPoint_HeadIsLeaf(pMergeFrom));
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathPoint* acgPathPoints_CreateNewAfterPathPoint(AICivilianPathPoint *pPathPoint, const Vec3 vNewPt)
{
	AICivilianPathPoint *pNewPt = StructAlloc(parse_AICivilianPathPoint);
	eaPush(&s_acgProcess.eaPathPoints, pNewPt);

	copyVec3(vNewPt, pNewPt->vPos);
	
	pNewPt->pPrevPathPoint = pPathPoint;
	pNewPt->pNextPathPoint = pPathPoint->pNextPathPoint;

	if (pPathPoint->pNextPathPoint)
	{
		pPathPoint->pNextPathPoint->pPrevPathPoint = pNewPt;
	}
	
	pPathPoint->pNextPathPoint = pNewPt;
	return pNewPt;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathPoints_FlipPath(AICivilianPathPoint *pPathPoint)
{
	// for ease of flipping the path, find the head of the list
	if (!acgPathPoint_HeadIsLeaf(pPathPoint))
	{
		while (pPathPoint->pPrevPathPoint)
			pPathPoint = pPathPoint->pPrevPathPoint;
	}
	
	{
		AICivilianPathPoint *pPrevPoint = NULL;
		
		while(pPathPoint)
		{
			AICivilianPathPoint *pNextPathPoint = pPathPoint->pNextPathPoint;
			
			pPathPoint->pNextPathPoint = pPrevPoint;
			
			if (pPrevPoint)
			{
				pPrevPoint->pPrevPathPoint = pPathPoint;	
			}
			
			pPrevPoint = pPathPoint;

			// walk down the list
			pPathPoint = pNextPathPoint;
		}

		if (pPrevPoint)
		{
			pPrevPoint->pPrevPathPoint = NULL;
		}

	}
	
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathPoints_MergePathPoints(AICivilianPathPoint *pPathPoint1, AICivilianPathPoint *pPathPoint2)
{
	// find out which path is longer, that one will become the base and the other path will merge in.
	// Then, find out if we need to reverse the other path so they are in the same direction. 
	// Lastly, fix up the path points, and remove one of them from the list
	AICivilianPathPoint *pMergeTo = NULL;
	AICivilianPathPoint *pMergeFrom = NULL;

	devassert( acgPathPoint_IsLeaf(pPathPoint1) && acgPathPoint_IsLeaf(pPathPoint2));

	{
		S32 path1Count, path2Count;
		path1Count = acgPathPoints_FindPathPointCount(pPathPoint1);
		path2Count = acgPathPoints_FindPathPointCount(pPathPoint2);
		if (path1Count > path2Count)
		{
			pMergeTo = pPathPoint1;
			pMergeFrom = pPathPoint2;
		}
		else
		{
			pMergeTo = pPathPoint2;
			pMergeFrom = pPathPoint1;
		}
	}

	if (acgPathPoints_ShouldReversePathForMerge(pMergeTo, pMergeFrom))
	{	// opposite directions, we need to flip
		acgPathPoints_FlipPath(pMergeFrom);
	}

	//  
	if (acgPathPoint_HeadIsLeaf(pMergeTo))
	{// head -> tail
		pMergeTo->pPrevPathPoint = pMergeFrom->pPrevPathPoint;
		pMergeFrom->pPrevPathPoint->pNextPathPoint = pMergeTo;

		// orphan this node and mark it deleted
		pMergeFrom->pPrevPathPoint = NULL;
		pMergeFrom->deleted = true;
	}
	else
	{// tail -> head
		pMergeTo->pNextPathPoint = pMergeFrom->pNextPathPoint;
		pMergeFrom->pNextPathPoint->pPrevPathPoint = pMergeTo;

		// orphan this node and mark it deleted
		pMergeFrom->pNextPathPoint = NULL;
		pMergeFrom->deleted = true;
	}


}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathPoints_CreatePathPointIntersection(AICivilianPathPoint **eaPathPoints)
{
	AICivilianPathPointIntersection *pIsect = StructAlloc(parse_AICivilianPathPointIntersection);
	S32 longestPath = -1;
	AICivilianPathPoint *pMainPath = NULL;

	eaPushEArray(&pIsect->eaPathPoints, &eaPathPoints);

	FOR_EACH_IN_EARRAY(pIsect->eaPathPoints, AICivilianPathPoint, pPathPt)
	{
		S32 length;
		devassert(acgPathPoint_IsLeaf(pPathPt) == true);

		length = acgPathPoints_FindPathPointCount(pPathPt);
		if (length > longestPath)
		{
			longestPath = length;
			pMainPath = pPathPt;
		}

		pPathPt->pPathPointIntersection = pIsect;
	}
	FOR_EACH_END

	eaPush(&s_acgProcess.eaPathPointIntersections, pIsect);
	
	
	FOR_EACH_IN_EARRAY(pIsect->eaPathPoints, AICivilianPathPoint, pPathPt)
	{
		if (pMainPath == pPathPt)
			continue;

		if (acgPathPoints_ShouldReversePathForMerge(pMainPath, pPathPt))
		{	// opposite directions, we need to flip
			acgPathPoints_FlipPath(pPathPt);
		}

		//pPathPt->pPathPointIntersection = pIsect;
	}
	FOR_EACH_END
	
	
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathPoints_RemoveDeleted()
{
	// remove all the deleted path points

	S32 i = eaSize(&s_acgProcess.eaPathPoints) - 1;
	for(; i >= 0; i--)
	{
		AICivilianPathPoint *pPoint = s_acgProcess.eaPathPoints[i];
		if (pPoint->deleted)
		{
			eaRemoveFast(&s_acgProcess.eaPathPoints, i);
			StructDestroy(parse_AICivilianPathPoint, pPoint);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathPoints_FindAndMergePathPoints()
{
	// run through all the path points, finding the start/end of the paths and find all other pathpoints that share
	// the same position.
	AICivilianPathPoint **eaPathPoints = NULL;
	FOR_EACH_IN_EARRAY(s_acgProcess.eaPathPoints, AICivilianPathPoint, pPathPoint)
	{
		if (! acgPathPoint_IsLeaf(pPathPoint) || pPathPoint->deleted || 
			pPathPoint->pPathPointIntersection)
			continue;

		eaClear(&eaPathPoints);
		acgPathPoints_FindAllLeafPathPoints(pPathPoint->vPos, 2.f, &eaPathPoints);
		if (eaSize(&eaPathPoints) <= 1)
			continue;

		if (eaSize(&eaPathPoints) == 2)
		{
			// only 2, we can just merge
			acgPathPoints_MergePathPoints(eaPathPoints[0], eaPathPoints[1]);
		}
		else
		{
			acgPathPoints_CreatePathPointIntersection(eaPathPoints);
			printf("\nFound a potential fork in the road (%.2f, %.2f, %.2f, )\n", 
						pPathPoint->vPos[0],pPathPoint->vPos[1],pPathPoint->vPos[2]);
		}
	}
	FOR_EACH_END
	eaDestroy(&eaPathPoints);


	acgPathPoints_RemoveDeleted();


}

static bool acgPathPoints_ShouldLineClipToLeg(AICivilianPathPoint *pPathPoint, AICivilianPathLeg *pLeg,
												AICivilianPathIntersection *isect)
{
	Vec3 vPathDir;
	Vec3 vTrafficDir;
	
	acgPathPoints_GetLeafPathDir(pPathPoint, vPathDir);


	if (pLeg->nextInt == isect)
	{
		copyVec3(pLeg->dir, vTrafficDir);
	}
	else 
	{
		negateVec3(pLeg->dir, vTrafficDir);
	}

	return dotVec3(vTrafficDir, vPathDir) > 0.f;

}

static void acgPathPoints_ClipPathPointToIntersection(AICivilianPathPoint *pPathPoint)
{
	AICivilianPathPoint *pNextPt = pPathPoint->pNextPathPoint;
		
	if (! pPathPoint->pNextPathPoint)
		return;

	// Go through all the intersections that are some special type,
	// and check every leg to see if the pathpoints cross into the intersection from their leg
	// if it does, clip / snap the point to the intersection point 
	FOR_EACH_IN_EARRAY(s_acgPathInfo.intersects, AICivilianPathIntersection, pLegIsect)
	{
		if (pLegIsect->isectionType == EIntersectionType_NONE || pLegIsect->bIsMidIntersection)
			continue;

		FOR_EACH_IN_EARRAY(pLegIsect->legIntersects, PathLegIntersect, pli)
		{
			Vec3 vSt, vEnd, vIsectPt;
			bool bStart = (pli->leg->nextInt != pLegIsect);

			if (!acgPathPoints_ShouldLineClipToLeg(pPathPoint, pli->leg, pLegIsect) )
				continue;

			aiCivLeg_GetLegCornerPoints(pli->leg, bStart, vSt, vEnd);
			// get the line that we need to clip against

			if (lineSegLineSeg2dIntersection(vSt, vEnd, pPathPoint->vPos, pNextPt->vPos, vIsectPt))
			{
				// we found an intersection, check if either of the points is close enough to just snap to the 
				// intersection point

				AICivilianPathPoint *pClosest;
				F32 fClosestDistSQ; 
				F32 fPathDistSQ = distance3SquaredXZ(vIsectPt, pPathPoint->vPos);
				F32 fNextDistSQ = distance3SquaredXZ(vIsectPt, pNextPt->vPos);
				 
				if (fPathDistSQ < fNextDistSQ)
				{
					fClosestDistSQ = fPathDistSQ;
					pClosest = pPathPoint;
				}
				else 
				{
					fClosestDistSQ = fNextDistSQ;
					pClosest = pNextPt;
				}
				
				if( fClosestDistSQ < SQR(8.f))
				{
					copyVec3(vIsectPt, pClosest->vPos);
					pClosest->pIntersection = pLegIsect;
				}
				else 
				{
					// we're going to create a new path point and insert it
					AICivilianPathPoint *pNewPathPoint = acgPathPoints_CreateNewAfterPathPoint(pPathPoint, vIsectPt);
					pNewPathPoint->pIntersection = pLegIsect;
					pNextPt = pNewPathPoint;
				}
		}

		}
		FOR_EACH_END		

	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
void acgPathPoints_CreateIntersectionStopPoints()
{
	// loop over all the pathPoints, and check if the connection crosses a car intersection. 
	// if so we need to clip it
	FOR_EACH_IN_EARRAY(s_acgProcess.eaPathPoints, AICivilianPathPoint, pPathPoint)
	{
		if (acg_d_pos && distance3SquaredXZ(s_acgDebugPos, pPathPoint->vPos)<SQR(5))
		{
			int xxx = 0;
		}

		acgPathPoints_ClipPathPointToIntersection(pPathPoint);
		
		

	}
	FOR_EACH_END

}

// ------------------------------------------------------------------------------------------------------------------
// returns the previous node back X feet. anything over 0 will at least turn the pPathPoint's previous
static AICivilianPathPoint* acgPathPoint_WalkBackXFeet(AICivilianPathPoint *pPathPoint, F32 fFeet, F32 *pfDistTravelled)
{
	AICivilianPathPoint *pPrev;
	F32 fDist;
	
	if (fFeet <= 0.f)
		return pPathPoint;

	if (!pPathPoint || !pPathPoint->pPrevPathPoint)
		return NULL;

	pPrev = pPathPoint->pPrevPathPoint;
	fDist = distance3(pPathPoint->vPos, pPathPoint->pPrevPathPoint->vPos);
	if (fFeet <= fDist)
	{
		return pPrev;
	}
			
	if (pfDistTravelled) 
		*pfDistTravelled += fDist; 

	return acgPathPoint_WalkBackXFeet(pPrev, fFeet - fDist, pfDistTravelled);
}

// ------------------------------------------------------------------------------------------------------------------
// pre-insert stop waypoints in the trolley path
static void acgPathPoints_FixupStopWaypoints()
{
	S32 i = eaSize(&s_acgProcess.eaPathPoints) - 1; 
	for (; i >= 0; --i)
	{
		AICivilianPathPoint *pStopWayPt = s_acgProcess.eaPathPoints[i];
	
		if (pStopWayPt->deleted || pStopWayPt->isectionFixupFlag || !pStopWayPt->pIntersection)
			continue;

		if (acg_d_pos && distance3SquaredXZ(s_acgDebugPos, pStopWayPt->vPos)<SQR(5))
		{
			int xxx = 0;
		}


		{
			AICivilianPathPoint *pStopPointInsert;
			F32 fDistWalked = 0.f;
			F32 fInsertDist = 0.f;
			
			pStopPointInsert = acgPathPoint_WalkBackXFeet(pStopWayPt, s_fCivTrolleyStopDistance, &fDistWalked);

			if (!pStopPointInsert)
				continue;
			assert(pStopPointInsert->pNextPathPoint);

			fInsertDist = s_fCivTrolleyStopDistance - fDistWalked;
			{
				Vec3 vDir, vNewPos; 
				AICivilianPathPoint *pNewPathPoint;				

				acgPathPoints_GetLeafPathDir(pStopPointInsert, vDir);
				scaleAddVec3(vDir, -fInsertDist, pStopPointInsert->pNextPathPoint->vPos, vNewPos);

				pNewPathPoint = acgPathPoints_CreateNewAfterPathPoint(pStopPointInsert, vNewPos);
				if (!pNewPathPoint)
					continue;


				{
					AICivilianPathPoint *pRemPathPoint = pNewPathPoint->pNextPathPoint;
					while(pRemPathPoint && !pRemPathPoint->pIntersection)
					{
						AICivilianPathPoint *ptmp = pRemPathPoint;

						
						pRemPathPoint = pRemPathPoint->pNextPathPoint;
						
						ptmp->deleted = true;
						ptmp->pNextPathPoint = NULL;
						ptmp->pPrevPathPoint = NULL;
					}

					pNewPathPoint->pNextPathPoint = pStopWayPt;
					pStopWayPt->pPrevPathPoint = pNewPathPoint;
				}

			}
		}
	}

	acgPathPoints_RemoveDeleted();
}

static void acgPathPoints_FindReversalWayPoints()
{
	S32 i = eaSize(&s_acgProcess.eaPathPoints) - 1; 
	for (; i >= 0; --i)
	{
		AICivilianPathPoint *pPPt = s_acgProcess.eaPathPoints[i];

		if (pPPt->deleted)
			continue;

		if (acg_d_pos && distance3SquaredXZ(s_acgDebugPos, pPPt->vPos)<SQR(5))
		{
			int xxx = 0;
		}

		if (pPPt->pPrevPathPoint && pPPt->pNextPathPoint)
		{
			Vec3 vToPrev, vToNext;
			F32 fAngle;
			subVec3(pPPt->pPrevPathPoint->vPos, pPPt->vPos, vToPrev);
			subVec3(pPPt->pNextPathPoint->vPos, pPPt->vPos, vToNext);
			
			fAngle = getAngleBetweenVec3(vToPrev, vToNext);
			if (fAngle > 0.f && fAngle < RAD(10.f))
			{	// turning point
				pPPt->bIsReversalPoint = true;
			}	

		}

	}
}


static void acgPathPoints_CenterPathPoints(int iPartitionIdx)
{
	FOR_EACH_IN_EARRAY(s_acgProcess.eaPathPoints, AICivilianPathPoint, pPathPoint)
	{
		if (acg_d_pos && distance3SquaredXZ(s_acgDebugPos, pPathPoint->vPos)<SQR(5))
		{
			int xxx = 0;
		}

		{
			Vec3 vTrackPos1, vTrackPos2;
			Vec3 vPerpDir;
			const F32 fTargetDist = 3.5f;
			F32 fDir = 1.f;
			F32 *pvTrackPos = vTrackPos1;
			
			// get the perp dir for this pathpoint
			{
				Vec3 vDir;
				acgPathPoints_GetLeafPathDir(pPathPoint, vDir);
				crossVec3Up(vDir, vPerpDir);
				normalVec3(vPerpDir);
			}
			
			do {
				F32 fCurDist = 0.f;
				F32 avgGroundPos = pPathPoint->vPos[1];

				copyVec3(pPathPoint->vPos, pvTrackPos);

				while (fCurDist < fTargetDist)
				{
					WorldCollCollideResults results = {0};
					Vec3 vCurPos, vStart, vEnd;

					scaleAddVec3(vPerpDir, fCurDist * fDir, pPathPoint->vPos, vCurPos);
					copyVec3(vCurPos, vStart);
					copyVec3(vCurPos, vEnd);
					vStart[1] += 5.f;
					vEnd[1] -= 5.f;
					acgCastRay(iPartitionIdx, &results, vStart, vEnd);
					if (!results.hitSomething)
					{
						break;
					}

					if (fabs(avgGroundPos - results.posWorldImpact[1]) < 0.6f)
					{
						avgGroundPos = (avgGroundPos + results.posWorldImpact[1]) * 0.5f;
					}
					else
					{
						copyVec3(results.posWorldImpact, pvTrackPos);
						break;
					}
					fCurDist += 0.1f;
				}

				pvTrackPos = vTrackPos2;
				fDir = -fDir;

			} while(fDir < 0.f);


			if (distance3Squared(vTrackPos1, vTrackPos2) < SQR(4.2f))
			{
				// could not center
				continue;
			}
			{
				Vec3 vCenter;
				interpVec3(0.5f, vTrackPos1, vTrackPos2, vCenter);
				vCenter[1] = pPathPoint->vPos[1];
				if (acgSnapPosToGround(iPartitionIdx, vCenter, 5.f, -5.f))
				{
					copyVec3(vCenter, pPathPoint->vPos);
				}

			}



		}
		


	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
static void acgPathPoints(int iPartitionIdx)
{
	GamePatrolRoute** eaPatrols = acgPathPoints_GetPatrolRoutes(TROLLEY_PATROL_ROUTE_BASENAME);
	if (!eaPatrols)
		return;
	
	acgPathPoints_CreatePathsFromPatrolRoutes(eaPatrols);
	eaDestroy(&eaPatrols);

	acgPathPoints_FindAndMergePathPoints();
	
	//
	acgPathPoints_CreateIntersectionStopPoints();

	acgPathPoints_FixupStopWaypoints();
	acgPathPoints_FindReversalWayPoints();

	acgPathPoints_CenterPathPoints(iPartitionIdx);

}

// ------------------------------------------------------------------------------------------------------------------

#define DEFAULT_CAR_LANE_WIDTH (20.f)
extern F32 s_fCarTurnAngleThreshold;

// lanes start at 1
// this will clamp to the appropriate lane numbers
static void acgLeg_GetLanePos(const AICivilianPathLeg *leg, S32 lane, F32 fStartToEndRatio, S32 forward, Vec3 vOutPos)
{
	fStartToEndRatio = leg->len * CLAMP(fStartToEndRatio, 0.f, 1.f);
	scaleAddVec3(leg->dir, fStartToEndRatio, leg->start, vOutPos);

	if (leg->max_lanes)
	{
		F32 fDistanceFromLeg;

		lane = CLAMP(lane, 1, leg->max_lanes);
		fDistanceFromLeg = DEFAULT_CAR_LANE_WIDTH * (lane - 0.5f) + leg->median_width * 0.5f;
		if (!forward)
			fDistanceFromLeg = -fDistanceFromLeg;

		if (fStartToEndRatio == 0.f && leg->bSkewed_Start)
		{
			Vec3 vPerpDir;
			F32 fLength = acgLeg_GetSkewedLaneLength(leg);
			F32 fPercent = fDistanceFromLeg / (leg->width * .5f);
			F32 fOffset = fLength * fPercent;

			acgLeg_GetSkewedLaneDirection(leg, vPerpDir);
			scaleAddVec3(vPerpDir, fOffset, vOutPos, vOutPos);

		}
		else
		{
			scaleAddVec3(leg->perp, fDistanceFromLeg, vOutPos, vOutPos);

		}

		
		
	}
}

static bool acgCurveFit_GetDirection(const AICivilianPathLeg * leg, bool bAtStart, bool bGetSkewedDir, Vec3 vDir)
{
	if (bAtStart)
	{
		if (bGetSkewedDir)
		{
			Vec3 vWpDir;
			if (!leg->bSkewed_Start)
				return false;
			acgLeg_GetSkewedLaneDirection(leg, vWpDir);
			crossVec3Up(vWpDir, vDir);
			normalVec3(vDir);
		}
		else
		{
			copyVec3(leg->dir, vDir);
		}
	}
	else
	{
		if (bGetSkewedDir)
			return false;
		copyVec3(leg->dir, vDir);
	}

	return true;
}

static F32 acgCurveFit_DefaultACPI_GetHeuristic(const Vec3 vCurvePt, const Vec3 vLegDir, const Vec3 vOtherLegDir)
{
	return 0.f;
}
static F32 acgCurveFit_DefaultACPI_GetHeuristic2(const Vec3 vCurvePt, const Vec3 vLegPos, const Vec3 vOtherLegPos,
												const Vec3 vLegDir, const Vec3 vOtherLegDir)
{
	Vec3 vLegToPt, vOtherToPt;
	F32 fAngle;
	subVec3(vLegPos, vCurvePt, vLegToPt);
	subVec3(vOtherLegPos, vCurvePt, vOtherToPt);
	fAngle = getAngleBetweenVec3(vLegToPt, vOtherToPt);
	return fAngle;
}

// 
static bool acgLeg_GetLanePosRelativeToACPI(const AICivilianPathLeg *leg, 
											 const AICivilianPathIntersection *acpi, 
											 S32 rhs, S32 lane, Vec3 vOutLanePos)
{
	bool bAtStart;

	if (leg->prevInt == acpi)
	{
		bAtStart = true;
	}
	else
	{
		bAtStart = false;
	}

	rhs = !!rhs;
	

	acgLeg_GetLanePos(leg, lane, (bAtStart) ? 0.f : 1.f, bAtStart ^ rhs, vOutLanePos);
	return bAtStart;
}

static bool acgLeg_GetLanePosRelativeToPos(const AICivilianPathLeg *leg, const Vec3 vPos, 
													S32 rhs, S32 lane, Vec3 vOutLanePos)
{
	F32 legDirMod;
	bool bAtStart;
	acgLegGetClosestEndAndDirMod(leg, vPos, &legDirMod);
	bAtStart = legDirMod < 0.f;

	rhs = !!rhs;

	acgLeg_GetLanePos(leg, lane, (bAtStart) ? 0.f : 1.f, bAtStart ^ rhs, vOutLanePos);
	return bAtStart;
}

static bool acgCurveFit_ValidateCurvePoint(const Vec3 vIsectPos, const AICivilianPathIntersection *acpi)
{
	Vec3 vMidPos;
	F32 radius, dist;
	interpVec3(0.5f, acpi->min, acpi->max, vMidPos);
	radius = distance3(acpi->min, acpi->max);
	dist = distance3(vIsectPos, vMidPos);

	return (dist < radius * 1.5f);
}

static void acgCurveFit_ProcessForLanes(int iPartitionIdx, AICivilianPathLeg * leg, S32 legLane, bool bLegAtStart, const Vec3 vLegLanePos, 
										const AICivilianPathLeg *destLeg, AICivilianPathIntersection *acpi)
{
	F32 fAngle = getAngleBetweenNormalizedVec3(leg->dir, destLeg->dir);
	S32 destLane;
	bool bDestLegAtStart;

	fAngle = acgMath_GetAngleBetweenNormsAbs(leg->dir, destLeg->dir);
	// legs aren't angled enough
	if (fAngle < s_fCarTurnAngleThreshold)
	{
		return;
	}
	
	
	for (destLane = 1; destLane < destLeg->max_lanes + 1; ++destLane)
	{
		Vec3 vOtherLegLanePos;
		Vec3 vIsectPos;
		
		F32 fBestHeuristic = -FLT_MAX;
		Vec3 vBestCurvePos;
		zeroVec3(vBestCurvePos);

		// get our lane position
		// acgLeg_GetLanePos(destLeg, destLane, (bDestLegAtStart) ? 0.f : 1.f, bDestLegAtStart, vOtherLegLanePos);
		bDestLegAtStart = acgLeg_GetLanePosRelativeToACPI(destLeg, acpi, false, destLane, vOtherLegLanePos);

		{
			// two different directions
			Vec3 vLegDir, vDestLegDir;
			F32 fHeuristic;
			bool bGetSkewedStart = false;
			
			do {
				if (acgCurveFit_GetDirection(leg, bLegAtStart, bGetSkewedStart, vLegDir))
				{
					bool bOtherGetSkewedStart = false;

					do {
						if (acgCurveFit_GetDirection(destLeg, bDestLegAtStart, bOtherGetSkewedStart, vDestLegDir))
						{
							if (acgLineLine2dIntersection(vLegLanePos, vLegDir, vOtherLegLanePos, vDestLegDir, vIsectPos))
							{
								if (acgCurveFit_ValidateCurvePoint(vIsectPos, acpi))
								{
									// get the heuristic for this leg
									fHeuristic = acgCurveFit_DefaultACPI_GetHeuristic2(vIsectPos, vLegLanePos, vOtherLegLanePos,
										vLegDir, vDestLegDir);

									if (fHeuristic > RAD(45.f))
									{	
										if (fHeuristic > fBestHeuristic)
										{
											fBestHeuristic = fHeuristic;
											copyVec3(vIsectPos, vBestCurvePos);
										}
									}
								}
							}

						}
						
						bOtherGetSkewedStart = !bOtherGetSkewedStart;
					} while (bOtherGetSkewedStart);
				}
				bGetSkewedStart = !bGetSkewedStart;
			} while(bGetSkewedStart);
			
		}


		if (fBestHeuristic != -FLT_MAX)
		{
			AICivIntersectionCurve *pIsectCurve = calloc(1, sizeof(AICivIntersectionCurve));

			pIsectCurve->legSource = leg;
			pIsectCurve->sourceLane = legLane;
			pIsectCurve->legDest = (AICivilianPathLeg*)destLeg;
			pIsectCurve->destLane = destLane;
			
			copyVec3(vBestCurvePos, pIsectCurve->vCurvePoint);
			acgSnapPosToGround(iPartitionIdx, pIsectCurve->vCurvePoint, 20, -100);

			eaPush(&s_acgProcess.eaIntersectionCurves, pIsectCurve);
		}
		
	}
}


static void acgCurveFitIntersections(int iPartitionIdx, AICivilianPathIntersection *acpi)
{
	Vec3 vIsectMid;
	aiCivIntersection_GetBoundingMidPos(acpi, vIsectMid);

	FOR_EACH_IN_EARRAY(acpi->legIntersects, PathLegIntersect, pli)
	{
		AICivilianPathLeg *leg;
		S32 i;
		// bool bAtStart;
		leg = pli->leg;


		for (i = 1; i < leg->max_lanes + 1; ++i)
		{
			Vec3 vLanePos;
			bool bAtStart;
			// get our lane position
			// acgLeg_GetLanePos(leg, i, (bAtStart) ? 0.f : 1.f, !bAtStart, vLanePos);
			bAtStart = acgLeg_GetLanePosRelativeToACPI(leg, acpi, true, i, vLanePos);

			if (acg_d_pos && distance3SquaredXZ(s_acgDebugPos, vLanePos) < SQR(30.f))
			{
				int bbb = 0;
			}

			// for every other intersection leg lane
			{
				FOR_EACH_IN_EARRAY(acpi->legIntersects, PathLegIntersect, otherpli)
				{
					if (otherpli == pli)
						continue;
					
					acgCurveFit_ProcessForLanes(iPartitionIdx, pli->leg, i, bAtStart, vLanePos, otherpli->leg, acpi);
				}
				FOR_EACH_END

			}
			
				
		}
		
	}
	FOR_EACH_END
}

static void acgCurveFit(int iPartitionIdx)
{
	// 
	FOR_EACH_IN_EARRAY(s_acgPathInfo.intersects, AICivilianPathIntersection, acpi)
	{
		if (acpi->legIntersects[0]->leg->type != EAICivilianLegType_CAR)
			continue; 

		if (eaSize(&acpi->legIntersects) > 2)
		{
			acgCurveFitIntersections(iPartitionIdx, acpi);
		}
	}
	FOR_EACH_END

}


// ------------------------------------------------------------------------------------------------------------------
static void acgProcessShutdown(void)
{
	acgNodeBlocksFree();

	eaDestroy(&s_acgProcess.genBlockList);
	eaDestroy(&s_acgProcess.genStartList);
	eaDestroy(&s_acgProcess.edgeStartList);
	eaDestroy(&s_acgProcess.legList);
	eaDestroy(&s_acgProcess.eaPathPoints);
	eaDestroy(&s_acgProcess.eaPathPointIntersections);
	eaDestroy(&s_acgProcess.eaIntersectionCurves);
	
	eaDestroyEx(&s_acgProcess.deletedLegs, acgPathLeg_Free);
	eaDestroyEx(&s_acgProcess.deletedNodes, NULL);
	eaDestroyEx(&s_acgProcess.deletedLines, NULL);
	eaDestroyEx(&s_acgProcess.lineList, acgDestroyPathGenLine);
	eaDestroy(&s_acgProcess.lastWorldList);
	eaDestroy(&s_acgProcess.split.eaNeighborList);
	eaDestroy(&s_acgProcess.split.eaLeftNeighborList);
	eaDestroy(&s_acgProcess.split.eaRightNeighborList);

	StructDestroySafe(parse_SavedTaggedGroups, &s_acgProcess.pTagData);
	
	ZeroStruct(&s_acgProcess);

	MP_DESTROY(AICivilianPathGenNodeBlock);
	MP_DESTROY(AICivilianPathGenNode);
	MP_DESTROY(AICivilianPathGenLine);
	MP_DESTROY(AICivilianPathGenPair);

	mpCompactPools();
}

// ------------------------------------------------------------------------------------------------------------------
static int acgProcessFinal(Entity *debugger)
{
	int i;
	AICivilianRuntimePathInfo *pPathInfo = &s_acgPathInfo;
	
	
	for(i=0; i<eaSize(&s_acgProcess.legList); i++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		assert(leg->type > EAICivilianLegType_NONE && leg->type < EAICivilianLegType_COUNT);
		eaPush(&pPathInfo->legs[leg->type], leg);
	}

	eaPushEArray(&pPathInfo->eaPathPoints, &s_acgProcess.eaPathPoints);
	eaClear(&s_acgProcess.eaPathPoints);

	eaPushEArray(&pPathInfo->eaPathPointIntersects, &s_acgProcess.eaPathPointIntersections);
	eaClear(&s_acgProcess.eaPathPointIntersections);

	eaPushEArray(&pPathInfo->eaIntersectionCurves, &s_acgProcess.eaIntersectionCurves);
	eaClear(&s_acgProcess.eaIntersectionCurves);

	//eaPushEArray(&g_aiCivilianState.deletedLegs, &s_acgProcess.deletedLegs);
	eaClear(&s_acgProcess.deletedLegs);

	//eaPushEArray(&g_aiCivilianState.deletedNodes, &s_acgProcess.deletedNodes);
	eaClear(&s_acgProcess.deletedNodes);

	//eaPushEArray(&g_aiCivilianState.deletedLines, &s_acgProcess.deletedLines);
	eaClear(&s_acgProcess.deletedLines);


	acgProcessShutdown();

	return 1;
}



// ------------------------------------------------------------------------------------------------------------------
void acgSendLeg(Entity *debugger, AICivilianPathLeg *leg, U32 color)
{
	Vec3 p1, p2, p3, p4;

	if(s_sendCivQuads)
	{
		legToQuad(leg, p1, p2, p3, p4);
		wlAddClientQuad(debugger, p1, p2, p3, p4, color);
	}
	legToLine(leg, p1, p2);
	wlAddClientLine(debugger, p1, p2, color);
}


// __forceinline static F32 acgLeg_GetSkewedLan

static void acgLeg_GetSkewedLaneOffset(AICivilianPathLeg *leg, S32 lane, Vec3 vLanePos)
{
	Vec3 vSkewedLaneDir;
	F32 fDist;
	
	acgLeg_GetSkewedLaneDirection(leg, vSkewedLaneDir);
	
	{
		F32 fLength = acgLeg_GetSkewedLaneLength(leg);
		F32 fPercent, fOffset;
		//fDist = (lane - 1) * leg->lane_width + leg->lane_width * .5f + leg->median_width * .5f;
		fDist = (lane - 1) * leg->lane_width + leg->lane_width * .5f + leg->median_width * .5f;
		fPercent = fDist / (leg->width * .5f);
		
		fOffset = fLength * fPercent;
		scaleAddVec3(vSkewedLaneDir, fOffset, leg->start, vLanePos);
	}
	
}


// ------------------------------------------------------------------------------------------------------------------
static void acgSendLegs(Entity *debugger)
{
	int i;

	if(eaSize(&s_acgProcess.legList))
	{
		for(i=0; i<eaSize(&s_acgProcess.legList); i++)
		{
			AICivilianPathLeg *leg = s_acgProcess.legList[i];
			acgSendLeg(debugger, leg, acgGetTypeColor(leg->type, 0));
		}
	}
	else
	{
		AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
		AICivilianRuntimePathInfo *pPathInfo;
		if (!pPartition)
			return;

		pPathInfo = &pPartition->pathInfo;
		
		for(i = 0; i < EAICivilianLegType_COUNT; i++)
		{
			int j;
			
			for(j = 0; j < eaSize(&pPathInfo->legs[i]); j++)
			{
				Vec3 s, e;
				AICivilianPathLeg *leg = pPathInfo->legs[i][j];
				acgSendLeg(debugger, leg, acgGetTypeColor(leg->type, 0));

				if(leg->median_width)
				{
					centerVec3(leg->start, leg->end, s);
					s[1] += 3;
					scaleAddVec3(leg->perp, leg->median_width/2, s, e);
					scaleAddVec3(leg->perp, -leg->median_width/2, s, s);

					wlAddClientLine(debugger, s, e, 0xffff00ff);
				}

				if(leg->bSkewed_Start)
				{
					Vec3 vDir;
					F32 skewedRoadWidth = acgLeg_GetSkewedLaneLength(leg);

					acgLeg_GetSkewedLaneDirection(leg, vDir);
					// setVec3(vDir, sinf(leg->fSkewedAngle_Start), 0, cosf(leg->fSkewedAngle_Start));
					scaleAddVec3(vDir, skewedRoadWidth, leg->start, s);
					scaleAddVec3(vDir, -skewedRoadWidth, leg->start, e);
					s[1] += 3;
					e[1] += 3;
	
					wlAddClientLine(debugger, s, e, 0xff0080ff);


					if (leg->bSkewed_Start)
					{
						#define WL_COLORRED 0xFFFF0000
						Vec3 vLanePos;
						acgLeg_GetSkewedLaneOffset(leg, 1, vLanePos);
						vLanePos[1] += 3;
						wlAddClientPoint(debugger, vLanePos, WL_COLORRED);

						acgLeg_GetSkewedLaneOffset(leg, 2, vLanePos);
						vLanePos[1] += 3;
						wlAddClientPoint(debugger, vLanePos, WL_COLORRED);
					}
					
					
				}

				// Send connections
				if(leg->next)
				{
					acgLegLegDistSquared(leg, s, leg->next, e);
					s[1] += 3.1;
					e[1] += 2.9;
					wlAddClientLine(debugger, s, e, 0xffff00ff);
				}

				if(leg->prev)
				{
					acgLegLegDistSquared(leg, s, leg->prev, e);
					s[1] += 3.1;
					e[1] += 2.9;
					wlAddClientLine(debugger, s, e, 0xffff00ff);
				}

				if (leg->pCrosswalkNearestRoad)
				{
					acgLegLegDistSquared(leg, s, leg->pCrosswalkNearestRoad, e);
					s[1] += 3.1;
					e[1] += 2.9;
					wlAddClientLine(debugger, s, e, 0xFFFFFF00);
				}

				if (s_acgShowGroundCoplanar)
				{
					U32 uColor;
					F32 fHalfLen = leg->len * 0.5f;
					scaleAddVec3(leg->dir, fHalfLen, leg->start, s);
					s[1] += 4.2;

					if (leg->bIsGroundCoplanar) uColor = 0xFF00FF00;
					else uColor = 0xFFFF0000;

					wlAddClientPoint(debugger, s, uColor);
				}
			}
		}
				
		for(i=0; i<eaSize(&pPathInfo->intersects); i++)
		{
			AICivilianPathIntersection *acpi = pPathInfo->intersects[i];
			EAICivilianLegType type = acpi->legIntersects[0]->leg->type;
			S32 x;

			for (x = 0; x < eaSize(&acpi->legIntersects); x++)
			{
				S32 j;
				for(j=x+1; j<eaSize(&acpi->legIntersects); j++)
				{
					PathLegIntersect *pli1 = acpi->legIntersects[x];
					PathLegIntersect *pli2 = acpi->legIntersects[j];
					Vec3 p1, p2;

					acgLegLegDistSquared(pli1->leg, p1, pli2->leg, p2);

					p1[1] += 3;
					p2[1] += 3;

					p1[1] -= 0.5;
					p2[1] += 0.5;

					wlAddClientLine(debugger, p2, p1,acgGetTypeColor(type, 0));

					p1[1] += 1.0;
					p2[1] -= 1.0;
					wlAddClientLine(debugger, p2, p1, acgGetTypeColor(type, 0));
				}
			}

			
		}

		// todo: where should the deleted things live
		/*
		if (s_debugDrawDeletedLegs)
		{
			if(acg_d_pos)
			{
				for(i=0; i<eaSize(&g_aiCivilianState.path_info->deletedNodes); i++)
				{
					ACGDelNode *dn = g_aiCivilianState.path_info->deletedNodes[i];

					if(distance3SquaredXZ(s_acgDebugPos, dn->pos)<SQR(500))
						wlAddClientPoint(debugger, dn->pos, acgGetTypeColor(dn->type, 1));
				}
			}

			for(i=0; i<eaSize(&g_aiCivilianState.path_info->deletedLines); i++)
			{
				ACGDelLine *dl = g_aiCivilianState.path_info->deletedLines[i];

				wlAddClientLine(debugger, dl->start, dl->end, acgGetTypeColor(dl->type, 1));
			}

			for(i=0; i<eaSize(&g_aiCivilianState.path_info->deletedLegs); i++)
			{
				AICivilianPathLeg *leg = g_aiCivilianState.path_info->deletedLegs[i];
				U32 color = acgGetTypeColor(leg->type, 1);

				acgSendLeg(debugger, leg, color);
			}
		}
		*/
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void acgSendIntersectionBounds(Entity *debugger)
{
	S32 i;
	Mat4 mtx;
	AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
	AICivilianRuntimePathInfo *pPathInfo;
	if (!pPartition)
		return;

	pPathInfo = &pPartition->pathInfo;
	identityMat4(mtx);

	for(i=0; i<eaSize(&pPathInfo->intersects); i++)
	{
		AICivilianPathIntersection *acpi = pPathInfo->intersects[i];
		EAICivilianLegType type = acpi->legIntersects[0]->leg->type;

		if (type == EAICivilianLegType_CAR)
		{
			wlAddClientBox(debugger, acpi->min, acpi->max, mtx, 0x7FFF0000);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivSendIntersectionTypes(Entity *debugger)
{
	if (debugger)
	{
		S32 i;
		AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
		AICivilianRuntimePathInfo *pPathInfo;
		if (!pPartition)
			return;
		pPathInfo = &pPartition->pathInfo;

		for(i = 0; i < eaSize(&pPathInfo->intersects); i++)
		{
			AICivilianPathIntersection *acpi = pPathInfo->intersects[i];
			EAICivilianLegType type = acpi->legIntersects[0]->leg->type;

			if (type == EAICivilianLegType_CAR && acpi->isectionType > EIntersectionType_NONE)
			{
				Vec3 vMid;
				U32 color = 0;
				switch(acpi->isectionType)
				{
					acase EIntersectionType_SIDESTREET_STOPSIGN:
						color = 0xFFFF0000;
					xcase EIntersectionType_STOPSIGN:
						color = 0xFF00FF00;
					xcase EIntersectionType_3WAY_STOPLIGHT:
						color = 0xFF0000FF;
					xcase EIntersectionType_4WAY_STOPLIGHT:
						color = 0xFF0007FF;
				}

				interpVec3(0.5f, acpi->min, acpi->max, vMid);
				vMid[1] += 3.0f;
				wlAddClientPoint(debugger, vMid, color);
			}
		}
	}


}

// ------------------------------------------------------------------------------------------------------------------
static void _acgDrawXAtLocation(Entity *debugger, const Vec3 vPos, U32 color)
{
	Vec3 vPt1, vPt2;

	scaleAddVec3(sidevec, 3.f, vPos, vPt1);
	scaleAddVec3(sidevec, -3.f, vPos, vPt2);
	vPt1[1] += 3.f;
	vPt2[1] += 3.f;
	wlAddClientLine(debugger, vPt1, vPt2, 0xFFFFFFFF);

	scaleAddVec3(forwardvec, 3.f, vPos, vPt1);
	scaleAddVec3(forwardvec, -3.f, vPos, vPt2);
	vPt1[1] += 3.f;
	vPt2[1] += 3.f;
	wlAddClientLine(debugger, vPt1, vPt2, 0xFFFFFFFF);
}

// ------------------------------------------------------------------------------------------------------------------
static void _acgSendPathPoints(Entity *debugger)
{
	if (debugger)
	{
		AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
		AICivilianRuntimePathInfo *pPathInfo;
		F32 fHeightOffset = 3.f;

		if (!pPartition)
			return;
		pPathInfo = &pPartition->pathInfo;
		
		FOR_EACH_IN_EARRAY(pPathInfo->eaPathPoints, AICivilianPathPoint, pPathPoint)
		{
			Vec3 vPt1, vPt2;
			U32 color;
			
			scaleAddVec3(forwardvec, fHeightOffset, pPathPoint->vPos, vPt1);
			scaleAddVec3(forwardvec, -fHeightOffset, pPathPoint->vPos, vPt2);
			vPt1[1] += fHeightOffset;
			vPt2[1] += fHeightOffset;
			if (pPathPoint->bIsReversalPoint)
				color = 0xFFFF00FF;
			else if (!pPathPoint->pIntersection)
				color = 0xFF0000FF;
			else
				color = 0xFFFFFF00;

			wlAddClientLine(debugger, vPt1, vPt2, color);
			
			scaleAddVec3(sidevec, fHeightOffset, pPathPoint->vPos, vPt1);
			scaleAddVec3(sidevec, -fHeightOffset, pPathPoint->vPos, vPt2);
			vPt1[1] += 3.f;
			vPt2[1] += 3.f;
			wlAddClientLine(debugger, vPt1, vPt2, color);
			
			if (pPathPoint->pNextPathPoint)
			{
				copyVec3(pPathPoint->vPos, vPt1);
				copyVec3(pPathPoint->pNextPathPoint->vPos, vPt2);
				vPt1[1] += fHeightOffset;
				vPt2[1] += fHeightOffset;
				wlAddClientLine(debugger, vPt1, vPt2, 0xFF00FF00);
			}

		}
		FOR_EACH_END

		fHeightOffset = 5.f;
		FOR_EACH_IN_EARRAY(pPathInfo->eaPathPointIntersects, AICivilianPathPointIntersection, pPPIsect)
		{
			if (eaSize(&pPPIsect->eaPathPoints) > 0)
			{
				_acgDrawXAtLocation( debugger, pPPIsect->eaPathPoints[0]->vPos, 0xFFFFFFFF);
			}
		}
		FOR_EACH_END

	}
}

// ------------------------------------------------------------------------------------------------------------------
static void _acgSendCarCurvePoints(Entity *debugger)
{
	if (debugger && g_civSharedState.pCivPathInfo)
	{
		F32 fHeightOffset = 3.f;
		
		FOR_EACH_IN_EARRAY(g_civSharedState.pCivPathInfo->eaIntersectionCurves, AICivIntersectionCurve, pcurve)
		{
			//
			Vec3 vSrcLanePos, vDstLanePos;
			
			Vec3 vPt1, vPt2;
			
			acgLeg_GetLanePosRelativeToPos(pcurve->legSource, pcurve->vCurvePoint, 
											true, pcurve->sourceLane, vSrcLanePos);
			acgLeg_GetLanePosRelativeToPos(pcurve->legDest, pcurve->vCurvePoint, 
											false, pcurve->destLane, vDstLanePos);


			copyVec3(vSrcLanePos, vPt1);
			copyVec3(pcurve->vCurvePoint, vPt2);
			vPt1[1] += fHeightOffset;
			vPt2[1] += fHeightOffset;
			wlAddClientLine(debugger, vPt1, vPt2, 0xFFFF0000);

			copyVec3(pcurve->vCurvePoint, vPt1);
			copyVec3(vDstLanePos, vPt2);
			vPt1[1] += fHeightOffset;
			vPt2[1] += fHeightOffset;
			wlAddClientLine(debugger, vPt1, vPt2, 0xFFFF0000);


			_acgDrawXAtLocation( debugger, pcurve->vCurvePoint, 0xFFFFFFFF);

		}
		FOR_EACH_END

	}
	
}


// ------------------------------------------------------------------------------------------------------------------
static void acgBuildMapInfo(AICivilianPathMapInfo *info)
{
	// Build current view of map
	// TODO(AM): Map objects on different runs are not the same
	// info->mapCRC = wcGenerateCollCRC(0, s_civlogcrc, 0, 0);
	info->mapCRC = 0;
	info->procVersion = s_acgProcessVersion;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgWriteFile(Entity *debugger)
{
	const char *filename;
		
	if(!g_civSharedState.pCivPathInfo)
		g_civSharedState.pCivPathInfo = StructCreate(parse_AICivilianPathInfo);
		
	acgInitPathInfo(&s_acgPathInfo, g_civSharedState.pCivPathInfo);

	if(!s_ignoreUptodate)
	{	// Only do this for people who aren't regenerating over and over again
		filename = aiCivilianGetSuffixedFileName(1, 0, CIV_FILE_SUFFIX);
		ParserWriteTextFile(filename, parse_AICivilianPathInfo, g_civSharedState.pCivPathInfo, 0, 0);
	}

	filename = aiCivilianGetSuffixedFileName(1, 1, CIV_FILE_SUFFIX);
	ParserWriteBinaryFile(filename, NULL, parse_AICivilianPathInfo, g_civSharedState.pCivPathInfo,NULL,  NULL, NULL, NULL, 0, 0, NULL, 0, 0);
	binNotifyTouchedOutputFile(filename);

	
	if(!g_civSharedState.map_info)
		g_civSharedState.map_info = StructCreate(parse_AICivilianPathMapInfo);

	acgBuildMapInfo(g_civSharedState.map_info);
	filename = aiCivilianGetSuffixedFileName(1, 1, "map_info");

	ParserWriteTextFile(filename, parse_AICivilianPathMapInfo, g_civSharedState.map_info, 0, 0);
	binNotifyTouchedOutputFile(filename);
}

// ------------------------------------------------------------------------------------------------------------------
int acgCheckMapInfo(void)
{
	const char *filename = aiCivilianGetSuffixedFileName(1, 1, "map_info");

	if(fileExists(filename))
	{
		AICivilianPathMapInfo cur_info = {0};
		
		if(!g_civSharedState.map_info)
			g_civSharedState.map_info = StructCreate(parse_AICivilianPathMapInfo);

		ParserReadTextFile(filename, parse_AICivilianPathMapInfo, g_civSharedState.map_info, 0);

		acgBuildMapInfo(&cur_info);
		if(!memcmp(&cur_info, g_civSharedState.map_info, sizeof(cur_info)) && !worldRebuiltBins())
			return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
#define SIMPLE_WRITE(x) fwrite(&x, 1, sizeof(x), file)
#define SIMPLE_READ(x) fread(&x, 1, sizeof(x), file)

static void acgLoadNode(AICivilianPathGenNode *node, FILE *file)
{
	SIMPLE_READ(node->block_x); SIMPLE_READ(node->block_z);
	SIMPLE_READ(node->grid_x); SIMPLE_READ(node->grid_z);
	SIMPLE_READ(node->type_y_coord);
}

static void acgSaveNode(AICivilianPathGenNode *node, FILE *file)
{
	SIMPLE_WRITE(node->block_x); SIMPLE_WRITE(node->block_z);
	SIMPLE_WRITE(node->grid_x); SIMPLE_WRITE(node->grid_z);
	SIMPLE_WRITE(node->type_y_coord);
}

// ------------------------------------------------------------------------------------------------------------------
int acgSaveNodeBlockEdges(AICivilianPathGenNodeBlock *block, FILE *file, int write)
{
	int i, j;
	int count = 0;

	for(i=0; i<NODE_BLOCK_SIZE; i++)
	{
		for(j=0; j<NODE_BLOCK_SIZE; j++)
		{
			AICivilianPathGenNode *node = block->nodes[i][j];

			if(node && node->type == EAICivilianLegType_NONE)
			{
				if(write)
					acgSaveNode(node, file);
				count++;
			}
		}
	}

	return count;
}

// ------------------------------------------------------------------------------------------------------------------
void acgFreeACGNodeFileContents(ACGNodeFileContents *contents)
{
	if (contents->paOtherNodes)
	{
		free(contents->paOtherNodes);
	}
	if (contents->paEdgeStartNodes)
	{
		free(contents->paEdgeStartNodes);
	}

	ZeroStruct(contents);
}

// ------------------------------------------------------------------------------------------------------------------
int acgLoadGridNodesEx(ACGNodeFileContents *pACGNodeFile)
{
	int i;
	const char *filename = aiCivilianGetSuffixedFileName(1, 0, CIV_NODE_SUFFIX);
	FILE* file;

	acgFreeACGNodeFileContents(pACGNodeFile);

	file = fileOpen(filename, "rb");

	if(!file)
		return 0;

	SIMPLE_READ(pACGNodeFile->grid_max[0]); SIMPLE_READ(pACGNodeFile->grid_max[1]);
	SIMPLE_READ(pACGNodeFile->grid_min[0]); SIMPLE_READ(pACGNodeFile->grid_min[1]);

	SIMPLE_READ(pACGNodeFile->edgeOtherCount);
	pACGNodeFile->paOtherNodes = calloc(pACGNodeFile->edgeOtherCount, sizeof(AICivilianPathGenNode));

	for(i=0; i < pACGNodeFile->edgeOtherCount; i++)
	{
		AICivilianPathGenNode *node = &pACGNodeFile->paOtherNodes[i];
		acgLoadNode(node, file);
		devassert(node->type==EAICivilianLegType_NONE);
	}

	SIMPLE_READ(pACGNodeFile->edgeStartCount);
	pACGNodeFile->paEdgeStartNodes = calloc(pACGNodeFile->edgeStartCount, sizeof(AICivilianPathGenNode));

	for(i=0; i < pACGNodeFile->edgeStartCount; i++)
	{
		AICivilianPathGenNode *node = &pACGNodeFile->paEdgeStartNodes[i];
		acgLoadNode(node, file);
	}

	fileClose(file);
	return 1;
}


// ------------------------------------------------------------------------------------------------------------------
int acgSetupGridNodesFromFileContents(const ACGNodeFileContents *pACGNodeFile)
{
	int i;
	int size;
	int max_grid_x, max_grid_z;

	int edgeStartCount = 0;

	acgNodeBlocksFree();

	s_acgNodeBlocks.grid_max[0] = pACGNodeFile->grid_max[0];
	s_acgNodeBlocks.grid_max[1] = pACGNodeFile->grid_max[1];

	s_acgNodeBlocks.grid_min[0] = pACGNodeFile->grid_min[0];
	s_acgNodeBlocks.grid_min[1] = pACGNodeFile->grid_min[1];

	max_grid_x = s_acgNodeBlocks.grid_max[0] - s_acgNodeBlocks.grid_min[0]+1;
	max_grid_z = s_acgNodeBlocks.grid_max[1] - s_acgNodeBlocks.grid_min[1]+1;

	size = max_grid_x*max_grid_z;
	s_acgNodeBlocks.aiCivGenNodeBlocks = calloc(size, sizeof(AICivilianPathGenNodeBlock*));

	for(i=0; i< pACGNodeFile->edgeOtherCount; i++)
	{
		AICivilianPathGenNode *node = acgNodeAlloc();
		const AICivilianPathGenNode *pOtherNode = &pACGNodeFile->paOtherNodes[i];
		AICivilianPathGenNodeBlock *block;

		{
			node->block_x = pOtherNode->block_x;
			node->block_z = pOtherNode->block_z;
			node->grid_x = pOtherNode->grid_x;
			node->grid_z = pOtherNode->grid_z;
			node->type_y_coord = pOtherNode->type_y_coord;
		}

		devassert(node->type == EAICivilianLegType_NONE);
		block = acgGeneratorGetNodeBlockByGrid(node->grid_x, node->grid_z, 1);
		block->nodes[node->block_x][node->block_z] = node;
	}

	for(i=0; i < pACGNodeFile->edgeStartCount; i++)
	{
		AICivilianPathGenNode *node = acgNodeAlloc();
		const AICivilianPathGenNode *pEdgeStart = &pACGNodeFile->paEdgeStartNodes[i];
		AICivilianPathGenNodeBlock *block;

		{
			node->block_x = pEdgeStart->block_x;
			node->block_z = pEdgeStart->block_z;
			node->grid_x = pEdgeStart->grid_x;
			node->grid_z = pEdgeStart->grid_z;
			node->type_y_coord = pEdgeStart->type_y_coord;
		}

		block = acgGeneratorGetNodeBlockByGrid(node->grid_x, node->grid_z, 1);
		block->nodes[node->block_x][node->block_z] = node;
		eaPush(&s_acgProcess.edgeStartList, node);
	}


	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
void acgLoadDeletedNodes()
{
	const char *filename = aiCivilianGetSuffixedFileName(1, 0, "civ_del_n");

	ParserOpenReadBinaryFile(NULL, filename, parse_ACGDelNodeList, &s_acgProcess.deletedNodes,
								NULL, NULL, NULL, NULL, 0, 0, 0);
}

// ------------------------------------------------------------------------------------------------------------------
int acgLoadGridNodes()
{
	int ret;

	if (!s_acgFileNodeContents.paOtherNodes && !s_acgFileNodeContents.paEdgeStartNodes)
	{
		acgLoadGridNodesEx(&s_acgFileNodeContents);
	}

	ret = acgSetupGridNodesFromFileContents(&s_acgFileNodeContents);

	acgLoadDeletedNodes();

	if (s_cachePartialFiles == 0)
	{
		acgFreeACGNodeFileContents(&s_acgFileNodeContents);
	}

	return ret;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSaveGridNodes(void)
{
	int i, j;
	int max_grid_x, max_grid_z;
	const char *filename = aiCivilianGetSuffixedFileName(1, 0, CIV_NODE_SUFFIX);
	FILE* file;
	int edgeOtherCount = 0;
	int edgeStartCount = 0;

	file = fileOpen(filename, "wb");

	SIMPLE_WRITE(s_acgNodeBlocks.grid_max[0]); SIMPLE_WRITE(s_acgNodeBlocks.grid_max[1]);
	SIMPLE_WRITE(s_acgNodeBlocks.grid_min[0]); SIMPLE_WRITE(s_acgNodeBlocks.grid_min[1]);

	max_grid_x = s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1;
	max_grid_z = s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]+1;

	// Count other edge nodes
	for(i=0; i<max_grid_x; i++)
	{
		for(j=0; j<max_grid_z; j++)
		{
			AICivilianPathGenNodeBlock *block = acgGeneratorGetNodeBlockByGrid(i, j, 0);

			if(block)
				edgeOtherCount += acgSaveNodeBlockEdges(block, file, 0);
		}
	}

	SIMPLE_WRITE(edgeOtherCount);
	for(i=0; i<max_grid_x; i++)
	{
		for(j=0; j<max_grid_z; j++)
		{
			AICivilianPathGenNodeBlock *block = acgGeneratorGetNodeBlockByGrid(i, j, 0);

			if(block)
				acgSaveNodeBlockEdges(block, file, 1);
		}
	}

	edgeStartCount = eaSize(&s_acgProcess.edgeStartList);
	SIMPLE_WRITE(edgeStartCount);

	for(i=0; i<eaSize(&s_acgProcess.edgeStartList); i++)
	{
		AICivilianPathGenNode *node = s_acgProcess.edgeStartList[i];
		acgSaveNode(node, file);
	}

	fileClose(file);

	filename = aiCivilianGetSuffixedFileName(1, 0, "civ_del_n");
	ParserWriteBinaryFile(filename, NULL, parse_ACGDelNodeList, &s_acgProcess.deletedNodes,
							NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
}

// ------------------------------------------------------------------------------------------------------------------
static int acgSaveLines(void)
{
	int i;
	int lineCount = 0;
	const char *filename = aiCivilianGetSuffixedFileName(1, 0, "civ_line");
	FILE* file;

	file = fileOpen(filename, "wb");

	if(!file)
		return 0;

	lineCount = eaSize(&s_acgProcess.lineList);
	SIMPLE_WRITE(lineCount);
	ANALYSIS_ASSUME(s_acgProcess.lineList);
	for(i=0; i<lineCount; i++)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];
		int j, nodeCount = eaSize(&line->nodes);

		SIMPLE_WRITE(nodeCount);
		ANALYSIS_ASSUME(line->nodes);
		for(j=0; j<nodeCount; j++)
		{
			AICivilianPathGenNode *node = line->nodes[j];
			SIMPLE_WRITE(node->grid_x); SIMPLE_WRITE(node->grid_z);
			SIMPLE_WRITE(node->block_x); SIMPLE_WRITE(node->block_z);
		}

		SIMPLE_WRITE(line->touched);
	}

	fileClose(file);

	filename = aiCivilianGetSuffixedFileName(1, 0, "civ_del_l");
	ParserWriteBinaryFile(filename, NULL, parse_ACGDelLineList, &s_acgProcess.deletedLines,
							NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
void acgFreeACGLineFileContents(ACGLineFileContents *contents)
{
	if (contents->paLines)
	{
		S32 i;
		for (i = 0; i < contents->lineCount; i++)
		{
			ACGLineFile *line = &contents->paLines[i];
			if (line->paNodes)
			{
				free (line->paNodes);
			}
		}

		free(contents->paLines);
	}

	ZeroStruct(contents);
}

// ------------------------------------------------------------------------------------------------------------------
int acgLoadLinesEx(ACGLineFileContents *pLineFile)
{
	int i;
	const char *filename = aiCivilianGetSuffixedFileName(1, 0, "civ_line");
	FILE* file;

	file = fileOpen(filename, "rb");

	if(!file)
		return 0;

	acgFreeACGLineFileContents(pLineFile);

	SIMPLE_READ(pLineFile->lineCount);
	pLineFile->paLines = calloc(pLineFile->lineCount, sizeof(ACGLineFile));

	for(i=0; i< pLineFile->lineCount; i++)
	{
		ACGLineFile *line = &pLineFile->paLines[i];
		int j;

		SIMPLE_READ(line->nodeCount);
		line->paNodes = calloc(line->nodeCount, sizeof(ACGLineNodeFile));

		for(j=0; j < line->nodeCount; j++)
		{
			ACGLineNodeFile *node = &line->paNodes[j];
			SIMPLE_READ(node->grid_x); SIMPLE_READ(node->grid_z);
			SIMPLE_READ(node->block_x); SIMPLE_READ(node->block_z);
		}

		SIMPLE_READ(line->touched);
	}

	fileClose(file);

	return 1;
}


// ------------------------------------------------------------------------------------------------------------------
int acgSetupLinesFromFileContents(const ACGLineFileContents *pLineFile)
{
	int i;

	for(i=0; i < pLineFile->lineCount; i++)
	{
		ACGLineFile *pFileLine = &pLineFile->paLines[i];
		AICivilianPathGenLine *line = acgLineAlloc();
		int j, nodeCount = 0;

		for(j=0; j < pFileLine->nodeCount; j++)
		{
			ACGLineNodeFile *pFileNode = &pFileLine->paNodes[j];
			AICivilianPathGenNode *node;

			node = acgGeneratorGetNode(pFileNode->grid_x, pFileNode->grid_z, pFileNode->block_x, pFileNode->block_z, 0);
			devassert(node);

			node->line = line;
			eaPush(&line->nodes, node);
		}

		line->touched = pFileLine->touched;
		eaPush(&s_acgProcess.lineList, line);
	}

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
void acgLoadDeletedLineList()
{
	const char *filename = aiCivilianGetSuffixedFileName(1, 0, "civ_del_l");
	ParserOpenReadBinaryFile(NULL, filename, parse_ACGDelLineList, &s_acgProcess.deletedLines,
								NULL, NULL, NULL, NULL, 0, 0, 0);
}


// ------------------------------------------------------------------------------------------------------------------
int acgLoadLines()
{
	int ret;
	if (!s_acgFileLineContents.paLines)
	{
		acgLoadLinesEx(&s_acgFileLineContents);
	}

	ret = acgSetupLinesFromFileContents(&s_acgFileLineContents);

	acgLoadDeletedLineList();

	if (! s_cachePartialFiles)
	{
		acgFreeACGLineFileContents(&s_acgFileLineContents);
	}

	return ret;
}


// ------------------------------------------------------------------------------------------------------------------
static void acgClearPathGenNodeLinePointer(AICivilianPathGenNode *node)
{
	node->line = NULL;
}


typedef struct acgBoundingBox
{
	Quat	qInvRot;
	Vec3	vPos;
	Vec3	vMin;
	Vec3	vMax;
} acgBoundingBox;

__forceinline static bool acgIsPointInABox(const acgBoundingBox **eaBoxList, const Vec3 vPos)
{
	Vec3 vLocalPt, vTmp;
	S32 i = eaSize(&eaBoxList) - 1;
	do{
		const acgBoundingBox *box;
		ANALYSIS_ASSUME(eaBoxList);
		box = eaBoxList[i];

		// transform the point into the local box's space
		subVec3(vPos, box->vPos, vTmp);
		quatRotateVec3(box->qInvRot, vTmp, vLocalPt);

		if (pointBoxCollision(vLocalPt, box->vMin, box->vMax))
			return true;

	}while(--i >= 0);

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgCullLinesToBoundingBoxList(const acgBoundingBox **eaBoxList)
{
	S32 i;

	if (eaSize(&eaBoxList) == 0)
		return;

	/*
	loop backwards through the acg line list and remove any line that does not intersect with the
	cull bounding box
	*/
	for (i = eaSize(&s_acgProcess.lineList) - 1; i >= 0; i--)
	{
		AICivilianPathGenLine *line = s_acgProcess.lineList[i];
		S32 x;
		bool bLineInBounds = false;

		for( x = 0; x < eaSize(&line->nodes); x++)
		{
			Vec3 nodePos;
			const AICivilianPathGenNode *node = line->nodes[x];

			acgGetPathGenNodePosition(node, nodePos);

			if (acgIsPointInABox(eaBoxList, nodePos))
			{
				bLineInBounds = true;
				break;
			}
		}

		if (!bLineInBounds)
		{
			// clear the node's line pointers
			// and  delete the line
			eaClearEx(&line->nodes, acgClearPathGenNodeLinePointer);
			acgDestroyPathGenLine(line);
			eaRemoveFast(&s_acgProcess.lineList, i);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgCullLinesToPlayableBounds()
{
	acgBoundingBox **eaBoxList = NULL;
	S32 i;

	
	if (!eaSize(&g_civSharedState.eaPlayableVolumes))
	{
		return;
	}

	for (i = 0; i < eaSize(&g_civSharedState.eaPlayableVolumes); i++)
	{
		acgBoundingBox *box = malloc(sizeof(acgBoundingBox));
		const WorldVolumeEntry *ent = g_civSharedState.eaPlayableVolumes[i];

		mat3ToQuat(ent->base_entry.bounds.world_matrix, box->qInvRot);
		quatInverse(box->qInvRot, box->qInvRot);

		copyVec3(ent->base_entry.bounds.world_matrix[3], box->vPos);

		copyVec3(ent->base_entry.shared_bounds->local_min, box->vMin);
		copyVec3(ent->base_entry.shared_bounds->local_max, box->vMax);

		eaPush(&eaBoxList, box);
	}

	acgCullLinesToBoundingBoxList(eaBoxList);

	eaDestroyEx(&eaBoxList, NULL);
}


// ------------------------------------------------------------------------------------------------------------------
static void acgPostCGS_LINES_CullLinesToPartitionGrids()
{
	acgBoundingBox **eaBoxList = NULL;
	acgBoundingBox box;
	
	unitQuat(box.qInvRot);
	zeroVec3(box.vPos);

	copyVec3(s_acg_d_culling.boxMin, box.vMin);
	copyVec3(s_acg_d_culling.boxMax, box.vMax);
	
	eaPush(&eaBoxList, &box);

	acgCullLinesToBoundingBoxList(eaBoxList);

	eaDestroy(&eaBoxList);

}

// ------------------------------------------------------------------------------------------------------------------
static void acgDebugSendEdges(Entity *debugger, int drawDeleted)
{
	if(debugger)
	{
		S32 i;
		bool bOutOfACG = false;

		if (s_acgProcess.state == CGS_DONE || s_acgProcess.state == CGS_NONE)
		{
			bOutOfACG = true;

			MP_CREATE(AICivilianPathGenNodeBlock, 10);
			MP_CREATE(AICivilianPathGenNode, 1000);

			acgLoadGridNodes();
		}

		if (!drawDeleted)
		{
			for(i = 0; i < eaSize(&s_acgProcess.edgeStartList); i++)
			{
				Vec3 pos;
				AICivilianPathGenNode *node = s_acgProcess.edgeStartList[i];
				acgGetPathGenNodePosition(node, pos);
				if(acg_d_pos)
				{
					if (distance3SquaredXZ(pos, s_acgDebugPos)<SQR(400))
						wlAddClientPoint(debugger, pos, colors[node->type]);
				}
				else
				{
					wlAddClientPoint(debugger, pos, colors[node->type]);
				}
			}
		}
		else
		{
			for(i = 0; i < eaSize(&s_acgProcess.deletedNodes); i++)
			{
				ACGDelNode *delnode = s_acgProcess.deletedNodes[i];

				int color;
				switch(delnode->delState)
				{
				case CGS_EDGE:
					color = 0xFFFF0000;
					break;
				case CGS_EDGE2:
					color = 0xFF00FF00;
					break;
				default:
				case CGS_EDGE3:
					if (delnode->delSubState == 0)
					{
						color = 0xFF0000FF;
					}
					else
					{
						color = 0xFFFF00FF;
					}
					break;
				}
				if (delnode->type == EAICivilianLegType_CAR)
				{
					color |= 0x00808080;
				}

				if(acg_d_pos)
				{
					if (distance3SquaredXZ(delnode->pos, s_acgDebugPos)<SQR(400))
						wlAddClientPoint(debugger, delnode->pos, color);
				}
				else
				{
					wlAddClientPoint(debugger, delnode->pos, color);
				}
			}

		}



		if (bOutOfACG)
		{
			acgProcessShutdown();
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgDebugSendLines(Entity *debugger)
{
	if(debugger)
	{
		bool bOutOfACG = false;
		S32 i;

		if (s_acgProcess.state == CGS_DONE || s_acgProcess.state == CGS_NONE)
		{
			bOutOfACG = true;
			MP_CREATE(AICivilianPathGenNodeBlock, 10);
			MP_CREATE(AICivilianPathGenNode, 1000);
			MP_CREATE(AICivilianPathGenLine, 10);
			acgLoadGridNodes();
			acgLoadLines();
		}

		for(i=0; i<eaSize(&s_acgProcess.lineList); i++)
		{
			AICivilianPathGenLine *line = s_acgProcess.lineList[i];
			Vec3 start, end, dir;
			F32 len;

			len = acgLine_GetBestFitLine(line->nodes, NULL, start, end, dir);
			//acgGetPathGenNodePosition(eaHead(&line->nodes), start);
			//acgGetPathGenNodePosition(eaTail(&line->nodes), end);
			// len = distance3(start, end)/10;

			wlAddClientLine(debugger, start, end, acgGetTypeColor(eaHead(&line->nodes)->type, 0));
			
			centerVec3(start, end, start);
			scaleAddVec3(line->perp, len, start, end);
			
			// Send perp line
			wlAddClientLine(debugger, start, end, acgGetTypeColor(eaHead(&line->nodes)->type, 0));
		}

		if (bOutOfACG)
		{
			acgProcessShutdown();
		}

	}
}


// ------------------------------------------------------------------------------------------------------------------
static void acgProcessSetState(CivGenState newState)
{
	const char *str = NULL;
	devassert(newState >= CGS_NONE && newState <= CGS_DONE);

	if (newState == s_acgProcess.state)
		return;
#if defined(ACG_DEBUGPRINT)
	{
		F32 seconds;
		static S64 lastStateChangeTime = 0;
		S64 curTime;
		static CivGenState oldState = -1;
		static const char *s_stateStrings [] = {
			"NONE",		/*CGS_NONE*/ "GRID",	/*CGS_GRID*/
			"EDGE #1",	/*CGS_EDGE*/ "EDGE #2", /*CGS_EDGE2*/ "EDGE #3",	/*CGS_EDGE3*/
			"LINE #1",	/*CGS_LINE*/ "LINE #2",	/*CGS_LINE2*/ "LINE #3",	/*CGS_LINE3*/
			"POSTLINE", /*CGS_POSTLINE*/
			"PAIR #1",	/*CGS_PAIR*/ "PAIR #2",	/*CGS_PAIR2*/
			"LANE #3",	/*CGS_LANE*/
			"LEG #1",	/*CGS_LEG*/ "LEG #2",	/*CGS_LEG2*/ "LEG #3",	/*CGS_LEG3*/ "LEG #4",	/*CGS_LEG4*/
			"INT #0",	/*CGS_INT0*/ "INT #1",	/*CGS_INT1*/ "INT #2",	/*CGS_INT2*/ "INT #3",	/*CGS_INT3*/ "INT #4", /*CGS_INT4*/
			"SPLIT",	/*CGS_SPLIT*/
			"MIN",		/*CGS_MIN*/ "COPLANAR",	/*CGS_COPLANAR*/ "CROSSWALK", /*CGS_CROSSWALK*/ 
			"PATHPOINTS", /*CGS_PATHPOINTS*/ "CURVEFITTING", /*CGS_CURVEFITTING*/
			"FIN",		/*CGS_FIN*/ "FILE",		/*CGS_FILE*/ "DONE",		/*CGS_DONE*/ };
		STATIC_ASSERT(ARRAY_SIZE(s_stateStrings) == (1 + CGS_DONE - CGS_NONE));

		curTime = timerCpuTicks64();
		if (oldState != s_acgProcess.state)
		{
			seconds = 0.0f;
		}
		else
		{
			seconds = (float)((double)(curTime - lastStateChangeTime) / (double)timerCpuSpeed64());
		}
		lastStateChangeTime = curTime;
		oldState = newState;

		//if (g_bAICivVerbose)
		printf("\n(dt %3.2f): NextState: %s", seconds, s_stateStrings[oldState]);

		if (s_acgSkipTable[newState] == true)
		{
			printf("\nSkipping State: %s", s_stateStrings[newState]);
			acgProcessSetState(newState+1);
			return;
		}

	}
#endif

	
	{
		CivGenState *pstate = (CivGenState*)&s_acgProcess.state;
		*pstate = newState;
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void acgGenerate(int iPartitionIdx)
{
	Entity *debugger = NULL;
	int uptodate = 0;

	if(aiCivDebugRef)
	{
		debugger = entFromEntityRef(iPartitionIdx, aiCivDebugRef);
	}

	acgInitData();
	aiCivilian_CreateAndInitSharedData();

	uptodate = acgCheckMapInfo();
	if(!s_ignoreUptodate && uptodate)
	{
		acgProcessSetState(CGS_DONE);

		binNotifyTouchedOutputFile(aiCivilianGetSuffixedFileName(1, 1, "map_info"));
		binNotifyTouchedOutputFile(aiCivilianGetSuffixedFileName(1, 1, CIV_FILE_SUFFIX));
	}

	do
	{
		mpCompactPools();

		switch(s_acgProcess.state)
		{
			xcase CGS_NONE: {
				if(acgProcessCheckStart(iPartitionIdx, debugger))
				{
					if (s_acg_regenForcedLegsOnly)
					{
						acgProcessSetState(CGS_LEG2);
						break;
					}

					printf("\nGenerating civilian paths...");
					
					//acgGetDebugCullingBounds();

					acgProcessSetState(CGS_GRID);
				}
			}
			xcase CGS_GRID: {
				
				// check if we should load the grid nodes
				if(s_acg_forceLoadPartial == 1 ||
					(s_acg_forceRegenPartial == 0 && g_civSharedState.partialLoad >= 1 && uptodate))
				{	// If s_ignoreUptodate is false, it means the map changed and the saved file is invalid
					
					printf("Loading grid nodes...");
					
					if(acgLoadGridNodes())
					{
						printf("done. (%d nodes).", eaSize(&s_acgProcess.edgeStartList));

						if(acg_d_edges)
							acgDebugSendEdges(debugger, false);
						
						if(g_civSharedState.partialLoad >= 2)
						{
							printf("Loading lines...");
						
							if(acgLoadLines())
							{
								printf(" done. (%d lines)", eaSize(&s_acgProcess.lineList));
								acgProcessSetState(CGS_POSTLINE);
								break;
							}

							printf("failed.");
						}
						acgProcessSetState(CGS_LINE);
						break;
					}
					else
					{
						printf("failed.");
					}	
				}

				printf("\n\tProcessing grid blocks...\n");
				
				if (s_acg_forceRegenPartial)
				{
					s_acg_forceRegenPartial = 0;
					s_acg_forceLoadPartial = 1;
				}

				acgProcessGridNodes(iPartitionIdx, debugger);
				
				acgProcessSetState(CGS_EDGE);

			}
			xcase CGS_EDGE: {
				if(acgProcessEdgesCleanAreas(debugger))
				{
					acgProcessSetState(CGS_EDGE2);
				}
			}
			xcase CGS_EDGE2: {
				acgProcessEdgesCleanInvalid(debugger);
				
				acgProcessSetState(CGS_EDGE3);
			}
			xcase CGS_EDGE3: {
				acgProcessEdgesCleanExtraneous(debugger);

				if(g_civSharedState.partialLoad >= 1)
				{
					printf("\nSaving grid nodes...");
					// if we have any cached file contents, free them
					acgFreeACGNodeFileContents(&s_acgFileNodeContents);
					acgSaveGridNodes();
					printf(" done!");
				}

				acgProcessSetState(CGS_LINE);

				if(acg_d_edges)
				{
					acgDebugSendEdges(debugger, false);
				}
			}
			xcase CGS_LINE: {
				acgProcessLines(debugger);
				
				acgProcessSetState(CGS_LINE2);
			}
			xcase CGS_LINE2: {
				acgProcessLinesMerge(debugger, false);
				
				acgProcessSetState(CGS_LINE3);
			}
			xcase CGS_LINE3: {
				
				acgProcessLinesMinimizeError(debugger);

				
				if(g_civSharedState.partialLoad >= 2)
				{
					// if we have any cached line contents, free them
					acgFreeACGLineFileContents(&s_acgFileLineContents);
					acgSaveLines();
				}

				acgProcessSetState(CGS_POSTLINE);
			}
			
			xcase CGS_POSTLINE: {

				acgCullLinesToPlayableBounds();

				// debugging, cull the lines to the partitions
				if (s_acg_d_culling.do_culling)
				{
					acgPostCGS_LINES_CullLinesToPartitionGrids();
				}

				if(acg_d_lines)
				{
					acgDebugSendLines(debugger);
				}

				acgProcessSetState(CGS_PAIR);
			}

			xcase CGS_PAIR: {
				acgProcessLinePerp(debugger);
				acgProcessSetState(CGS_PAIR2);
			}
			xcase CGS_PAIR2: {
				
				if(acgCreatePairs(debugger))
				{
					acgProcessSetState(CGS_LANE);
				}
			}
			xcase CGS_LANE: {
				
				if(acgLeg_CreateLegsFromPairs(iPartitionIdx, debugger))
				{
					acgLeg_MergeDupes(debugger);
					acgLeg_MergeOverlapping(debugger);
					acgLeg_MergeNearby2(debugger);
					acgLeg_RealignToNeighbors(debugger);
					acgLeg_ExtendToNearby(debugger);
					acgLeg_ExtendToNearby(debugger);
					acgLeg_RealignToNeighbors(debugger);
					acgLeg_MergeNearby2(debugger);
					acgProcessSetState(CGS_LEG);
				}
			}

			xcase CGS_LEG: {
				
				acgRemoveSidewalksFromMedians();

				acgLeg_RoadFixup(iPartitionIdx, debugger);

				acgLeg_SkewedRoadFixup();

				acgProcessSetState(CGS_LEG2);
			}
			
			xcase CGS_LEG2: {
				// this is a band-aid fix to remove legs that are insanely wide
				
				// acgClipIntersectingLegs();

				acgClipLegsToForcedLegVolumes();

				// create the forced leg from the volumes 
				acgCreateForcedVolumeLegs(iPartitionIdx);

				acgClipForcedLegsToDisableVolumes();

				acgProcessSetState(CGS_INT0);
			}
			xcase CGS_INT0: {
				acgConnection_ProcessImplicitConnection(debugger);
				acgProcessSetState(CGS_INT1);
			}
			xcase CGS_INT1: {
				acgConnection_ProcessIntersectionsByMaterial(iPartitionIdx, debugger);
				acgProcessSetState(CGS_INT2);
			}
			xcase CGS_INT2: {
				acgProcessIntersections(iPartitionIdx);
				acgProcessSetState(CGS_INT3);
			}
			xcase CGS_INT3: {
				acgValidateAllLegConnections(true);
				
				acgReplaceSingleIntersections();

				if (! s_bSkipDeadEndOneWayStreets)
					acgDestroyDeadEndOneWayStreets();

				acgCorrectOneWayStreets();

				acgLeg_FixupCarPaths(iPartitionIdx);
				
				acgIntersectionClipDanglingMidIntersections();

				// acgClipHardTurns();
				
				acgProcessSetState(CGS_INT4);
			}

			xcase CGS_INT4: {
				acgCalculateIntersectionBoundingVolumes(debugger);
				acgClassifyIntersections(debugger);
				acgProcessSetState(CGS_SPLIT);
			}

			xcase CGS_SPLIT: {

				if (s_acgSkip_LegSplit == 1)
				{
					acgProcessSetState(CGS_MIN);
					continue;
				}

				if (acgSplitLegsOnObjects(iPartitionIdx, debugger))
				{
					acgProcessSetState(CGS_MIN);

				}
			}

			xcase CGS_MIN: {

				if (s_acgSkip_RemoveLowCircutCheck == 1)
				{
					acgProcessSetState(CGS_COPLANAR);
					continue;
				}

				if(acgProcessLegsRemoveLowDistance(debugger))
				{
					acgProcessSetState(CGS_COPLANAR);
				}
			}
			xcase CGS_COPLANAR: {
				if (s_acgSkip_GroundCoplanarCheck == 1)
				{
					acgProcessSetState(CGS_CROSSWALK);
					continue;

				}

				if (acgDetermineGroundCoPlanarLegs(iPartitionIdx, debugger))
				{
					acgProcessSetState(CGS_CROSSWALK);
				}
			}

			xcase CGS_CROSSWALK: {
				
				acgCrosswalk_Fixup();
				acgProcessSetState(CGS_PATHPOINTS);
				
			}

			xcase CGS_PATHPOINTS: {
				
				acgPathPoints(iPartitionIdx);
				acgProcessSetState(CGS_CURVEFITTING);
			}

			xcase CGS_CURVEFITTING: {

				acgCurveFit(iPartitionIdx);
				acgProcessSetState(CGS_FIN);
			}

			xcase CGS_FIN: {
				if(acgProcessFinal(debugger))
				{
					acgProcessSetState(CGS_FILE);
				}
			}
			xcase CGS_FILE:{
				acgWriteFile(debugger);
				acgProcessSetState(CGS_DONE);
			}
		}
	} while(s_acgProcess.state != CGS_DONE && s_acgProcess.state != CGS_NONE);

	//loadend_printf("done!");
}


// ------------------------------------------------------------------------------------------------------------------
// Randomly selects a portion of a string separated by '|'s
static void RandomSplit(char **pestr, const char *pText)
{
	const char *ptr = pText;
	int count = 0;
	int index;

	// Count the vertical bars in the text
	while (ptr = strchr(ptr, '|'))
	{
		++count;
		++ptr;
	}

	// Pick a random entry
	index = randomIntRange(0,count);

	// Copy the appropriate substring into the estring.
	count = 0;
	ptr = strchr(pText, '|');
	while (count < index)
	{
		pText = ptr+1;
		ptr = strchr(pText, '|');
		++count;
	}
	if (ptr)
		estrConcat(pestr, pText, (ptr - pText));
	else
		estrConcat(pestr, pText, (int)strlen(pText));
}


// ------------------------------------------------------------------------------------------------------------------
typedef void* (*eaAccessor)(void *);
int eaFindAccessor(void ***earray, void *ptr, eaAccessor acc)
{
	int i;
	for(i=eaSize(earray)-1; i>=0; i--)
		if(acc((*earray)[i])==ptr)
			return i;

	return -1;
}


// ------------------------------------------------------------------------------------------------------------------
static bool acgForcedLegInitializeQuad(int iPartitionIdx, AICivilianPathLeg *leg, const WorldCellEntryBounds *bounds, const WorldVolumeElement *volumeElement)
{
#define F32_MID(a,b)	((a)*0.5f+(b)*0.5f)
	F32 xLen, zLen;
	//F32 fWidth, fLength;
	// get the longest length of the bounding box (XZ distance)

	
	xLen = volumeElement->local_max[0] - volumeElement->local_min[0];
	zLen = volumeElement->local_max[2] - volumeElement->local_min[2];

	// along the Z axis
	leg->width = xLen;
	leg->len = zLen;
	
	leg->start[0] = F32_MID(volumeElement->local_min[0], volumeElement->local_max[0]);
	//leg->start[1] = F32_MID(volumeElement->local_min[1], volumeElement->local_max[1]);
	leg->start[1] = volumeElement->local_max[1];
	leg->start[2] = volumeElement->local_min[2];

	leg->end[0] = F32_MID(volumeElement->local_min[0], volumeElement->local_max[0]);
	leg->end[1] = volumeElement->local_max[1];
	leg->end[2] = volumeElement->local_max[2];

	// transform the leg position 
	{
		Vec3 tmp;
		mulVecMat4(leg->start, bounds->world_matrix, tmp);
		copyVec3(tmp, leg->start); // copy the result back into tmp
		mulVecMat4(leg->end, bounds->world_matrix, tmp);
		copyVec3(tmp, leg->end); // copy the result back into tmp
	}

	acgSnapPosToGround(iPartitionIdx, leg->start, 0.0f, -100.0f);
	acgSnapPosToGround(iPartitionIdx, leg->end, 0.0f, -100.0f);

	leg->len = calcLegDir(leg, leg->dir, leg->perp);
	
	// validate the leg (should only need length, width validation)
	if (leg->len == 0 || leg->width == 0)
	{
		printf("\n\tForced leg could not be created at location: (%.1f, %.1f, %.1f)", leg->start[0], leg->start[1], leg->start[2]);
		return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgForcedRoadGetMedianWidth(int iPartitionIdx, AICivilianPathLeg *leg)
{
#define LEN_START_DIST (0.5f)
#define LEN_STEP_DIST (1.0f)
#define WIDTH_START_DIST	(4.25f)
#define WIDTH_STEP_DIST		(0.1f)

#define MEDIAN_PRE	0
#define MEDIAN_IN	1
#define MEDIAN_END	2

	F32 fCurLen, fCurWidth;
	F32 fMinMedian, fMaxMedian;
	F32 fLegHalfWidth = leg->width * 0.5f;
	U32 medianState = MEDIAN_PRE;

	

	fMinMedian = FLT_MAX;
	fMaxMedian = -FLT_MAX;
	// walk the leg and cast rays looking for non-road type

	fCurLen = LEN_START_DIST;
	fCurLen = leg->len * 0.5f;

	while(fCurLen <= leg->len)
	{
		medianState = MEDIAN_PRE;

		fCurWidth = -(fLegHalfWidth) + WIDTH_START_DIST;
		while(fCurWidth <= fLegHalfWidth) 
		{
			WorldCollCollideResults results = {0};
			Vec3 vCastPos;
			EAICivilianLegType type;

			scaleAddVec3(leg->dir, fCurLen, leg->start, vCastPos);
			scaleAddVec3(leg->perp, fCurWidth, vCastPos, vCastPos);

			acgUtil_CastVerticalRay(iPartitionIdx, &results, vCastPos, 5.0f, 10.0f);
			type = aiCivGenClassifyResults(&results);
			if (type != EAICivilianLegType_CAR && acgIsTypeIntersection(&results))
				type = EAICivilianLegType_CAR;

			if (type != EAICivilianLegType_CAR)
			{
				if (medianState == MEDIAN_PRE)
				{
					if (fCurWidth < fMinMedian)
						fMinMedian = fCurWidth;
					medianState = MEDIAN_IN;
				}
			}
			else
			{
				if (medianState == MEDIAN_IN)
				{
					if (fCurWidth > fMaxMedian)
						fMaxMedian = fCurWidth;
					medianState = MEDIAN_END;
					break;
				}
			}
			
			fCurWidth += WIDTH_STEP_DIST;
		}

		
		fCurLen += LEN_STEP_DIST;
		break;
	}

	// we probably want to at least center the leg around the median.
	// if the leg was placed especially poorly, the user may see some bad results.

	// find the offset from the center
	{
		F32 fMedianMid, fMedianWidth;
				
		fMedianWidth = fMaxMedian - fMinMedian;
		fMedianMid = fMinMedian + fMedianWidth * 0.5f;
		
		// the median Mid should be close to 0 if it is centered
		if (ABS(fMedianMid) > 0.1f)
		{	// the median is offset by enough, let's move it
			scaleAddVec3(leg->perp, fMedianMid, leg->start, leg->start);
			scaleAddVec3(leg->perp, fMedianMid, leg->end, leg->end);
		
		#if defined(ACG_DEBUGPRINT)
			printf("\n\tForced Road median required repositioning of road by %0.2f Feet.", fMedianMid);
		#endif
		}
		
		leg->median_width = fMedianWidth;
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgCreateForcedVolumeLegs(int iPartitionIdx)
{
	S32 x; 
	
	for (x = 0; x < eaSize(&g_civSharedState.eaCivVolumeForcedLegs); x++)
	{
		WorldVolumeEntry *volume = g_civSharedState.eaCivVolumeForcedLegs[x];
		AICivilianPathLeg *leg;
		WorldCivilianVolumeProperties *civVolume = volume->server_volume.civilian_volume_properties;
		if (! volume->server_volume.civilian_volume_properties)
			continue;

		// if we're doing a local regen, test to see if the volume intersects with the local regen bbox
		if (s_acg_d_culling.do_culling)
		{
			const WorldVolumeElement *volumeElement = eaGet(&volume->elements, 0);
			bool bRet;
			devassert(volumeElement);

			bRet = orientBoxBoxCollision(	volumeElement->local_min, 
											volumeElement->local_max, 
											volume->base_entry.bounds.world_matrix,
											s_acg_d_culling.boxMin, 
											s_acg_d_culling.boxMax, unitmat);
			if (!bRet)
			{
				continue;
			}
		}
		
		leg = acgPathLeg_Alloc();

		devassert(civVolume->forced_sidewalk || civVolume->forced_road || 
				  civVolume->forced_crosswalk );

		// note: if both are set (forced_sidewalk && forced_road), piss off, I'm giving sidewalks priority
		leg->type = (civVolume->forced_sidewalk || civVolume->forced_crosswalk) ? EAICivilianLegType_PERSON : EAICivilianLegType_CAR;
		leg->bIsCrosswalk = civVolume->forced_crosswalk;
		leg->bForcedLegAsIs = civVolume->forced_as_is || civVolume->forced_crosswalk;
		leg->bIsForcedLeg = true;

		devassert(eaGet(&volume->elements,0));
		// 
		if( !acgForcedLegInitializeQuad(iPartitionIdx, leg, &volume->base_entry.bounds, volume->elements[0]))
		{
			acgPathLeg_Free(leg);
			continue;
		}

		if (leg->type == EAICivilianLegType_CAR && civVolume->forced_road_has_median)
		{
			// this road has a median, we're going to get the median width here.
			acgForcedRoadGetMedianWidth(iPartitionIdx, leg);
		}

		if (civVolume->pcLegDefinition)
		{
			leg->pchLegTag = allocFindString(civVolume->pcLegDefinition);
		}

		eaPush(&s_acgProcess.legList, leg);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgRemoveSidewalksFromMedians()
{
	S32 x;

	for (x = 0; x < eaSize(&s_acgProcess.legList); x++)	
	{
		Vec3 legMin, legMax;
		Mat4 mtxLeg;
		AICivilianPathLeg *leg = s_acgProcess.legList[x];
		S32 i;

		if (leg->type != EAICivilianLegType_CAR || leg->median_width <= 0.5f)
			continue;

		acgLegToOBB(leg, legMin, legMax, mtxLeg);

		// convert the bounding to use the median width
		{
			F32 hwidth = leg->median_width * 0.5f;
			legMin[0] = -hwidth;
			legMax[0] = hwidth;
		}

		for (i = 0; i < eaSize(&s_acgProcess.legList); i++)	
		{
			Vec3 other_legMin, other_legMax;
			Mat4 other_mtxLeg;

			AICivilianPathLeg *other_leg = s_acgProcess.legList[i];
			if (other_leg->type != EAICivilianLegType_PERSON || other_leg->deleted)
				continue;
			
			acgLegToOBB(other_leg, other_legMin, other_legMax, other_mtxLeg);
			
			if (orientBoxBoxCollision(legMin, legMax, mtxLeg, other_legMin, other_legMax, other_mtxLeg))
			{
				other_leg->deleted = s_acgProcess.state;
				other_leg->deleteReason = "Intersecting median";
			}
		}


	}

	acgProcess_RemoveDeletedLegs();
}

// ------------------------------------------------------------------------------------------------------------------
static void acgDrawLegOBB(Entity *debugger, const Vec3 vMin, const Vec3 vMax, const Mat4 mtx)
{
	Vec3 p1, p2, p3, p4;
	Vec3 vTmp;
	
	// p1
	mulVecMat4(vMin, mtx, p1);
	// p2
	setVec3(vTmp, vMin[0], vMin[1], vMax[2]);
	mulVecMat4(vTmp, mtx, p2);

	// p3
	mulVecMat4(vMax, mtx, p3);
	
	// p4
	setVec3(vTmp, vMax[0], vMax[1], vMin[2]);
	mulVecMat4(vTmp, mtx, p4);

	p1[1] += 2.0f;
	p2[1] += 2.0f;
	p3[1] += 2.0f;
	p4[1] += 2.0f;
	
	wlAddClientQuad(debugger, p1, p2, p3, p4, 0xFFFF0000);
	
}


// ------------------------------------------------------------------------------------------------------------------

#define MIN_LEG_LENGTH_SQ	(SQR(7))
#define LEG_PUSHBACK_DIST	(3)

typedef enum ELegClipState
{
	ELegClipState_NOLEG = 0,
	ELegClipState_LEG1,
	ELegClipState_NOLEG2,
	ELegClipState_LEG2,
} ELegClipState;

static bool acgClipLegToVolume(const WorldCellEntryBounds *bounds, const WorldVolumeElement *volumeElement, AICivilianPathLeg *leg, 
							   AICivilianPathLeg **new_leg1, AICivilianPathLeg **new_leg2)
{
	F32 fCurLen, fHWidth;
	Vec3 localLegDir, localLegPerp;
	Vec3 localLegStart, localLegEnd, vStartLine, vEndLine;
	
	ELegClipState state = ELegClipState_NOLEG;
	Vec3 leg1_start, leg1_end;
	Vec3 leg2_start, leg2_end;
	Vec3 vIsect;
	S32 numLegs = 0;
	Vec3 vVolumeLocalMin, vVolumeLocalMax;

	*new_leg1 = NULL;
	*new_leg2 = NULL;
 
	// transform the leg's quad into the bounding volume's space

	subVec3(leg->start, bounds->world_matrix[3], vEndLine);
	mulVecMat3Transpose(vEndLine, bounds->world_matrix, localLegStart);

	subVec3(leg->end, bounds->world_matrix[3], vEndLine);
	mulVecMat3Transpose(vEndLine, bounds->world_matrix, localLegEnd);
	
	// recalculate the leg direction and perp
	subVec3(localLegEnd, localLegStart, localLegDir);
	normalVec3(localLegDir);
	crossVec3Up(localLegDir, localLegPerp);
	normalVec3(localLegPerp);
	
	fHWidth = leg->width * 0.5f;

	copyVec3(volumeElement->local_min, vVolumeLocalMin);
	copyVec3(volumeElement->local_max, vVolumeLocalMax);
	vVolumeLocalMin[1] -= 10.f;
	vVolumeLocalMax[1] += 10.f;
	

	// now walk down the leg and check intersections with the volume
	fCurLen = 0.0f;
	while(fCurLen <= leg->len)
	{
		scaleAddVec3(localLegPerp, fHWidth, localLegStart, vStartLine);
		scaleAddVec3(localLegPerp, -fHWidth, localLegStart, vEndLine);
		scaleAddVec3(localLegDir, fCurLen, vStartLine, vStartLine);
		scaleAddVec3(localLegDir, fCurLen, vEndLine, vEndLine);

		if (lineSegBoxCollision(vStartLine, vEndLine, vVolumeLocalMin, vVolumeLocalMax, vIsect))
		{
			switch(state)
			{
				xcase ELegClipState_NOLEG:
					// do nothing, waiting for the leg to start
					
				xcase ELegClipState_LEG1:
					// the leg is ending at our previous position
					state = ELegClipState_NOLEG2;
					scaleAddVec3(localLegDir, fCurLen - 1, localLegStart, leg1_end);

				xcase ELegClipState_NOLEG2:
					// do nothing

				xcase ELegClipState_LEG2:
					// I'm not sure we should ever get to this state.
					// if we do, then something is probably wrong
					devassert(0);
			}

		}
		else
		{
			switch(state)
			{
				xcase ELegClipState_NOLEG:
					// starting the leg now
					state = ELegClipState_LEG1;
					scaleAddVec3(localLegDir, fCurLen, localLegStart, leg1_start);
					
				xcase ELegClipState_LEG1:
					// do nothing.	
				xcase ELegClipState_NOLEG2:
					// starting the second leg now
					state = ELegClipState_LEG2;
					scaleAddVec3(localLegDir, fCurLen, localLegStart, leg2_start);
					
				//xcase ELegClipState_LEG2:
					// do nothing
			}
		}
		
		fCurLen += 1.0f;
	}

	switch(state)
	{
		xcase ELegClipState_NOLEG:
			// this leg was completely obstructed, we're going to delete it
			numLegs = 0;

		xcase ELegClipState_LEG1:
			// the first leg was never finished, meaning we reached the end.
			// set the end point
			copyVec3(localLegEnd, leg1_end);
			numLegs = 1;

		xcase ELegClipState_NOLEG2:
			// the second leg was never started, we only have one leg
			numLegs = 1;
		xcase ELegClipState_LEG2:
			// 
			copyVec3(localLegEnd, leg2_end);
			numLegs = 2;
	}

	if (numLegs)
	{
		numLegs--;
		
		if (distance3Squared(leg1_start, leg1_end) >= MIN_LEG_LENGTH_SQ)
		{
			Vec3 vNewLegStart, vNewLegEnd;
			
			// reverse transform the leg1_start/leg1_end
			mulVecMat4(leg1_start, bounds->world_matrix, vNewLegStart);
			mulVecMat4(leg1_end, bounds->world_matrix, vNewLegEnd);

			if ( nearSameVec3(leg->start, vNewLegStart) && nearSameVec3(leg->end, vNewLegEnd) )
				return false; // leg was untouched, somehow the oriented v orient box test failed....

			{
				AICivilianPathLeg *newLeg = acgPathLeg_Alloc();
				newLeg->type = leg->type;
				newLeg->median_width = leg->median_width;
				newLeg->width = leg->width;
				copyVec3(leg->dir, newLeg->dir);
				copyVec3(leg->perp, newLeg->perp);

				// push the end point back a bit
				copyVec3(vNewLegStart, newLeg->start);
				copyVec3(vNewLegEnd, newLeg->end);
				
				if (nearSameVec3(leg->start, vNewLegStart))
				{
					scaleAddVec3(leg->dir, -LEG_PUSHBACK_DIST, newLeg->end, newLeg->end);
				}
				else
				{
					scaleAddVec3(leg->dir, LEG_PUSHBACK_DIST, newLeg->start, newLeg->start);
				}
				

				newLeg->len = distance3(newLeg->start, newLeg->end);

				*new_leg1 = newLeg;
			}
		}
	}
	
	if (numLegs)
	{
		if (distance3Squared(leg2_start, leg2_end) >= MIN_LEG_LENGTH_SQ)
		{
			AICivilianPathLeg *newLeg = acgPathLeg_Alloc();

			newLeg->type = leg->type;
			newLeg->median_width = leg->median_width;
			newLeg->width = leg->width;
			copyVec3(leg->dir, newLeg->dir);
			copyVec3(leg->perp, newLeg->perp);

			// push the start point back a bit
			scaleAddVec3(localLegDir, LEG_PUSHBACK_DIST, leg2_start, leg2_start);

			mulVecMat4(leg2_start, bounds->world_matrix, newLeg->start);
			mulVecMat4(leg2_end, bounds->world_matrix, newLeg->end);
			newLeg->len = distance3(leg2_start, leg2_end);

			*new_leg2 = newLeg;
		}
	}
	
	return true;
}

typedef int (*fpShouldClipLeg)(const WorldCivilianVolumeProperties *pCivVolume, const AICivilianPathLeg *leg);

// ------------------------------------------------------------------------------------------------------------------
static void acgClipLegsToVolumeList(WorldVolumeEntry **volumeList, fpShouldClipLeg fp, bool bForcedLegsOnly)
{
	if (eaSize(&volumeList) == 0 || !fp)
		return;

	FOR_EACH_IN_EARRAY(s_acgProcess.legList, AICivilianPathLeg, leg)
	{
		// create a bounding box for this leg.
		Vec3 legMin, legMax;
		Mat4 mtxLeg;
		
		if (bForcedLegsOnly && !leg->bIsForcedLeg)
			continue;	// only clipping the forced legs

		acgLegToOBB(leg, legMin, legMax, mtxLeg);

		// test this leg vs all the volumes
		FOR_EACH_IN_EARRAY(volumeList, WorldVolumeEntry, volume)
		{
			const WorldCellEntryBounds *bounds;
			const WorldVolumeElement *volumeElement;
			const WorldCivilianVolumeProperties *pCivVolume = volume->server_volume.civilian_volume_properties;

			if (! pCivVolume)
				continue;
			
			bounds = &volume->base_entry.bounds;
			volumeElement = eaGet(&volume->elements, 0);
			devassert(volumeElement);
		
			if (fp(pCivVolume, leg))
			{
				if (orientBoxBoxCollision(	volumeElement->local_min, 
											volumeElement->local_max, 
											bounds->world_matrix,
											legMin, legMax, mtxLeg))
				{
					AICivilianPathLeg *newLeg1, *newLeg2;
					// this leg collides with this box. 
					// clip the leg to the box.
					if (acgClipLegToVolume(bounds, volumeElement, leg, &newLeg1, &newLeg2))
					{
						leg->deleted = s_acgProcess.state;
						leg->deleteReason = "Clipped to Forced Leg";

						if (newLeg1)
						{
							newLeg1->bIsForcedLeg = leg->bIsForcedLeg;
							eaPush(&s_acgProcess.legList, newLeg1);
						}
						if (newLeg2)
						{
							newLeg2->bIsForcedLeg = leg->bIsForcedLeg;
							eaPush(&s_acgProcess.legList, newLeg2);
						}
						break;
					}
				}
			}
		}
		FOR_EACH_END

	}
	FOR_EACH_END

	acgProcess_RemoveDeletedLegs();
}

// ------------------------------------------------------------------------------------------------------------------
int ShouldClipLegToForcedLeg(const WorldCivilianVolumeProperties *pCivVolume, const AICivilianPathLeg *leg)
{
	return ((pCivVolume->forced_sidewalk && (leg->type == EAICivilianLegType_PERSON)) ||
			(pCivVolume->forced_road && (leg->type == EAICivilianLegType_CAR)));
}

// ------------------------------------------------------------------------------------------------------------------
static void acgClipLegsToForcedLegVolumes()
{
	
	acgClipLegsToVolumeList(g_civSharedState.eaCivVolumeForcedLegs, ShouldClipLegToForcedLeg, false);
}

// ------------------------------------------------------------------------------------------------------------------
int ShouldClipForcedLegToDisableVolume(const WorldCivilianVolumeProperties *pCivVolume, const AICivilianPathLeg *leg)
{
	if (!pCivVolume->forced_as_is)
		return false;

	return ((pCivVolume->disable_sidewalks && (leg->type == EAICivilianLegType_PERSON)) ||
			(pCivVolume->disable_roads  && (leg->type == EAICivilianLegType_CAR)));
}

// ------------------------------------------------------------------------------------------------------------------
static void acgClipForcedLegsToDisableVolumes()
{
	acgClipLegsToVolumeList(g_civSharedState.eaCivilianVolumes, ShouldClipForcedLegToDisableVolume, true);
}

// ------------------------------------------------------------------------------------------------------------------
bool acgPointInLeg(const Vec3 vPos, const AICivilianPathLeg *pLeg)
{
	F32 fDist;
	Vec3 vStartToPos;

	subVec3(vPos, pLeg->start, vStartToPos);

	fDist = dotVec3(vStartToPos, pLeg->dir);
	if (fDist < 0.001f || fDist > (pLeg->len - 0.001f))
		return false;

	fDist = dotVec3(vStartToPos, pLeg->perp);
	fDist = ABS(fDist);

	fDist = fDist - (pLeg->width * 0.5f);
	return fDist < 0.001f;

}

// ------------------------------------------------------------------------------------------------------------------
static bool acgLineSegVsBox(const Vec3 vPt1, const Vec3 vPt2, const Vec3 vBoxMin, const Vec3 vBoxMax, Mat4 BoxMtx, Vec3 vIsect)
{
	Vec3 vLegLocalPt1, vLegLocalPt2, vTmp;

	subVec3(vPt1, BoxMtx[3], vTmp);
	mulVecMat3Transpose(vTmp, BoxMtx, vLegLocalPt1);
	subVec3(vPt2, BoxMtx[3], vTmp);
	mulVecMat3Transpose(vTmp, BoxMtx, vLegLocalPt2);

	if (! lineSegBoxCollision(vLegLocalPt1, vLegLocalPt2, vBoxMin, vBoxMax, vIsect))
		return false;
	
	mulVecMat3(vIsect, BoxMtx, vTmp);
	addVec3(vTmp, BoxMtx[3], vIsect);
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static F32 acgGetOverlapAreaApproximation(const AICivilianPathLeg *leg, const AICivilianPathLeg *otherLeg)
{
	const F32 *pvAnchorPos = NULL;
	const F32 *pvEndPos = NULL;
	Mat4 mtxOtherLeg;
	Vec3 vOtherLegMin, vOtherLegMax, vIsectPt;
	Vec3 vEndPt, vPerpPt1, vPerpPt2;
	Vec3 vTmpPerp;
		
	if (acgPointInLeg(leg->start, otherLeg))
	{
		pvAnchorPos = leg->start;
		pvEndPos = leg->end;
	}
	else if (acgPointInLeg(leg->end, otherLeg))
	{
		pvAnchorPos = leg->end;
		pvEndPos = leg->start;
	}
	else
	{	// neither end point of leg was in otherLeg
		//	(very rough approximation, btw)
		return 0.0f; 
	}
	

	acgLegToOBB(otherLeg, vOtherLegMin, vOtherLegMax, mtxOtherLeg);
	
	// clip the start/end to the otherLeg
	if (acgLineSegVsBox(pvEndPos, pvAnchorPos, vOtherLegMin, vOtherLegMax, mtxOtherLeg, vIsectPt)) {
		copyVec3(vIsectPt, vEndPt);
	} else {
		copyVec3(pvEndPos, vEndPt);
	}
	
	// clip the left/right to the otherLeg
	scaleAddVec3(leg->perp, (leg->width*0.5f), pvAnchorPos, vTmpPerp);
	if (acgLineSegVsBox(vTmpPerp, pvAnchorPos, vOtherLegMin, vOtherLegMax, mtxOtherLeg, vIsectPt)) {
		copyVec3(vIsectPt, vPerpPt1);
	} else {
		copyVec3(vTmpPerp, vPerpPt1);
	}


	scaleAddVec3(leg->perp, -(leg->width*0.5f), pvAnchorPos, vTmpPerp);
	if (acgLineSegVsBox(vTmpPerp, pvAnchorPos, vOtherLegMin, vOtherLegMax, mtxOtherLeg, vIsectPt)) {
		copyVec3(vIsectPt, vPerpPt2);
	} else {
		copyVec3(vTmpPerp, vPerpPt2);
	}
	
	{
		F32 fWidth, fLen;

		fWidth = distance3(vPerpPt1, vPerpPt2);
		fLen = distance3(pvAnchorPos, vEndPt);

		return fWidth * fLen;
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void acgClipIntersectingLegs()
{
	S32 x;

	for (x = 0; x < eaSize(&s_acgProcess.legList); x++)
	{
		S32 i;
		AICivilianPathLeg *leg = s_acgProcess.legList[x];
		F32 fLegArea = leg->width * leg->len;

		if (leg->type == EAICivilianLegType_CAR || leg->deleted)
			continue;

		for (i = x + 1; i < eaSize(&s_acgProcess.legList); i++)
		{
			AICivilianPathLeg *otherLeg = s_acgProcess.legList[i];
			F32 fOtherLegArea;
			
			if (otherLeg->type == EAICivilianLegType_CAR || otherLeg->deleted)
				continue;

			fOtherLegArea = otherLeg->len * otherLeg->width;
			
			if ((fLegArea + leg->len) > (fOtherLegArea + otherLeg->len))
			{
				F32 fOverlap = acgGetOverlapAreaApproximation(otherLeg, leg);
				if (fOverlap > (fOtherLegArea * .4f))
				{
					otherLeg->deleted = s_acgProcess.state;
				}
			}
			else
			{
				F32 fOverlap = acgGetOverlapAreaApproximation(leg, otherLeg);
				if (fOverlap > (fLegArea * .4f))
				{
					leg->deleted = s_acgProcess.state;
					break;
				}
			}
		}
	}
	

	acgProcess_RemoveDeletedLegs();

}

// ------------------------------------------------------------------------------------------------------------------
static bool acgHeatmapGatherLegs(gslHeatMapCBHandle *pHandle, char **ppErrorString)
{
	AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
	AICivilianRuntimePathInfo *pPathInfo;
	S32 typeIdx = (g_eHeatMapSet==EAICivilianLegType_PERSON) ? 0 : 1;
	S32 i;
	
	if (!pPartition)
		return false;

	pPathInfo = &pPartition->pathInfo;


	// draw all the legs
	for (i = 0; i < eaSize(&pPathInfo->legs[typeIdx]); i++)
	{
		AICivilianPathLeg *pLeg = pPathInfo->legs[typeIdx][i];

		gslHeatMapAddLineEx(pHandle, pLeg->start, pLeg->dir, pLeg->len, 20);

		// draw the leg's next and previous leg connections just to close any gaps
		if (pLeg->next)
		{
			AICivilianPathLeg *nextLeg = pLeg->next;
			F32 *next_leg_pos = (nextLeg->prev == pLeg) ? nextLeg->start : nextLeg->end;

			gslHeatMapAddLine(pHandle, pLeg->end, next_leg_pos, 20);
		}

		if (pLeg->prev)
		{
			AICivilianPathLeg *prevLeg = pLeg->prev;
			F32 *prev_leg_pos = (prevLeg->prev == pLeg) ? prevLeg->start : prevLeg->end;

			gslHeatMapAddLine(pHandle, pLeg->start, prev_leg_pos, 20);
		}
	}

	
	// draw all the intersections
	for (i = 0; i < eaSize(&pPathInfo->intersects); i++)
	{
		AICivilianPathIntersection *acpi = pPathInfo->intersects[i];
		EAICivilianLegType type = acpi->legIntersects[0]->leg->type;
		S32 n, x, num;
		if (type != g_eHeatMapSet)
		{
			continue;
		}
		num = eaSize(&acpi->legIntersects);

		for(n = 0; n < num; n++)
		{
			PathLegIntersect *pli1 = acpi->legIntersects[n];

			for(x = n + 1; x < num; x++)
			{
				PathLegIntersect *pli2 = acpi->legIntersects[x];
				Vec3 p1, p2;

				acgLegLegDistSquared(pli1->leg, p1, pli2->leg, p2);

				gslHeatMapAddLine(pHandle, p1, p2, 1);
			}

		}
	}

	if (s_legmapIncludePartition)
	{
		
		aiCivilian_HeatmapDumpPartitionLines(pPartition->pWorldLegGrid, pHandle, ppErrorString);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
// CGS_SPLIT
// ------------------------------------------------------------------------------------------------------------------

typedef struct ACGColumnSegment
{
	S32		startIdx;
	S32		endIdx;
} ACGColumnSegment;

typedef struct ACGColumn
{
	S32					columnIdx;
	ACGColumnSegment **eaSegments;

} ACGColumn;

typedef struct ACGSegmentGroup
{
	// the columns are down the direction of the leg, or the length
	S32					startColumnIdx;
	S32					endColumnIdx;

	//S32					columnStartMin;	// the original column start/end before growth/splits, etc
	//S32					columnEndMax;

	// the rows are the perp of the leg, or the width
	S32					startRowIdx;
	S32					endRowIdx;

	S32					rowStartMin;	// after all the merges, what the min/max of the row
	S32					rowEndMax;		// ""

	AICivilianPathLeg*	leg;

	// ACGColumnSegment **eaSegmentGroup;
	bool touched;
} ACGSegmentGroup;


#define ACG_SPLIT_START_OFFSET (0.25f)
#define ACG_SPLIT_STEP_DIST (1.0f)
#define ACG_SPLIT_CASTDIST_UP	(9.0f)
#define ACG_SPLIT_CASTDIST_DOWN	(20.0f)
#define ACG_SPLIT_INVALID_INDEX (-1)
#define ACG_SPLIT_COLUMNSEGMENT_MERGE_THRESHOLD (1)

#define ACG_SPLIT_GROUPING_DIST_THRESHOLD (6)
#define ACG_SPLIT_GROUPING_INVALID (-1)

#define ACG_SPLIT_GROUP_MINWIDTH_THRESHOLD (3)
#define ACG_SPLIT_GROUP_MINLEN_THRESHOLD (5)
#define ACG_SPLIT_GROUP_ALLOWGROW_THRESHOLD (12)
#define ACG_SPLIT_GROUP_OVERLAP_THRESHOLD (5)

#define ACG_SPLIT_GROUP_MERGING_ERROR_THRESHOLD (21)
#define ACG_SPLIT_GROUP_MERGE_DIST_THRESHOLD (2)

// ------------------------------------------------------------------------------------------------------------------
// ACGSegmentGroup functions
// ------------------------------------------------------------------------------------------------------------------
__forceinline S32 acgSegmentGroup_GetLength(const ACGSegmentGroup *group)
{
	return group->endColumnIdx - group->startColumnIdx;
}

__forceinline S32 acgSegmentGroup_GetWidth(const ACGSegmentGroup *group)
{
	return group->endRowIdx - group->startRowIdx;
}

static ACGSegmentGroup* acgSegmentGroup_Create(S32 startColumnIdx)
{
	ACGSegmentGroup *group = calloc(1, sizeof(ACGSegmentGroup));

	group->startColumnIdx = startColumnIdx;
	group->endColumnIdx = startColumnIdx;

	//group->columnStartMin = startColumnIdx;
	//group->columnEndMax = startColumnIdx;

	group->startRowIdx = ACG_SPLIT_INVALID_INDEX;
	group->endRowIdx = ACG_SPLIT_INVALID_INDEX;

	return group;
}

static ACGSegmentGroup* acgSegmentGroup_CreateCopy(const ACGSegmentGroup *other)
{
	ACGSegmentGroup *group = calloc(1, sizeof(ACGSegmentGroup));
	memcpy(group, other, sizeof(ACGSegmentGroup));
	return group;
}

static void acgSegmentGroup_Free(ACGSegmentGroup *group)
{
	// eaDestroyEx(&group->eaSegmentGroup, NULL);
	free(group);
}


// ------------------------------------------------------------------------------------------------------------------
void acgSegmentGroup_AddSegment(ACGSegmentGroup *group, const ACGColumnSegment *segment)
{
	if (group->startRowIdx == ACG_SPLIT_GROUPING_INVALID)
	{
		group->startRowIdx = segment->startIdx;
		group->endRowIdx = segment->endIdx;

		group->rowStartMin = segment->startIdx;
		group->rowEndMax = segment->endIdx;
	}
	else
	{
		group->startRowIdx = MAX(group->startRowIdx, segment->startIdx);
		group->endRowIdx = MIN(group->endRowIdx, segment->endIdx);

		// get the min/max so we can track the absolute error
		group->rowStartMin = MIN(group->rowStartMin, group->startRowIdx);
		group->rowEndMax = MAX(group->rowEndMax, group->endRowIdx);
	}

	// eaPush(&group->eaSegmentGroup, segment);

}

// ------------------------------------------------------------------------------------------------------------------
static S32 acgSegmentGroup_GetOverlapDistance(const ACGSegmentGroup *group, const ACGSegmentGroup *other_group)
{
	S32 endIdx, startIdx;

	// we have determined that they are column neighbors, now determine if they overlap at all.
	startIdx = MAX(group->startRowIdx, other_group->startRowIdx);
	endIdx = MIN(group->endRowIdx, other_group->endRowIdx);

	if (startIdx > endIdx)
	{	// they do not overlap
		return -1;
	}

	return endIdx - startIdx;
}

static bool acgSegmentGroup_IsGroupRowContained(const ACGSegmentGroup *container, const ACGSegmentGroup *other)
{
#define DIST_THRESHOLD (1)
	if (container->startRowIdx <= other->startRowIdx ||
		(container->startRowIdx - other->startRowIdx) <= DIST_THRESHOLD)
	{
		if (container->endRowIdx >= other->endRowIdx ||
			(other->endRowIdx - container->endRowIdx) <= DIST_THRESHOLD)
			return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgSegmentGroup_AreGroupsNeighbors(const ACGSegmentGroup *base, const ACGSegmentGroup *dest)
{
	S32 dist, endIdx, startIdx;

	dist = base->endColumnIdx - dest->startColumnIdx;
	if (ABS(dist) > 1)
	{
		// not neighbors on the end of the base's segments, check the start
		dist = dest->endColumnIdx - base->startColumnIdx;
		if (ABS(dist) > 1)
			return false;
	}

	// we have determined that they are column neighbors, now determine if they overlap at all.
	startIdx = MAX(base->startRowIdx, dest->startRowIdx);
	endIdx = MIN(base->endRowIdx, dest->endRowIdx);

	if (startIdx >= endIdx)
	{	// they do not overlap
		return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
// the two are assumed neighbors already
static bool acgSegmentGroup_IsLeftNeighbhor(const ACGSegmentGroup *base, const ACGSegmentGroup *other)
{
	return base->startColumnIdx > other->startColumnIdx;
}



// ------------------------------------------------------------------------------------------------------------------
static S32 acgSegmentGroup_CalculateMergeError(const ACGSegmentGroup *base, const ACGSegmentGroup *dest)
{
	S32 dist;
	S32 error = 0;
	S32 startIdx, endIdx;

	if (!acgSegmentGroup_AreGroupsNeighbors(base, dest))
	{	// these are not neighbors
		return ACG_SPLIT_GROUPING_INVALID;
	}

	// check if the overlap in the rows is wide enough to constitute the merge
	//
	startIdx = MAX(base->startRowIdx, dest->startRowIdx);
	endIdx = MIN(base->endRowIdx, dest->endRowIdx);
	if ((endIdx - startIdx) < ACG_SPLIT_GROUP_MINWIDTH_THRESHOLD)
	{
		return ACG_SPLIT_GROUPING_INVALID;
	}


	dist = base->startRowIdx - dest->startRowIdx;
	error = ABS(dist);
	dist = base->endRowIdx - dest->endRowIdx;
	error += ABS(dist);

	return error;
}

//static bool acgSegmentGroup_CanGroupGrowOnto


// ------------------------------------------------------------------------------------------------------------------
// this function will destroy the other's eaSegmentGroup list.
// It is the calling function's responsibility to free the other ACGSegmentGroup
static void acgSegmentGroup_MergeGroups(ACGSegmentGroup *dst, const ACGSegmentGroup *other)
{
	devassert(acgSegmentGroup_AreGroupsNeighbors(dst, other));

	dst->startColumnIdx = MIN(dst->startColumnIdx, other->startColumnIdx);
	//dst->columnStartMin = MIN(dst->columnStartMin, dst->startColumnIdx);

	dst->endColumnIdx = MAX(dst->endColumnIdx, other->endColumnIdx);
	//dst->columnEndMax = MAX(dst->columnEndMax, dst->endColumnIdx);

	dst->startRowIdx = MAX(dst->startRowIdx, other->startRowIdx);
	dst->rowStartMin = MIN(dst->rowStartMin, other->startRowIdx);

	dst->endRowIdx = MIN(dst->endRowIdx, other->endRowIdx);
	dst->rowEndMax = MAX(dst->rowEndMax, dst->endRowIdx);

	/*
	//
	{
		S32 insertionIdx;

		if (dst->startRowIdx > other->endRowIdx)
		{	// dest is to the right, insert at the beginning
			insertionIdx = 0;
		}
		else
		{
			// dest is to the left, insert at the end
			insertionIdx = eaSize(&dst->eaSegmentGroup) - 1;
		}

		eaInsertEArray(&dst->eaSegmentGroup, &other->eaSegmentGroup, insertionIdx);
	}

	eaDestroy(&other->eaSegmentGroup);
	*/
}

// ------------------------------------------------------------------------------------------------------------------
/*
static void acgSegmentGroup_Grow(ACGSegmentGroup *group, S32 amount, bool bStartEdge)
{
	if (bStartEdge)
	{
		group->startColumnIdx += amount;
	}
	else
	{
		group->endColumnIdx += amount;
	}
}
*/

// END // ACGSegmentGroup functions
// ------------------------------------------------------------------------------------------------------------------


// ------------------------------------------------------------------------------------------------------------------
__forceinline ACGColumnSegment* acgCreateColumnSegment(ACGColumn *parentColumn)
{
	ACGColumnSegment *segment = malloc(sizeof(ACGColumnSegment));

	segment->startIdx = ACG_SPLIT_INVALID_INDEX;
	segment->endIdx = ACG_SPLIT_INVALID_INDEX;

	eaPush(&parentColumn->eaSegments, segment);

	return segment;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline void acgFreeColumnSegment(ACGColumn *parentColumn, ACGColumnSegment **segment)
{
	if (*segment)
	{
		eaFindAndRemove(&parentColumn->eaSegments, *segment);
		free(*segment);
		*segment = NULL;
	}
}

static void acgFreeACGColumn(ACGColumn *column)
{
	eaDestroyEx(&column->eaSegments, NULL);
	free(column);
}



// ------------------------------------------------------------------------------------------------------------------
static void acgMergeCleanColumnSegments(ACGColumn *column)
{
	S32 i = 0;
	while(i < (eaSize(&column->eaSegments) - 1))
	{
		S32 dist;
		ACGColumnSegment *segment = column->eaSegments[i];
		ACGColumnSegment *nextSegment = column->eaSegments[i + 1];


		dist = nextSegment->startIdx - segment->endIdx;
		if (dist <= ACG_SPLIT_COLUMNSEGMENT_MERGE_THRESHOLD)
		{
			// merge curSegment with the nextSegment
			segment->endIdx = nextSegment->endIdx;
			eaRemove(&column->eaSegments, i + 1);
			free(nextSegment);
			continue;
		}

		i++;
	}

}


// ------------------------------------------------------------------------------------------------------------------
// sweep the leg!
static bool acgSplit_SweepColumn(int iPartitionIdx, const AICivilianPathLeg *leg, F32 fCurLen, ACGColumnSegment *curSegment)
{
	WorldCollCollideResults results = {0};
	EAICivilianLegType type;
	Vec3 vColumnStart, vColumnEnd;
	Vec3 vImpactPosStart, vImpactNormalStart;
	Vec3 vImpactPosEnd, vImpactNormalEnd;
	F32 fWidthStart = -(leg->width * 0.5f) + ACG_SPLIT_START_OFFSET;
	F32 fWidthEnd = (leg->width * 0.5f) - ACG_SPLIT_START_OFFSET;
	
	scaleAddVec3(leg->dir, fCurLen, leg->start, vColumnStart);
	scaleAddVec3(leg->perp, fWidthStart, vColumnStart, vColumnStart);

	scaleAddVec3(leg->dir, fCurLen, leg->start, vColumnEnd);
	scaleAddVec3(leg->perp, fWidthEnd, vColumnEnd, vColumnEnd);

	acgUtil_CastVerticalRay(iPartitionIdx, &results, vColumnStart, ACG_SPLIT_CASTDIST_UP, ACG_SPLIT_CASTDIST_DOWN);
	if (! results.hitSomething)
		return false;

	type = aiCivGenClassifyResults(&results);
	if (type != leg->type && leg->bIsForcedLeg == 0)
		return false;
	copyVec3(results.posWorldImpact, vImpactPosStart);
	copyVec3(results.normalWorld, vImpactNormalStart);

	acgUtil_CastVerticalRay(iPartitionIdx, &results, vColumnEnd, ACG_SPLIT_CASTDIST_UP, ACG_SPLIT_CASTDIST_DOWN);
	if (! results.hitSomething)
		return false;

	type = aiCivGenClassifyResults(&results);
	if (type != leg->type && leg->bIsForcedLeg == 0)
		return false;
	copyVec3(results.posWorldImpact, vImpactPosEnd);
	copyVec3(results.normalWorld, vImpactNormalEnd);
	

	// forced legs get special treatment as they are sometimes placed on non-civilian material. 
	{
		Capsule cap;
		Vec3 vCaspuleStartPos, vCaspuleEndPos;
		WorldCollCollideResults boxCast_Results = {0};

		static const F32 CAPSULE_SIZE = 1.0f;
		static const F32 CAPSULE_HEIGHT = 5.0f;
		static const F32 GROUND_OFFSET = 1.15f;
		
		
		cap.fLength = CAPSULE_HEIGHT;
		setVec3(cap.vStart, 0.0f, 0.0f, 0.0f);
		copyVec3(vImpactNormalStart, cap.vDir);
		cap.fRadius = CAPSULE_SIZE;

		scaleAddVec3(vImpactNormalStart, GROUND_OFFSET, vImpactPosStart, vCaspuleStartPos);
		scaleAddVec3(vImpactNormalStart, GROUND_OFFSET, vImpactPosEnd, vCaspuleEndPos);

		wcCapsuleCollideEx(worldGetActiveColl(iPartitionIdx), cap, vCaspuleStartPos, vCaspuleEndPos, WC_QUERY_BITS_AI_CIV, &boxCast_Results); 
		if (boxCast_Results.hitSomething)
		{	
			return false;
		}
		
		curSegment->startIdx = 0;
		curSegment->endIdx = (S32)floorf( (leg->width - ACG_SPLIT_START_OFFSET) / ACG_SPLIT_STEP_DIST);
		
		return true;
	}
	
}

// ------------------------------------------------------------------------------------------------------------------
static void acgGenerateLegColumns(int iPartitionIdx, Entity *debugger, AICivilianPathLeg *leg, ACGColumn ***peaColumns, S32 *pLastSegmentCount, S32 *pLastRowIdx)
{
	const F32 	fHalfWidth = leg->width * 0.5f;
	F32		fLenDist = ACG_SPLIT_START_OFFSET;
	S32		columnIdx = 0;
	S32		rowIdx = 0;
	Vec3	vPos;

	columnIdx = 0;

	// walk over the leg, casting to find the ground nodes
	while(fLenDist <= leg->len)
	{
		ACGColumn *column = NULL;
		ACGColumnSegment *curSegment = NULL;
		F32		fWidthDist;

		rowIdx = 0;

		// initialize the column and our starting segment
		column = calloc(1, sizeof(ACGColumn));
		column->columnIdx = columnIdx;
		eaPush(peaColumns, column);

		curSegment = acgCreateColumnSegment(column);

		// initialize our line walking this column
		fWidthDist = -fHalfWidth + ACG_SPLIT_START_OFFSET;
		scaleAddVec3(leg->dir, fLenDist, leg->start, vPos);
		scaleAddVec3(leg->perp, fWidthDist, vPos, vPos);
		while(fWidthDist <= fHalfWidth)
		{
			//
			Vec3 vCastPosUp, vCastPosDown;
			WorldCollCollideResults results = {0};
			EAICivilianLegType type;

			copyVec3(vPos, vCastPosUp);
			copyVec3(vPos, vCastPosDown);
			vCastPosUp[1] += ACG_SPLIT_CASTDIST_UP;
			vCastPosDown[1] -= ACG_SPLIT_CASTDIST_DOWN;

			acgCastRay(iPartitionIdx, &results, vCastPosUp, vCastPosDown);

			// Debug drawing raycasts
			if (acg_d_split && acg_d_pos)
			{
				#define ACG_SPLIT_DEBUG_RAYCAST_DISTSQ	SQR(10)
				if (distance3SquaredXZ(vPos, s_acgDebugPos) < ACG_SPLIT_DEBUG_RAYCAST_DISTSQ)
				{
					wlAddClientLine(debugger, vCastPosUp, vCastPosDown, 0xFF00FF00);
					wlAddClientPoint(debugger, results.posWorldImpact, 0xFFFF0000);
				}
			}

			type = aiCivGenClassifyResults(&results);

			//if (type != leg->type && type == -1 ) // && leg->bIsForcedLeg)
			if ((type == leg->type) || (type == EAICivilianLegType_NONE && leg->bIsForcedLeg))
			{
				#define CAPSULE_SIZE	0.75f
				#define CAPSULE_HEIGHT	1.0f
				#define GROUND_OFFSET_START 7.0f
				#define GROUND_OFFSET_END 2.0f
				#define HORIZ_OFFSET 0.15f
				// forced legs get special treatment as they are sometimes placed on non-civilian material. 

				Capsule cap;
				Vec3 vCastStartPos, vCastEndPos;
				WorldCollCollideResults boxCast_Results = {0};


				cap.fLength = CAPSULE_HEIGHT;
				setVec3(cap.vStart, 0.0f, 0.0f, 0.0f);
				copyVec3(results.normalWorld, cap.vDir);
				cap.fRadius = CAPSULE_SIZE;

				scaleAddVec3(results.normalWorld, GROUND_OFFSET_START, results.posWorldImpact, vCastStartPos);
				scaleAddVec3(leg->dir, HORIZ_OFFSET, vCastStartPos, vCastStartPos);
				scaleAddVec3(results.normalWorld, GROUND_OFFSET_END, results.posWorldImpact, vCastEndPos);

				

				wcCapsuleCollideEx(worldGetActiveColl(iPartitionIdx), cap, vCastStartPos, vCastEndPos, WC_QUERY_BITS_AI_CIV, &boxCast_Results); 
				if (!boxCast_Results.hitSomething)
				{	// the box didn't hit anything, so we're going to assume we can walk here
					// set the type as the valid leg type
					type = leg->type;
				}
				else
				{
					type = EAICivilianLegType_NONE;
				}
			
				if (acg_d_split && acg_d_pos)
				{
					#define ACG_SPLIT_DEBUG_CAPSULE_DISTSQ	SQR(10)
					if (distance3SquaredXZ(vPos, s_acgDebugPos) < ACG_SPLIT_DEBUG_CAPSULE_DISTSQ)
					{
						wlAddClientPoint(debugger, vCastStartPos, 0xFF0000FF);

						if (type == EAICivilianLegType_NONE)
						{
							wlAddClientPoint(debugger, vCastEndPos, 0xFFFF0000);
						}
						else
						{
							wlAddClientPoint(debugger, vCastEndPos, 0xFF00FF00);
						}
						
					}
				}
			}

			if (type != leg->type)
			{
				
				// the type is not the same as the leg's
				// if this segment was already started, end it and create a new segment
				if (curSegment->startIdx != ACG_SPLIT_INVALID_INDEX)
				{
					curSegment->endIdx = rowIdx - 1;
					curSegment = acgCreateColumnSegment(column);
				}
			}
			else
			{
				if (curSegment->startIdx == ACG_SPLIT_INVALID_INDEX)
				{
					// the segment was not started yet, so set it's starting index
					curSegment->startIdx = rowIdx;
				}
			}
			//

			// advance iteration over the leg's column
			rowIdx++;
			fWidthDist += ACG_SPLIT_STEP_DIST;
			scaleAddVec3(leg->perp, ACG_SPLIT_STEP_DIST, vPos, vPos);
		}


		// Finish off the current segment,
		// if the start index is invalid, then we'll delete it
		// otherwise, set the end index to the last rowIndex
		if (curSegment->startIdx == ACG_SPLIT_INVALID_INDEX)
		{
			acgFreeColumnSegment(column, &curSegment);
		}
		else
		{
			curSegment->endIdx = rowIdx - 1;
			curSegment = NULL;
		}

		// finally, clean up the column
		acgMergeCleanColumnSegments(column);
		


		columnIdx++;
		fLenDist += ACG_SPLIT_STEP_DIST;
	}

	*pLastRowIdx = rowIdx - 1;
	*pLastSegmentCount = columnIdx - 1;

}

// ------------------------------------------------------------------------------------------------------------------
static ACGSegmentGroup* acgSplit_FindMatchingSegmentInGroups(ACGSegmentGroup **eaOpenGroups, const ACGColumnSegment *segment)
{
	S32 z = 0;
	for (z = 0; z < eaSize(&eaOpenGroups); z++)
	{
		S32 dist;
		ACGSegmentGroup *group = eaOpenGroups[z];

#if 0
		// this is by the last segment in the group
		ACGColumnSegment *groupSegment = eaTail(&group->eaSegmentGroup);

		devassert(groupSegment);

		dist = groupSegment->startIdx - segment->startIdx;
		if (ABS(dist) < ACG_SPLIT_GROUPING_DIST_THRESHOLD)
		{
			dist = groupSegment->endIdx - segment->endIdx;
			if (ABS(dist) < ACG_SPLIT_GROUPING_DIST_THRESHOLD)
			{
				return group;
			}
		}
#else
		// this is by the min/max row indicies
		dist = group->rowStartMin - segment->startIdx;
		if (ABS(dist) < ACG_SPLIT_GROUPING_DIST_THRESHOLD)
		{
			dist = group->rowEndMax - segment->endIdx;
			if (ABS(dist) < ACG_SPLIT_GROUPING_DIST_THRESHOLD)
			{
				return group;
			}
		}
#endif


	}

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
static void acgUnTouchGroups(ACGSegmentGroup *group)
{
	group->touched = false;
}

// ------------------------------------------------------------------------------------------------------------------
// this function takes all the columns' segments and groups them together
static void acgCollapseColumnsIntoSegmentGroups(const AICivilianPathLeg *leg,
												const ACGColumn **eaColumns, ACGSegmentGroup ***peaSegmentGroups)
{
	ACGSegmentGroup **eaOpenGroups = NULL; // groups that are currently have segments being added to them

	S32 i;

	// walk the columns and group the segments that are close together.
	//
	for (i = 0; i < eaSize(&eaColumns); i++)
	{
		S32 x;
		const ACGColumn *column = eaColumns[i];

		for (x = 0; x < eaSize(&column->eaSegments); x++)
		{
			const ACGColumnSegment *segment = column->eaSegments[x];
			ACGSegmentGroup *destGroup;

			// see if this segment fits with any open groups
			destGroup = acgSplit_FindMatchingSegmentInGroups(eaOpenGroups, segment);
			if (destGroup == NULL)
			{
				// no group lines up to the segment, create a new group
				destGroup = acgSegmentGroup_Create(column->columnIdx);

				eaPush(peaSegmentGroups, destGroup);
				eaPush(&eaOpenGroups, destGroup);
			}

			devassert(destGroup);

			acgSegmentGroup_AddSegment(destGroup, segment);
			destGroup->touched = true;
		}

		/*
		// the segments are now owned by the groups, just destroy the column's segment list
		eaDestroy(&column->eaSegments);
		*/

		// remove all the groups on the open list that have not been touched this pass
		for (x = eaSize(&eaOpenGroups) - 1; x >= 0; x--)
		{
			ACGSegmentGroup *group = eaOpenGroups[x];
			if (group->touched == false)
			{
				// remove this group from the open groups
				eaRemoveFast(&eaOpenGroups, x);
				//
				group->endColumnIdx = column->columnIdx - 1;
				//group->columnEndMax = group->endColumnIdx;
			}
		}

		// mark all the open groups as untouched
		eaForEach(&eaOpenGroups, acgUnTouchGroups);
	}

	// go through all the open groups and set the endColumnIdx
	for (i = eaSize(&eaOpenGroups) - 1; i >= 0; i--)
	{
		ACGSegmentGroup *group = eaOpenGroups[i];
		const ACGColumn *column = eaTail(&eaColumns);

		group->endColumnIdx = column->columnIdx;
		//group->columnEndMax = group->endColumnIdx;

	}


	// clean up the groups
	{
		// remove all the groups that are not wide enough
		for (i = eaSize(peaSegmentGroups) - 1; i >= 0; i--)
		{
			ACGSegmentGroup *group = (*peaSegmentGroups)[i];
			S32 width = acgSegmentGroup_GetWidth(group);
			
			if ((width < ACG_SPLIT_GROUP_MINWIDTH_THRESHOLD && 
					leg->width > ACG_SPLIT_GROUP_MINWIDTH_THRESHOLD) || width <= 1)
			{

				// this group segment isn't wide enough, so we must delete it
				eaRemove(peaSegmentGroups, i);
				acgSegmentGroup_Free(group);
			}
		}

		if (eaSize(peaSegmentGroups) > 1)
		{
			// remove all the groups that do not border any other group?
			for (i = eaSize(peaSegmentGroups) - 1; i >= 0; i--)
			{
				ACGSegmentGroup *group = (*peaSegmentGroups)[i];
				bool neighborFound = false;

				S32 x;
				for (x = eaSize(peaSegmentGroups) - 1; x >= 0; x--)
				{
					ACGSegmentGroup *other_group = (*peaSegmentGroups)[x];

					if (x == i)
						continue;

					if (acgSegmentGroup_AreGroupsNeighbors(group, other_group))
					{
						neighborFound = true;
						break;
					}
				}

				if (!neighborFound)
				{
					// this group segment has no neighbors, delete it
					eaRemove(peaSegmentGroups, i);
					acgSegmentGroup_Free(group);
				}
			}
		}
	}



	eaDestroy(&eaOpenGroups);

}

// ------------------------------------------------------------------------------------------------------------------
static ACGSegmentGroup** acgSegmentGroup_FindNeighborsOnEdge(ACGSegmentGroup **eaGroups, const ACGSegmentGroup *group, const bool bStartEdge)
{
	ACGSegmentGroup **eaNeighbors = NULL;

	S32 x;
	for (x = 0; x < eaSize(&eaGroups); x++)
	{
		ACGSegmentGroup *other_group = eaGroups[x];

		if (group == other_group)
			continue;

		if (acgSegmentGroup_AreGroupsNeighbors(group, other_group))
		{
			bool bOnStartEdge = (group->startColumnIdx > other_group->startColumnIdx);

			if (bOnStartEdge == bStartEdge)
			{
				eaPush(&eaNeighbors, other_group);
			}
		}
	}

	return eaNeighbors;
}

// ------------------------------------------------------------------------------------------------------------------
// go through the groups, and any that are too short, see if they have room to grow onto the nearby groups
static void acgSplit_GrowGroups(ACGSegmentGroup **eaGroups)
{
	S32 i;
	for (i = 0; i < eaSize(&eaGroups); i++)
	{
		S32 x;
		bool bWasGrown = false;
		ACGSegmentGroup *group = eaGroups[i];

		if (acgSegmentGroup_GetLength(group) > ACG_SPLIT_GROUP_MINLEN_THRESHOLD)
		{	// this group is long enough, let it be
			continue;
		}

		for(x = 0; x < eaSize(&eaGroups); x++)
		{
			ACGSegmentGroup *other_group = eaGroups[x];

			if (x == i)
				continue;

			// the width of the other_group needs to exceed the threshold to allow for growing
			// the groups also need to overlap by a certain amount
			if (acgSegmentGroup_GetLength(other_group) < ACG_SPLIT_GROUP_ALLOWGROW_THRESHOLD)
				continue; // this group isn't long enough to allow for growing

			//if (acgSegmentGroup_GetOverlapDistance(group, other_group) < ACG_SPLIT_GROUP_OVERLAP_THRESHOLD)
			//	continue;	// these do not overlap enough

			if (! acgSegmentGroup_IsGroupRowContained(other_group, group))
				continue;	// the group needs to be completely contained in otherGroup

			if (! acgSegmentGroup_AreGroupsNeighbors(group, other_group))
				continue;	// these aren't neighbors

			// TODO: we need to find other neighbors of other_group that are on the same border
			// as the initial group. we have to grow all the neighbors by the same amount so we don't
			// break apart connections.
			{
				S32 nn;
				ACGSegmentGroup **eaGroupsToGrow = NULL;
				bool bOnOtherStartEdge = (other_group->startColumnIdx > group->startColumnIdx);
				S32 growAmount;
				bool bIsGrowingKosher = true;

				eaGroupsToGrow = acgSegmentGroup_FindNeighborsOnEdge(eaGroups, other_group, bOnOtherStartEdge);
				devassert(eaSize(&eaGroupsToGrow) && (eaFind(&eaGroupsToGrow, group) != -1) );

				for (nn = 0; nn < eaSize(&eaGroupsToGrow); nn++)
				{
					ACGSegmentGroup *groupToGrow = eaGroupsToGrow[nn];
					if (! acgSegmentGroup_IsGroupRowContained(other_group, groupToGrow))
					{
						// one of the groups is not toally contained, so we cannot grow!
						// as this will most likely push into a non traversable space :(
						bIsGrowingKosher = false;
					}
				}

				if (bIsGrowingKosher)
				{
					growAmount = 1 + ACG_SPLIT_GROUP_MINLEN_THRESHOLD - acgSegmentGroup_GetLength(group);

					if (bOnOtherStartEdge)
					{// group->end growing onto other_group->start
						// shrink the other group from the start
						other_group->startColumnIdx += growAmount;
					}
					else
					{// group->start growing onto other_group->end
						// shrink the other group from the end
						other_group->endColumnIdx -= growAmount;
					}


					for (nn = 0; nn < eaSize(&eaGroupsToGrow); nn++)
					{
						ACGSegmentGroup *groupToGrow = eaGroupsToGrow[nn];
						if (bOnOtherStartEdge)
						{
							// group->end growing onto other_group->start
							groupToGrow->endColumnIdx += growAmount;
						}
						else
						{
							// group->end growing onto other_group->start
							groupToGrow->startColumnIdx -= growAmount;
						}
					}

				}

				eaDestroy(&eaGroupsToGrow);
			}

			bWasGrown = true;
			break;
		}

	}

}


// ------------------------------------------------------------------------------------------------------------------
// when this function returns, new segments are added to the master list.
// The toClip group is untouched and still in the list

static bool acgSegmentGroup_ClipSegmentEx(S32 rowIdx, const ACGSegmentGroup *toClip, ACGSegmentGroup **topGroup, ACGSegmentGroup **botGroup)
{
	*topGroup = NULL;
	*botGroup = NULL;

	if (rowIdx > toClip->startRowIdx && rowIdx < toClip->endRowIdx)
	{
		*topGroup = acgSegmentGroup_CreateCopy(toClip);
		*botGroup = acgSegmentGroup_CreateCopy(toClip);

		(*topGroup)->endRowIdx = rowIdx;
		(*topGroup)->rowEndMax = rowIdx;

		(*botGroup)->startRowIdx = rowIdx;
		(*botGroup)->rowStartMin = rowIdx;
		return true;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool acgSplit_AttemptToMerge(ACGSegmentGroup *dest, const ACGSegmentGroup *other_group)
{
	S32 dist;

	if (! acgSegmentGroup_AreGroupsNeighbors(dest, other_group))
		return false;

	//
	dist = dest->startRowIdx - other_group->startRowIdx;
	//dist = group->rowStartMin - other_group->rowStartMin;
	if (ABS(dist) > ACG_SPLIT_GROUP_MERGE_DIST_THRESHOLD)
		return false;

	dist = dest->endRowIdx - other_group->endRowIdx;
	//dist = group->rowEndMax - other_group->rowEndMax;
	if (ABS(dist) > ACG_SPLIT_GROUP_MERGE_DIST_THRESHOLD)
		return false;

	// merge the groups
	acgSegmentGroup_MergeGroups(dest, other_group);
	return true;
}

#define MAX_NEW_GROUPS 3
static void acgSplit_AttemptToMergeGroups(ACGSegmentGroup *dest, ACGSegmentGroup *newGroups[MAX_NEW_GROUPS],
										  ACGSegmentGroup ***peaGroupList)
{
	S32 nn = 0;
	// try to merge any of the new groups
	do{
		if (!newGroups[nn])
			break;

		if (acgSplit_AttemptToMerge(dest, newGroups[nn]))
		{
			acgSegmentGroup_Free(newGroups[nn]);
			newGroups[nn] = NULL;
		}
	}while(++nn < MAX_NEW_GROUPS);

	nn = 0;
	do{
		if (newGroups[nn])
		{
			eaPush(peaGroupList, newGroups[nn]);
		}
	}while(++nn < MAX_NEW_GROUPS);
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgSegmentGroup_ClipAndMergeSegment(ACGSegmentGroup *clipper, const ACGSegmentGroup *toClip,
												ACGSegmentGroup ***peaGroupList)
{
	ACGSegmentGroup *newGroups[MAX_NEW_GROUPS] = {0};
	ACGSegmentGroup *topGroup = NULL, *botGroup = NULL;


	if (acgSegmentGroup_ClipSegmentEx(clipper->startRowIdx, toClip, &topGroup, &botGroup))
	{
		ACGSegmentGroup *teir2TopGroup = NULL, *teir2BotGroup = NULL;

		// was clipped against the top;
		// we need to check the clipper's bottom row against each of the new groups
		if (acgSegmentGroup_ClipSegmentEx(clipper->endRowIdx, topGroup, &teir2TopGroup, &teir2BotGroup))
		{
			// the top was clipped in two, delete it since it is no longer needed
			acgSegmentGroup_Free(topGroup);
			topGroup = NULL;

			newGroups[0] = botGroup;
			newGroups[1] = teir2TopGroup;
			newGroups[2] = teir2BotGroup;
			acgSplit_AttemptToMergeGroups(clipper, newGroups, peaGroupList);
			return true;
		}

		if (acgSegmentGroup_ClipSegmentEx(clipper->endRowIdx, botGroup, &teir2TopGroup, &teir2BotGroup))
		{
			// the top was clipped in two, delete it since it is no longer needed
			acgSegmentGroup_Free(botGroup);
			botGroup = NULL;

			// push the 3 new groups onto the list
			newGroups[0] = topGroup;
			newGroups[1] = teir2TopGroup;
			newGroups[2] = teir2BotGroup;
			acgSplit_AttemptToMergeGroups(clipper, newGroups, peaGroupList);
			return true;
		}

		// if we get here, the end row didn't clip the new groups, push these two and exit
		newGroups[0] = topGroup;
		newGroups[1] = botGroup;
		acgSplit_AttemptToMergeGroups(clipper, newGroups, peaGroupList);
		return true;
	}

	if (acgSegmentGroup_ClipSegmentEx(clipper->endRowIdx, toClip, &topGroup, &botGroup))
	{
		// only the end row clipped, push the new groups
		newGroups[0] = topGroup;
		newGroups[1] = botGroup;
		acgSplit_AttemptToMergeGroups(clipper, newGroups, peaGroupList);
		return true;
	}

	return false;
}




// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_ClipGroups(ACGSegmentGroup ***peaGroups, bool clipLarger)
{
	S32 i;

	for( i = 0; i < eaSize(peaGroups); i++)
	{
		S32 x;
		ACGSegmentGroup *group = (*peaGroups)[i];

		if (acgSegmentGroup_GetLength(group) > ACG_SPLIT_GROUP_MINLEN_THRESHOLD)
		{	// this group is long enough, let it be
			continue;
		}

		// this group is short and wants to do some clipping

		for(x = 0; x < eaSize(peaGroups); x++)
		{
			#define MAX_NEW_GROUPS 3
			ACGSegmentGroup *other_group = (*peaGroups)[x];

			if (x == i)
				continue;

			if (! acgSegmentGroup_AreGroupsNeighbors(group, other_group))
				continue;

			if (acgSegmentGroup_GetOverlapDistance(group, other_group) < ACG_SPLIT_GROUP_OVERLAP_THRESHOLD)
				continue;	// these do not overlap enough

			if (! clipLarger)
			{
				// for now I'm only clipping the shorter segment
				if (acgSegmentGroup_ClipAndMergeSegment(other_group, group, peaGroups))
				{
					// remove the old group we clipped
					eaRemove(peaGroups, i);
					acgSegmentGroup_Free(group);
					i--;
					break;
				}
			}
			else
			{
				if (acgSegmentGroup_ClipAndMergeSegment(group, other_group, peaGroups))
				{
					// delete our group
					eaRemove(peaGroups, x);
					acgSegmentGroup_Free(other_group);
					x--;
					break;
				}
			}
		}

	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_RemoveOrphansAndDeadEnds(ACGSegmentGroup **eaGroups)
{
	S32 i, lastColumn = 0;

	if (eaSize(&eaGroups) == 1)
		return;

	for( i = 0; i < eaSize(&eaGroups); i++)
	{
		ACGSegmentGroup *group = eaGroups[i];

		if (group->endColumnIdx > lastColumn)
			lastColumn = group->endColumnIdx;
	}

	for( i = eaSize(&eaGroups)-1; i >= 0; i--)
	{
		ACGSegmentGroup *group = eaGroups[i];
		S32 x;
		bool bHasLeftNeighbor, bHasRightNeighbor;

		bHasLeftNeighbor = (group->startColumnIdx == 0);
		bHasRightNeighbor = (group->endColumnIdx == lastColumn);

		for(x = 0; x < eaSize(&eaGroups); x++)
		{
			ACGSegmentGroup *other_group = eaGroups[x];

			if (! acgSegmentGroup_AreGroupsNeighbors(group, other_group))
				continue;

			if (acgSegmentGroup_GetOverlapDistance(group, other_group) < ACG_SPLIT_GROUP_OVERLAP_THRESHOLD)
				continue;	// these do not overlap enough

			if (acgSegmentGroup_IsLeftNeighbhor(group, other_group))
			{
				bHasLeftNeighbor = true;
			}
			else
			{
				bHasRightNeighbor = true;
			}

			if (bHasLeftNeighbor && bHasRightNeighbor)
				break;
		}


		if (!bHasLeftNeighbor || !bHasRightNeighbor)
		{
			// delete this mofo
			eaRemove(&eaGroups, i);
			acgSegmentGroup_Free(group);
			i = eaSize(&eaGroups);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_MergeGroups(ACGSegmentGroup **eaGroups)
{
	S32 i;

	if (eaSize(&eaGroups) == 1)
		return;

	for( i = eaSize(&eaGroups)-1; i >= 0; i--)
	{
		S32 x;
		ACGSegmentGroup *group = eaGroups[i];

		for(x = 0; x < eaSize(&eaGroups); x++)
		{
			ACGSegmentGroup *other_group = eaGroups[x];

			if (x == i)
				continue;

			if (acgSplit_AttemptToMerge(group, other_group))
			{
				eaRemove(&eaGroups, x);
				acgSegmentGroup_Free(other_group);
				// reset our iteration
				i = eaSize(&eaGroups);
				break;
			}
		}

	}
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static void acgSegment_GetQuad(const AICivilianPathLeg *leg, const ACGSegmentGroup *group, Vec3 quad[4])
{
	F32 fColumnDist;
	F32 fSegmentDist;

#define ACG_SEGMENT_GET_POS(idx, pos)									\
	fSegmentDist = (-leg->width * 0.5f) + (idx * ACG_SPLIT_STEP_DIST);	\
	scaleAddVec3(leg->dir, fColumnDist, leg->start, pos);				\
	scaleAddVec3(leg->perp, fSegmentDist, pos, pos);					\

	fColumnDist = group->startColumnIdx * ACG_SPLIT_STEP_DIST;
	ACG_SEGMENT_GET_POS(group->startRowIdx, quad[0]);
	ACG_SEGMENT_GET_POS(group->endRowIdx, quad[1]);

	fColumnDist = group->endColumnIdx * ACG_SPLIT_STEP_DIST;
	ACG_SEGMENT_GET_POS(group->startRowIdx, quad[2]);
	ACG_SEGMENT_GET_POS(group->endRowIdx, quad[3]);

}


// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_GetGroupNeighbors(ACGSegmentGroup **eaGroups, const ACGSegmentGroup *pGroup, ACGSegmentGroup ***peaNeighbors)
{
	S32 x;

	eaClear(peaNeighbors);

	for(x = 0; x < eaSize(&eaGroups); x++)
	{
		ACGSegmentGroup *other_group = eaGroups[x];

		if (other_group == pGroup)
		{
			continue;
		}

		if (acgSegmentGroup_AreGroupsNeighbors(pGroup, other_group))
		{
			eaPush(peaNeighbors, other_group);
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_LinkToNeighbors(const ACGSegmentGroup **eaNeighbors, AICivilianPathLeg *leg, bool bLeftNeighbor)
{
	if (eaSize(&eaNeighbors))
	{
		if (eaSize(&eaNeighbors) == 1)
		{
			// only one neighbor, these can be directly connected
			const ACGSegmentGroup *neighbor = eaHead(&eaNeighbors);
			// these two will be directly connected
			if (bLeftNeighbor)
			{
				leg->prev = neighbor->leg;
			}
			else
			{
				leg->next = neighbor->leg;
			}
		}
		else
		{
			S32 x;
			AICivilianPathIntersection *acpi;
			PathLegIntersect *pli;

			acpi = acgPathIntersection_Alloc();
			
			eaPush(&s_acgPathInfo.intersects, acpi);

			// more than one connection. we need to create an intersection
			for (x = 0; x < eaSize(&eaNeighbors); x++)
			{
				const ACGSegmentGroup *neighbor = eaNeighbors[x];

				pli = acgPathLegIntersect_Alloc();
				pli->leg = neighbor->leg;

				eaPush(&acpi->legIntersects, pli);
			}

			if (bLeftNeighbor)
			{
				leg->prevInt = acpi;
			}
			else
			{
				leg->nextInt = acpi;
			}


		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgReplaceLegConnection(AICivilianPathLeg *newLeg, AICivilianPathLeg *connectedLeg, const AICivilianPathLeg *remLeg)
{
	U32 dbgLinks = 0, dbgLinksCount = 0;

	if (connectedLeg->prev == remLeg)
	{
		connectedLeg->prev = newLeg;
		dbgLinks |= 0x01;
		dbgLinksCount++;
	}

	if (connectedLeg->next == remLeg)
	{
		connectedLeg->next = newLeg;
		dbgLinks |= 0x02;
		dbgLinksCount++;
	}

	// check for intersections
	if (connectedLeg->nextInt && !connectedLeg->nextInt->bIsMidIntersection)
	{
		AICivilianPathIntersection *acpi = connectedLeg->nextInt;

		S32 idx = acgFindPathLegInACPI(acpi, remLeg);
		if (idx != -1)
		{
			acpi->legIntersects[idx]->leg = newLeg;
			dbgLinks |= 0x04;
			dbgLinksCount++;
		}
	}

	if (connectedLeg->prevInt && !connectedLeg->prevInt->bIsMidIntersection)
	{
		AICivilianPathIntersection *acpi = connectedLeg->prevInt;

		S32 idx = acgFindPathLegInACPI(acpi, remLeg);
		if (idx != -1)
		{
			acpi->legIntersects[idx]->leg = newLeg;
			dbgLinks |= 0x08;
			dbgLinksCount++;
		}
	}

#if defined(LEG_DESTROY_PARANOID)
	if (dbgLinksCount > 1)
	{
		devassert(0);
	}
#endif
}


// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_ConnectCreateNewACPI(AICivilianPathLeg *connectedLeg, bool connectedLegPrev,
										  ACGSegmentGroup **eaSegmentGroups, bool newLegPrev)
{
	S32 i;
	AICivilianPathIntersection *new_acpi;
	PathLegIntersect *pli;

	new_acpi = acgPathIntersection_Alloc();
	eaPush(&s_acgPathInfo.intersects, new_acpi);

	// first add the legPrev
	pli = acgPathLegIntersect_Alloc();
	pli->leg = connectedLeg;
	eaPush(&new_acpi->legIntersects, pli);

	// add all the new legs to the intersection
	for (i = 0; i < eaSize(&eaSegmentGroups); i++)
	{
		AICivilianPathLeg *newLeg = eaSegmentGroups[i]->leg;
		pli = acgPathLegIntersect_Alloc();

		if (newLegPrev)
		{
			newLeg->prevInt = new_acpi;
		}
		else
		{
			newLeg->nextInt = new_acpi;
		}

		newLeg->leg_set = s_acgProcess.split.set_count;

		pli->leg = newLeg;
		eaPush(&new_acpi->legIntersects, pli);
	}

	s_acgProcess.split.set_count++;

	if (connectedLegPrev)
	{
		connectedLeg->prev = NULL;
		devassert(connectedLeg->prevInt == NULL);
		connectedLeg->prevInt = new_acpi;
	}
	else
	{
		connectedLeg->next = NULL;
		devassert(connectedLeg->nextInt == NULL);
		connectedLeg->nextInt = new_acpi;
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_AddLegsToACPI(AICivilianPathIntersection *acpi, ACGSegmentGroup **eaSegmentGroups, bool newLegsPrev)
{
	S32 i;

	// add all the new legs to the intersection
	for (i = 0; i < eaSize(&eaSegmentGroups); i++)
	{
		AICivilianPathLeg *newLeg = eaSegmentGroups[i]->leg;
		PathLegIntersect* pli = acgPathLegIntersect_Alloc();

		if (newLegsPrev)
		{
			newLeg->prevInt = acpi;
		}
		else
		{
			newLeg->nextInt = acpi;
		}

		newLeg->leg_set = s_acgProcess.split.set_count;

		pli->leg = newLeg;
		eaPush(&acpi->legIntersects, pli);
	}

	s_acgProcess.split.set_count++;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_RemLegAddLegsToACPI(AICivilianPathLeg *connectedLeg, bool connectedLegPrev,
										 ACGSegmentGroup **eaSegmentGroups, bool newLegsPrev, const AICivilianPathLeg *remLeg)
{
	AICivilianPathIntersection *acpi;
	S32 idx;

	if (connectedLegPrev)
	{
		acpi = connectedLeg->prevInt;
	}
	else
	{
		acpi = connectedLeg->nextInt;
	}

	idx = acgFindPathLegInACPI(acpi, remLeg);
	if (idx != -1)
	{
		PathLegIntersect *pli = acpi->legIntersects[idx];
		eaRemoveFast(&acpi->legIntersects, idx);
		acgPathLegIntersect_Free(pli);

		acgSplit_AddLegsToACPI(acpi, eaSegmentGroups, newLegsPrev);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_ConnectLegs(const AICivilianPathLeg *remLeg, AICivilianPathLeg *otherLeg,
								 ACGSegmentGroup **eaSegmentGroups, bool newLegsPrev)
{
	AICivilianPathIntersection *remLegInt;

	if (eaSize(&eaSegmentGroups) == 0)
		return;

	if (newLegsPrev)
	{
		remLegInt = remLeg->prevInt;
	}
	else
	{
		remLegInt = remLeg->nextInt;
	}

	if( remLegInt || (eaSize(&eaSegmentGroups) == 1))
	{
		// if the leg we're replacing has an intersection here, we only want to set the next
		// one of the legs may get orphaned

		// only one leg on this side, we can directly connect these two
		AICivilianPathLeg *newLeg = eaSegmentGroups[0]->leg;
		if (newLegsPrev)
		{
			newLeg->prev = otherLeg;
		}
		else
		{
			newLeg->next = otherLeg;
		}
		// now connect the other leg to this one
		acgReplaceLegConnection(newLeg, otherLeg, remLeg);
	}
	else if( eaSize(&eaSegmentGroups) > 1)
	{	// there is more than one leg, we'll need to make an intersection
		// depending on how this leg is connected to me
		// if there already is an intersection, add these to it
		// if there isn't create an intersection

		if (otherLeg->next == remLeg)
		{
			if (otherLeg->nextInt == NULL)
			{
				acgSplit_ConnectCreateNewACPI(otherLeg, false, eaSegmentGroups, newLegsPrev);
			}
			else if (! otherLeg->nextInt->bIsMidIntersection)
			{
				// this other leg already has a next, so we're going to have to add to it
				acgSplit_AddLegsToACPI(otherLeg->nextInt, eaSegmentGroups, newLegsPrev);
				otherLeg->next = NULL;
			}
			else
			{
				// the otherLeg->nextInt is a mid intersection.
				// pick one of the legs and just add it to the next
				AICivilianPathLeg *newLeg = eaSegmentGroups[0]->leg;
				if (newLegsPrev)
				{
					newLeg->prev = otherLeg;
				}
				else
				{
					newLeg->next = otherLeg;
				}

				otherLeg->next = newLeg;
			}
		}
		else if (otherLeg->prev == remLeg)
		{
			if (otherLeg->prevInt == NULL)
			{
				acgSplit_ConnectCreateNewACPI(otherLeg, true, eaSegmentGroups, newLegsPrev);
			}
			else if (! otherLeg->prevInt->bIsMidIntersection)
			{
				// this other leg already has a prev, so we're going to have to add to it
				acgSplit_AddLegsToACPI(otherLeg->prevInt, eaSegmentGroups, newLegsPrev);
				otherLeg->prev = NULL;
			}
			else
			{
				// the otherLeg->nextInt is a mid intersection.
				// pick one of the legs and just add it to the next
				AICivilianPathLeg *newLeg = eaSegmentGroups[0]->leg;
				if (newLegsPrev)
				{
					newLeg->prev = otherLeg;
				}
				else
				{
					newLeg->next = otherLeg;
				}

				otherLeg->prev = newLeg;
			}

		}
		else
		{
			if (otherLeg->nextInt)
			{
				acgSplit_RemLegAddLegsToACPI(otherLeg, false, eaSegmentGroups, newLegsPrev, remLeg);
			}

			if (otherLeg->prevInt)
			{
				acgSplit_RemLegAddLegsToACPI(otherLeg, true, eaSegmentGroups, newLegsPrev, remLeg);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_ConnectLegIntersections(AICivilianPathLeg *remLeg, bool bLegPrev, ACGSegmentGroup **eaSegmentGroups)
{
	AICivilianPathIntersection *acpi;

	if (eaSize(&eaSegmentGroups) == 0)
		return;

	if (bLegPrev)
	{
		acpi = remLeg->prevInt;
	}
	else
	{
		acpi = remLeg->nextInt;
	}

	if (acpi->bIsMidIntersection)
	{
		// TODO: we'll need to make multiple mid intersections if the size is > 1
		//		interim: pick any one of the new legs and make it the mid intersection
		if (eaSize(&eaSegmentGroups) >= 1)
		{
			AICivilianPathLeg *newLeg = eaSegmentGroups[0]->leg;

			PathLegIntersect *pli;
			S32 idx = acgFindPathLegInACPI(acpi, remLeg);
			devassert(idx != -1);

			pli = acpi->legIntersects[idx];
			pli->leg = newLeg;

			// in this case we need to NULL out the intersection since the MID intersection was taken care of
			if (bLegPrev)
			{
				newLeg->prevInt = acpi;
				remLeg->prevInt = NULL;
			}
			else
			{
				newLeg->nextInt = acpi;
				remLeg->nextInt = NULL;
			}
		}
	}
	else
	{
		S32 i;
		// just connect all the left neighbors to the intersection
		for (i = 0; i < eaSize(&eaSegmentGroups); i++)
		{
			AICivilianPathLeg *newLeg = eaSegmentGroups[i]->leg;
			PathLegIntersect *pli = acgPathLegIntersect_Alloc();

			pli->leg = newLeg;
			eaPush(&acpi->legIntersects, pli);

			if (bLegPrev)
			{
				newLeg->prevInt = acpi;
			}
			else
			{
				newLeg->nextInt = acpi;
			}

			if (eaSize(&eaSegmentGroups) > 1)
			{
				newLeg->leg_set = s_acgProcess.split.set_count;
			}
		}

		if (eaSize(&eaSegmentGroups) > 1)
			s_acgProcess.split.set_count++;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_GetMidIntersectionLineSeg(const AICivilianPathIntersection *acpi, Vec3 vStartPt, Vec3 vIsectPt)
{
	PathLegIntersect *isectPli;

	devassert(eaSize(&acpi->legIntersects) == 2);
	// sort out which pli is the intersection and which is the edge
	// and get the points for the intersection's line segment
	if (vec3IsZero(acpi->legIntersects[0]->intersect))
	{
		isectPli = acpi->legIntersects[1];
	}
	else
	{
		isectPli = acpi->legIntersects[0];
	}

	if (isectPli->leg->nextInt == acpi)
	{
		copyVec3(isectPli->leg->end, vStartPt);
	}
	else
	{
		devassert(isectPli->leg->prevInt == acpi);
		copyVec3(isectPli->leg->start, vStartPt);
	}

	copyVec3(isectPli->intersect, vIsectPt);
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_CreateLegs(AICivilianPathLeg *leg, ACGSegmentGroup **eaSegmentGroups, AICivilianPathLeg ***peaNewLegs, S32 lastColumnIndex, S32 lastRowIndex )
{
	S32 i;

	// first create all the legs
	for (i = 0; i < eaSize(&eaSegmentGroups); i++)
	{
		ACGSegmentGroup *group = eaSegmentGroups[i];
		Vec3	quad[4];
		AICivilianPathLeg *newLeg;

		newLeg = acgPathLeg_Alloc();
		group->leg = newLeg;

		newLeg->type = leg->type;

		acgSegment_GetQuad(leg, group, quad);
		// get the start / end point
		interpVec3(0.5f, quad[0], quad[1], newLeg->start);
		interpVec3(0.5f, quad[2], quad[3], newLeg->end);

		newLeg->len = calcLegDir(newLeg, newLeg->dir, newLeg->perp);
		newLeg->width = distance3(quad[0], quad[1]);
		newLeg->bIsForcedLeg = leg->bIsForcedLeg;
		newLeg->pchLegTag = leg->pchLegTag;
		

		if (peaNewLegs)
		{
			eaPush(peaNewLegs, newLeg);
		}
		eaPush(&s_acgProcess.legList, newLeg);
	}

	// now connect all the legs within the group to each other
	for (i = 0; i < eaSize(&eaSegmentGroups); i++)
	{
		S32 x;
		ACGSegmentGroup *group = eaSegmentGroups[i];

		acgSplit_GetGroupNeighbors(eaSegmentGroups, group, &s_acgProcess.split.eaNeighborList);

		eaClear(&s_acgProcess.split.eaLeftNeighborList);
		eaClear(&s_acgProcess.split.eaRightNeighborList);

		// split our neighbor list up into left / right lists
		for (x = 0; x < eaSize(&s_acgProcess.split.eaNeighborList); x++)
		{
			ACGSegmentGroup *neighbor = s_acgProcess.split.eaNeighborList[x];

			if (acgSegmentGroup_IsLeftNeighbhor(group, neighbor))
			{
				eaPush(&s_acgProcess.split.eaLeftNeighborList, neighbor);
			}
			else
			{
				eaPush(&s_acgProcess.split.eaRightNeighborList, neighbor);
			}
		}

		// link the neighbors together
		acgSplit_LinkToNeighbors(s_acgProcess.split.eaLeftNeighborList, group->leg, true);
		acgSplit_LinkToNeighbors(s_acgProcess.split.eaRightNeighborList, group->leg, false);
	}

	eaClear(&s_acgProcess.split.eaLeftNeighborList);
	eaClear(&s_acgProcess.split.eaRightNeighborList);

	// now connect all the legs on the outside
	// get the legs on each border so we can connect the right ones
	for (i = 0; i < eaSize(&eaSegmentGroups); i++)
	{
		ACGSegmentGroup *group = eaSegmentGroups[i];

		if (group->startColumnIdx == 0)
		{
			eaPush(&s_acgProcess.split.eaLeftNeighborList, group);
		}

		if (group->endColumnIdx == lastColumnIndex)
		{
			eaPush(&s_acgProcess.split.eaRightNeighborList, group);
		}
	}


	if (leg->prev)
	{
		acgSplit_ConnectLegs(leg, leg->prev, s_acgProcess.split.eaLeftNeighborList, true);
	}

	if (leg->next)
	{
		acgSplit_ConnectLegs(leg, leg->next, s_acgProcess.split.eaRightNeighborList, false);
	}


	if (leg->prevInt)
	{
		acgSplit_ConnectLegIntersections(leg, true, s_acgProcess.split.eaLeftNeighborList);
	}

	if (leg->nextInt)
	{
		acgSplit_ConnectLegIntersections(leg, false, s_acgProcess.split.eaRightNeighborList);
	}


	if (leg->midInts)
	{
		AICivilianPathIntersection *acpi;
		for (i = eaSize(&leg->midInts) - 1; i >= 0; i--)
		{
			Vec3 vStart, vEnd;
			AICivilianPathLeg *newMidLeg = NULL;
			AICivilianPathLeg *intLeg;
			S32 x;

			acpi = leg->midInts[i];

			acgSplit_GetMidIntersectionLineSeg(acpi, vStart, vEnd);
			{
				PathLegIntersect *intPli, *legPli;
				acgMidIntersectionGetLegAndIsectLeg(acpi, &intPli, &legPli);
				intLeg = intPli->leg;
			}
			
			
			// find which of our new legs this intersection crosses

			for (x = 0; x < eaSize(&eaSegmentGroups); x++)
			{
				Vec3 vIsectPt;
				Vec3 quad[4];
				ACGSegmentGroup *group = eaSegmentGroups[x];
				AICivilianPathLeg *newLeg = group->leg;

				acgSegment_GetQuad(leg, group, quad);

				// Note: this may be insufficient to get the best mid intersection
				//	AND it may end up going through objects...

				if (lineSegLineSeg2dIntersection(vStart, vEnd, quad[0], quad[2], vIsectPt) || 
					lineSegLineSeg2dIntersection(intLeg->start, intLeg->end, quad[0], quad[2], vIsectPt) )
				{
					// the lines intersect
					newMidLeg = newLeg;
					// the intersection point becomes the new end
					copyVec3(vIsectPt, vEnd);
				}

				if (lineSegLineSeg2dIntersection(vStart, vEnd, quad[1], quad[3], vIsectPt) || 
					lineSegLineSeg2dIntersection(intLeg->start, intLeg->end, quad[1], quad[3], vIsectPt) )
				{
					// the lines intersect
					newMidLeg = newLeg;
					// the intersection point becomes the new end
					copyVec3(vIsectPt, vEnd);
				}
			}

			if (newMidLeg)
			{
				// we found a leg that intersects, this is the new mid connection
				PathLegIntersect *pli;
				S32 idx = acgFindPathLegInACPI(acpi, leg);
				devassert(idx != -1);
				// set the new leg on the pli
				pli = acpi->legIntersects[idx];
				pli->leg = newMidLeg;

				idx = !idx; // since there are only two pli's in this intersection, get the other pli

				// get the new intersection point
				{
					Vec3 vIsectPt;
					lineSegLineSeg2dIntersection(vStart, vEnd, newMidLeg->start, newMidLeg->end, vIsectPt);

					pli = acpi->legIntersects[idx];
					copyVec3(vIsectPt, pli->intersect);
				}


				// put the mid intersection on the newleg's list
				// and then remove it from the old leg
				eaPush(&newMidLeg->midInts, acpi);
				eaRemove(&leg->midInts, i);
			}
			else
			{
				// failed to find a leg to intersect...
				s_acgProcess.split.missed_mid_connections++;
			}



		}

	}

}

// ------------------------------------------------------------------------------------------------------------------
// returns true of the leg is intact and no processing is needed for it.
static bool acgSplit_IsLegIntact(const ACGSegmentGroup **eaSegmentGroups, S32 lastColumnIndex, S32 lastRowIndex)
{
	if (eaSize(&eaSegmentGroups) == 1)
	{
		const ACGSegmentGroup *group = eaHead(&eaSegmentGroups);

		if (group->startColumnIdx == 0 && group->endColumnIdx == lastColumnIndex)
		{
			if (group->startRowIdx == 0 && group->endRowIdx == lastRowIndex)
				return true;
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void acgSplit_DebugDrawGroups(Entity *debugger, const ACGSegmentGroup **eaSegmentGroups, const AICivilianPathLeg *leg)
{
	// split debugging. send lines of the column segments
	S32 x;
	for( x = 0; x < eaSize(&eaSegmentGroups); x++)
	{
		Vec3 vQuadPoints[4];
		const ACGSegmentGroup *group = eaSegmentGroups[x];

		acgSegment_GetQuad(leg, group, vQuadPoints);
		vQuadPoints[0][1] += 1.0f;
		vQuadPoints[1][1] += 1.0f;
		vQuadPoints[2][1] += 1.0f;
		vQuadPoints[3][1] += 1.0f;

		wlAddClientLine(debugger, vQuadPoints[0], vQuadPoints[1], 0xFFFF0000);
		wlAddClientLine(debugger, vQuadPoints[1], vQuadPoints[3], 0xFF0000FF);
		wlAddClientLine(debugger, vQuadPoints[2], vQuadPoints[3], 0xFFFF0000);
		wlAddClientLine(debugger, vQuadPoints[0], vQuadPoints[2], 0xFF0000FF);
	}
}



static void acgValidateLegConnections(AICivilianPathLeg *testLeg, bool bFixErrors)
{
	AICivilianPathLeg **connectedLegs = NULL;
	S32 x;

	if (testLeg->next)
	{
		eaPush(&connectedLegs, testLeg->next);
	}
	if (testLeg->prev)
	{
		eaPush(&connectedLegs, testLeg->prev);
	}

	if (testLeg->nextInt)
	{
		AICivilianPathIntersection *acpi = testLeg->nextInt;
		for (x = 0; x < eaSize(&acpi->legIntersects); x++)
		{
			PathLegIntersect *pli = acpi->legIntersects[x];
			if (pli->leg != testLeg)
			{
				eaPush(&connectedLegs, pli->leg);
			}
		}
	}

	if (testLeg->prevInt)
	{
		AICivilianPathIntersection *acpi = testLeg->prevInt;
		for (x = 0; x < eaSize(&acpi->legIntersects); x++)
		{
			PathLegIntersect *pli = acpi->legIntersects[x];
			if (pli->leg != testLeg)
			{
				eaPush(&connectedLegs, pli->leg);
			}
		}
	}


	if (testLeg->midInts)
	{
		S32 i;
		for (i = 0; i < eaSize(&testLeg->midInts); i++)
		{
			AICivilianPathIntersection *acpi = testLeg->midInts[i];
			for (x = 0; x < eaSize(&acpi->legIntersects); x++)
			{
				PathLegIntersect *pli = acpi->legIntersects[x];
				if (pli->leg != testLeg)
				{
					eaPush(&connectedLegs, pli->leg);
				}
			}
		}
	}

	// go through all the legs
	for (x = 0; x < eaSize(&s_acgProcess.legList); x++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[x];
		S32 i;

		if (leg == testLeg)
			continue;

		if ((leg->next == testLeg) || (leg->prev == testLeg) ||
			(leg->prevInt && acgFindPathLegInACPI(leg->prevInt, testLeg) != -1) ||
			(leg->nextInt && acgFindPathLegInACPI(leg->nextInt, testLeg) != -1) )
		{
			if (! bFixErrors)
			{
				devassert(eaFind(&connectedLegs, leg) != -1);
			}
			else
			{
				if (eaFind(&connectedLegs, leg) == -1)
				{
					#if defined(ACG_DEBUGPRINT)
					printf("Fixing Bad Leg Connection at leg startPos (%.1f, %.1f, %.1f)\n", leg->start[0], leg->start[1], leg->start[2]);
					#endif

					if (leg->next == testLeg)
					{
						leg->next = NULL;
					}
					else if (leg->prev == testLeg)
					{
						leg->prev = NULL;
					}
					else if (leg->prevInt && acgFindPathLegInACPI(leg->prevInt, testLeg) != -1)
					{
						leg->prevInt = NULL;
					}
					else if (leg->nextInt && acgFindPathLegInACPI(leg->nextInt, testLeg) != -1)
					{
						leg->nextInt = NULL;
					}
				}
			}

		}
		else
		{
			for (i = 0; i < eaSize(&leg->midInts); i++)
			{
				AICivilianPathIntersection *acpi = leg->midInts[i];
				if (acgFindPathLegInACPI(acpi, testLeg) != -1)
				{
					devassert(eaFind(&connectedLegs, leg) != -1);
					break;
				}
			}
		}


	}

	eaDestroy(&connectedLegs);
}

static void acgValidateAllLegConnections(bool bFixErrors)
{
	S32 x;

	for (x = 0; x < eaSize(&s_acgProcess.legList); x++)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[x];
		acgValidateLegConnections(leg, bFixErrors);
	}
}

static void acgDestroyDegenerateGroups(ACGSegmentGroup **eaSegmentGroups)
{
	S32 x;

	for (x = eaSize(&eaSegmentGroups) - 1; x >= 0; x--)
	{
		ACGSegmentGroup *group = eaSegmentGroups[x];
		
		if (acgSegmentGroup_GetLength(group) < 2 || acgSegmentGroup_GetWidth(group) < 2)
		{
			eaRemove(&eaSegmentGroups, x);
			acgSegmentGroup_Free(group);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static bool acgSplitLegsOnObjects(int iPartitionIdx, Entity *debugger)
{
	S32 i;

	for(i = eaSize(&s_acgProcess.legList) - 1; i >= 0; i--)
	{
		AICivilianPathLeg *leg = s_acgProcess.legList[i];
		ACGColumn **eaColumns = NULL;
		ACGSegmentGroup **eaSegmentGroups = NULL;
		S32 lastColumnIdx = 0;
		S32 lastRowIdx = 0;
		devassert(!leg->deleted);

		// do not split up car legs, only pedestrian legs
		if (leg->type == EAICivilianLegType_CAR || leg->bForcedLegAsIs == true)
		{
			continue;
		}

		if(distance3SquaredXZ(leg->start, s_acgDebugPos)<SQR(3) ||
			distance3SquaredXZ(leg->end, s_acgDebugPos)<SQR(3) )
			printf("");

		if (0)
		{
			if(distance3SquaredXZ(leg->start, s_acgDebugPos)>SQR(3) &&
				distance3SquaredXZ(leg->end, s_acgDebugPos)>SQR(3) )
				continue;
		}

		acgGenerateLegColumns(iPartitionIdx, debugger, leg, &eaColumns, &lastColumnIdx, &lastRowIdx);

		acgCollapseColumnsIntoSegmentGroups(leg, eaColumns, &eaSegmentGroups);

		eaDestroyEx(&eaColumns, acgFreeACGColumn);

		if (acgSplit_IsLegIntact(eaSegmentGroups, lastColumnIdx, lastRowIdx))
		{
			if (acg_d_split)
				acgSplit_DebugDrawGroups(debugger, eaSegmentGroups, leg);

			// this leg was perfect, move on
			eaDestroyEx(&eaSegmentGroups, acgSegmentGroup_Free);
			continue;
		}


		if (1)
		{
			acgSplit_GrowGroups(eaSegmentGroups);
		}

		if (1)
		{
			acgSplit_ClipGroups(&eaSegmentGroups, false);
			acgSplit_MergeGroups(eaSegmentGroups);
			acgSplit_RemoveOrphansAndDeadEnds(eaSegmentGroups);
		}

		if (1)
		{
			acgSplit_GrowGroups(eaSegmentGroups);
		}

		if (1)
		{
			acgSplit_ClipGroups(&eaSegmentGroups, true);
			acgSplit_MergeGroups(eaSegmentGroups);
			acgSplit_RemoveOrphansAndDeadEnds(eaSegmentGroups);
		}

		acgDestroyDegenerateGroups(eaSegmentGroups);

		// split debugging. send lines of the column segments
		if (acg_d_split)
		{
			acgSplit_DebugDrawGroups(debugger, eaSegmentGroups, leg);
		}


		// if there are no segment groups left, destroy the leg and move on
		if (eaSize(&eaSegmentGroups)== 0)
		{
			// eaRemove(&s_acgProcess.legList, i);
			leg->deleted = s_acgProcess.state;
			leg->deleteReason = "Split resulted in whole leg being destroyed.";
			
			if (leg->bIsForcedLeg)
			{
				Vec3 vMid;
				interpVec3(0.5f, leg->start, leg->end, vMid);
				acgLegGenReport_AddLocation(vMid, leg->deleteReason, 
											leg->bIsForcedLeg, 
											MAX(leg->len, leg->width));
			}
			
			
			acgDestroyLeg(&leg, false);
		}
		else
		{
			acgSplit_CreateLegs(leg, eaSegmentGroups, NULL, lastColumnIdx, lastRowIdx);
#if defined(LEG_DESTROY_PARANOID)
			{
				AICivilianPathLeg **eaNewLegs = NULL;
				S32 x;

				for(x = 0; x < eaSize(&eaNewLegs); x++)
				{
					acgValidateLegConnections(eaNewLegs[x], false);
				}
				eaDestroy(&eaNewLegs);
			}
#endif

			eaRemove(&s_acgProcess.legList, i);
			acgDestroyLeg(&leg, true);
		}



		eaDestroyEx(&eaSegmentGroups, acgSegmentGroup_Free);
	}

	acgProcess_RemoveDeletedLegs();

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
void acgLegGenerateion_DumpDeadEndRoads(AICivilianRuntimePathInfo *pPathInfo, AICivRegenReport *report)
{
	S32 i;

	for (i = 0; i < eaSize(&pPathInfo->legs[1]); i++)
	{
		AICivilianPathLeg *leg = pPathInfo->legs[1][i];
		
		if (! leg->prev && !leg->prevInt)
		{
			acgReport_AddLocation(report, leg->start, 
									"Road: Dead-End Road", false,
									MAX(leg->width,leg->len));
		}

		if (! leg->next && !leg->nextInt)
		{
			acgReport_AddLocation(report, leg->end, 
									"Road: Dead-End Road", false,
									MAX(leg->width,leg->len));
		}
	}
	
}

// ------------------------------------------------------------------------------------------------------------------
void acgReport_AddLocation(AICivRegenReport *pReport, 
							const Vec3 vPos, 
							const char *pchProblemDescription,
							S32 bIsAnError,
							F32 fProblemAreaSize)
{
	AICivProblemLocation *pProblemLoc;

	if (pReport)
	{
		pchProblemDescription = allocAddString(pchProblemDescription);
		
		pProblemLoc = calloc(1, sizeof(AICivProblemLocation));
		if (pProblemLoc)
		{
			copyVec3(vPos, pProblemLoc->vPos);
			pProblemLoc->pchReason = pchProblemDescription;
			pProblemLoc->bError = bIsAnError;
			pProblemLoc->fProblemAreaSize = fProblemAreaSize;
			eaPush(&pReport->eaProblemLocs, pProblemLoc);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void acgLegGenReport_AddLocation(const Vec3 vPos, 
										const char *pchProblemDescription, 
										S32 bIsAnError,
										F32 fProblemAreaSize)
{
	if (s_pRegenReport)
	{
		acgReport_AddLocation(s_pRegenReport, vPos, pchProblemDescription, bIsAnError, fProblemAreaSize);
	}
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSetDebugPosExplicit(Vec3 pos)
{
	acg_d_pos = 1;
	copyVec3(pos, s_acgDebugPos);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSetDebugPos(Entity *entity)
{
	if (!entity)
		return;

	acg_d_pos = 1;
	entGetPos(entity, s_acgDebugPos);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSetNodeGenPos(Vec3 pos)
{
	acg_d_nodeGenerator = 1;
	copyVec3(pos, s_acgNodeGeneratorPos);
}


// -------------------------------------------------------------------------
AUTO_COMMAND;
void sendCivLegs(Entity *e)
{
	if (!e) e = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	acgSendLegs(e);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSendPathPoints(Entity *e)
{
	if (!e) e = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	_acgSendPathPoints(e);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSendCarCurvePoints(Entity *e)
{
	Entity *ent = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	_acgSendCarCurvePoints(ent);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void sendCivIntersectionBounds(Entity *e)
{
	Entity *ent = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	acgSendIntersectionBounds(ent);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void sendCivIntersectionTypes(Entity *e)
{
	Entity *ent = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	aiCivSendIntersectionTypes(ent);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void civRegen(Entity *pEnt)
{
	acgMapUnload();
	acgMapLoad(entGetPartitionIdx(pEnt));
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgRegenForcedLegs(Entity *entity)
{
	if (entity)
	{
		aiCivDebugRef = entity->myRef;
	}

	
	g_civSharedState.bIsDisabled = 0;
	
	// force the following flags on, but save their state so after the regeneration we can reset them
	s_enableCivilianGeneration = 1;
	s_ignoreUptodate = 1;
	s_acg_regenForcedLegsOnly = 1;

	acgMapUnload();
	acgMapLoad(entGetPartitionIdx(entity));

	// if someone is using these commands we want to set the number of civilians to 0
	if (g_civSharedState.pMapDef)
	{
		aiCivilian_SetCountForEachPartition(EAICivilianType_PERSON, 0);
		aiCivilian_SetCountForEachPartition(EAICivilianType_CAR, 0);
	}

	s_acg_regenForcedLegsOnly = 0;
}

void civPartitionGetGridCoordsFromPos(Vec3 vPos, S32 *x, S32 *z);

// -------------------------------------------------------------------------
void acgRegenLocalLegsInteral(Entity *e, AICivRegenOptions *options)
{
	aiCivDebugRef = e->myRef;

	g_civSharedState.bIsDisabled = 0;

	s_enableCivilianGeneration = 1;
	s_ignoreUptodate = 1;
	s_acgSkip_GroundCoplanarCheck = 1;
	s_acg_regenForcedLegsOnly = options->bVolumeLegsOnly;

	s_acgSkip_LegSplit = options->bSkipLegSplit;

	// set the local regeneration bounding box
	{
		Vec3 vBoundingHalfExtents;

		s_acg_d_culling.do_culling = 1;
				
		setVec3(vBoundingHalfExtents, 
						options->fAreaRegen, 
						options->fAreaRegen, 
						options->fAreaRegen);

		subVec3(options->vRegenPos, vBoundingHalfExtents, s_acg_d_culling.boxMin);
		addVec3(options->vRegenPos, vBoundingHalfExtents, s_acg_d_culling.boxMax);
	}

	// 
	{
		acg_d_nodeGenerator = 1;
		copyVec3(options->vRegenPos, s_acgNodeGeneratorPos);
	}

	acgMapUnload();
	acgMapLoad(entGetPartitionIdx(e));

	// if someone is using these commands we want to set the number of civilians to 0
	if (g_civSharedState.pMapDef)
	{
		aiCivilian_SetCountForEachPartition(EAICivilianType_PERSON, 0);
		aiCivilian_SetCountForEachPartition(EAICivilianType_CAR, 0);
	}

	//
	s_acg_regenForcedLegsOnly = false;
	acg_d_nodeGenerator = false;
	s_acgSkip_GroundCoplanarCheck = false;
	s_acg_d_culling.do_culling = false;
	s_acgSkip_LegSplit = false;

	if (options->bPostPopulate && g_civSharedState.pMapDef)
	{
		// for each type
		AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
		AICivilianRuntimePathInfo *pPathInfo;
		AICivilianPathLeg **eaLegs;
		if (!pPartition)
			return;

		pPathInfo = &pPartition->pathInfo;

		eaLegs = pPathInfo->legs[EAICivilianType_PERSON];

		// do peds first
		{
			F32 fLegArea = 0.f;
			const F32 fPedsPerArea = 1.f/(25.f*25.f);
			S32 desiredNumPeds = 0;

			FOR_EACH_IN_EARRAY(eaLegs, AICivilianPathLeg, pLeg)
			{
				fLegArea += pLeg->width * pLeg->len;
			}
			FOR_EACH_END

				
			desiredNumPeds = ceil(fLegArea * fPedsPerArea);
			MIN1(desiredNumPeds, 1500);
			aiCivilian_SetCountForEachPartition(EAICivilianType_PERSON, desiredNumPeds);
		}

		eaLegs = pPathInfo->legs[EAICivilianType_CAR];

		// do cars next
		{
			F32 fLanesLen = 0.f;
			const F32 fCarPerLane = 1.f/(40.f);
			S32 desiredNumCars = 0;

			FOR_EACH_IN_EARRAY(eaLegs, AICivilianPathLeg, pLeg)
			{
				S32 numLanes = (pLeg->max_lanes != 0) ? (pLeg->max_lanes * 2) : 1;
				fLanesLen += numLanes * pLeg->len;
			}
			FOR_EACH_END


			desiredNumCars = ceil(fLanesLen * fCarPerLane);
			MIN1(desiredNumCars, 500);
			aiCivilian_SetCountForEachPartition(EAICivilianType_CAR, desiredNumCars);
		}

		
	}
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgRegenLocalLegs(Entity *entity)
{
	AICivRegenOptions	options = {0};
	
	if (! entity)
		return;
	
	
	options.bPostPopulate = false;
	options.bSkipLegSplit = true;
	options.bVolumeLegsOnly = false;
	entGetPos(entity, options.vRegenPos);
	options.fAreaRegen = s_acg_fLocalRegenHalfExtent;

	acgRegenLocalLegsInteral(entity, &options);
	sendCivLegs(entity);
}

// -------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(AI) ACMD_SERVERCMD ACMD_PRIVATE;
void acgRegenLocalLegsEx(Entity *entity, AICivRegenOptions *regenOptions)
{
	AICivRegenReport report = {0};

	if (! entity)
		return;

	s_pRegenReport = &report;
	acgRegenLocalLegsInteral(entity, regenOptions);
	s_pRegenReport = NULL;

	sendCivLegs(entity);

	
	if (g_civSharedState.pCivPathInfo)
	{
		report.totalLegsCreated = eaSize(&g_civSharedState.pCivPathInfo->legs);
	}
		
	ClientCmd_gclCivEditor_NotifyComplete(entity, &report);

	StructDeInit(parse_AICivRegenReport, &report);


}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgFindLeg(Vec3 pos)
{
	AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
	AICivilianRuntimePathInfo *pPathInfo;
	int i;
	if (!pPartition)
		return;

	pPathInfo = &pPartition->pathInfo;
	
	for(i=0; i<ARRAY_SIZE(pPathInfo->legs); i++)
	{
		int j;
		for(j=0; j<eaSize(&pPathInfo->legs[i]); j++)
		{
			AICivilianPathLeg *leg = pPathInfo->legs[i][j];
			if(PointLineDistSquared(pos, leg->start, leg->dir, leg->len, NULL)<SQR(4))
				printf("");
		}
	}
	/*
	for(i=0; i<eaSize(&g_aiCivilianState.path_info->deletedLegs); i++)
	{
		AICivilianPathLeg *leg = g_aiCivilianState.path_info->deletedLegs[i];
		if(PointLineDistSquared(pos, leg->start, leg->dir, leg->len, NULL)<SQR(4))
			printf("");
	}
	*/
}

// -------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(civPartial);
void acgPartialProcess(int phase)
{
	g_civSharedState.partialLoad = phase;
}


// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgDumpLegmap(int set)
{
	int i;
	char filename[MAX_PATH];

	if (set == 0)
	{
		g_eHeatMapSet = EAICivilianLegType_PERSON;
	}
	else
	{
		g_eHeatMapSet = EAICivilianLegType_CAR;
	}

	// create the filename
	i = 0;
	do{
		const char *mapfilename = zmapGetFilename(NULL);
		sprintf(filename, "c:\\temp\\%s_%d_%s.jpg",
			(g_eHeatMapSet==EAICivilianLegType_PERSON) ? "pedestrianLegmap" : "carLegmap", i,
			mapfilename ? getFileNameConst(mapfilename) : "" );
		i++;
	} while(fileExists(filename));

	{
		Vec3 min, max;
		char *pErrorString = NULL;
		S32 unitsPerPixel = 1;
		const int PEN_SIZE = 1;
		const int YELLOW_CUTOFF = 10;
		const int RED_CUTOFF = 20;

		aiCivHeatmapGetBoundingSizeAndUnitsPerPixel(min, max, &unitsPerPixel, true);

		gslWriteJpegHeatMapEx(filename, "LegMap", min, max, unitsPerPixel, PEN_SIZE, YELLOW_CUTOFF, RED_CUTOFF, 300, &pErrorString);
		estrDestroy(&pErrorString);

		{
			const char *infofilename = zmapGetFilename(NULL);
			FILE *pFile;

			sprintf(filename, "c:\\temp\\%s_%s.txt",
						(g_eHeatMapSet==EAICivilianLegType_PERSON) ? "pedestrianLegmap" : "carLegmap", 
						infofilename ? getFileNameConst(infofilename) : "" );
			
			pFile = fopen(filename, "wt");
			if (pFile)
			{
				AICivilianPartitionState* pPartition = aiCivilian_GetAnyValidPartition();
				
				aiCivilian_WorldLegGridGetBoundingMin(pPartition->pWorldLegGrid, min);

				fprintf(pFile, "Min Coords (X,Z): (%.2f, %.2f)\n", min[0], min[2]);
				fprintf(pFile, "Units Per Pixel: %d\n", unitsPerPixel);
				fclose(pFile);
			}
			
		}
	}

}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgTestGroundMaterial(Entity *eClient)
{
	if (eClient)
	{
		Vec3 vPos;
		entGetPos(eClient, vPos);
		{
			Vec3 world_pos_up, world_pos_down;
			WorldCollCollideResults results = {0};
			int type;
			const char *materialTypeStr = NULL;
			int iPartitionIdx = entGetPartitionIdx(eClient);

			copyVec3(vPos, world_pos_up);
			copyVec3(vPos, world_pos_down);
			world_pos_up[1] += CGS_GRID_BLOCK_CAST_DISTUP;
			world_pos_down[1] -= CGS_GRID_BLOCK_CAST_DISTDOWN;

			wcRayCollide(worldGetActiveColl(iPartitionIdx), world_pos_up, world_pos_down, WC_QUERY_BITS_AI_CIV, &results);

			type = aiCivGenClassifyResults(&results);

			if (type == EAICivilianLegType_PERSON)
			{
				materialTypeStr = "Pedestrian";
			}
			else if (type == EAICivilianLegType_CAR)
			{
				materialTypeStr = "Car";
			}
			else
			{
				materialTypeStr = "Invalid";
			}

			printf("\nGround Material Type: %s\n", materialTypeStr);
			if (results.wco)
			{
				WorldCollisionEntry *entry;

				if (wcoGetUserPointer(results.wco, entryCollObjectMsgHandler, &entry))
				{

					Model *model = SAFE_MEMBER(entry, model);
					Material* mat = modelGetCollisionMaterialByTri(model, results.tri.index);
					//MaterialData *matData = SAFE_MEMBER(mat, material_data);
					//const char *materialName = SAFE_MEMBER(matData, material_name);
					const char *materialName = SAFE_MEMBER(mat, material_name);

					if (materialName)
					{
						printf("Material Name: %s\n", materialName);
					}
					else
					{
						printf("Unknown Material Name\n");
					}
				}
			}

		}
	}
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSendEdges(Entity *e, int deletedEdges)
{
	Entity *ent = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	acgDebugSendEdges(ent, deletedEdges);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgSendLines(Entity *e)
{
	Entity *ent = entFromEntityRef(entGetPartitionIdx(e), aiCivDebugRef);
	acgDebugSendLines(ent);
}

// -------------------------------------------------------------------------
// batch setup 
AUTO_COMMAND;
void acgSetupLegGenDebug()
{
	g_civSharedState.partialLoad = 2;
	s_acg_forceRegenPartial = 1;
	s_acg_forceLoadPartial = 0;
	s_acg_fLocalRegenHalfExtent = 750.f;
	
	//acg_d_pairs = 1;
	//acg_d_lines = 1;

	//acgPartialProcess(1);
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgForceRegenPartial()
{
	s_acg_forceRegenPartial = true;
	s_acg_forceLoadPartial = false;
}

// -------------------------------------------------------------------------
AUTO_COMMAND;
void acgForceLoadPartial()
{
	s_acg_forceRegenPartial = false;
	s_acg_forceLoadPartial = true;
}

AUTO_COMMAND;
void aiCivLegReport(Entity *e)
{
	int i;
	int coplanarCount = 0, count = 0;
	char *estrMsg = NULL;

	count = eaSize(&g_civSharedState.pCivPathInfo->legs);
	for(i=0; i<count; i++)
	{
		AICivilianPathLeg *leg = g_civSharedState.pCivPathInfo->legs[i];

		if(leg->bIsGroundCoplanar)
			coplanarCount++;
	}

	estrPrintf(&estrMsg, "Coplanar: %d/%d - %.2f", coplanarCount, count, 1.0*coplanarCount/count);
	ClientCmd_clientConPrint(e, estrMsg);
}

AUTO_COMMAND;
void acgWorldStatus(Entity *e)
{
	int blockcount = 0;
	int partition = 1;

	if(e)
		partition = entGetPartitionIdx(e);
	acgGetNodeBlocksMinMax(partition);

	blockcount =	(s_acgNodeBlocks.grid_max[0]-s_acgNodeBlocks.grid_min[0]+1) *
					(s_acgNodeBlocks.grid_max[1]-s_acgNodeBlocks.grid_min[1]+1);

	printf(""LOC_PRINTF_STR" - "LOC_PRINTF_STR": %d\n", vecParamsXYZ(s_acgNodeBlocks.world_min), 
														vecParamsXYZ(s_acgNodeBlocks.world_max), 
														blockcount);
}


#include "aiCivLegGeneration_c_ast.c"
#include "aiCivilianPrivate_h_ast.c"
