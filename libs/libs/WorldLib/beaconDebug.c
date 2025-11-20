#include "beaconClientServerPrivate.h"
#include "simpleparser.h"
#include "WorldLib.h"
#include "netpacketutil.h"
#include "Octree.h"
#include "SparseGrid.h"
#include "mathutil.h"
#include "WorldGrid.h"
#include "PhysicsSDK.h"

#include "beaconDebug_h_ast.h"
#include "beaconClientServerPrivate_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef enum BeaconDebugMessage{
	BDM_INIT = 0xcccccccc,
	BDM_INIT_CHK,
	BDM_BCNS,
	BDM_BCNS_CHK,
	BDM_DYN,
	BDM_DYN_CHK,
	BDM_CONNS,
	BDM_CONNS_CHK,
	BDM_DEL,
	BDM_DEL_CHK,
	BDM_HIDE,
	BDM_HIDE_CHK,
	BDM_SUBBLK,
	BDM_SUBBLK_CHK,
	BDM_GALAXY,
	BDM_GALAXY_CHK,
	BDM_GALCRT,
	BDM_GALCRT_CHK,
	BDM_GALDES,
	BDM_GALDES_CHK,
	BDM_GALCHG,
	BDM_GALCHG_CHK,
	BDM_BLKCRT,
	BDM_BLKCRT_CHK,
	BDM_BLKDES,
	BDM_BLKDES_CHK,
	BDM_BLKCHG,
	BDM_BLKCHG_CHK,
	BDM_CLUST,
	BDM_CLUST_CHK,
	BDM_CLSTCRT,
	BDM_CLSTCRT_CHK,
	BDM_CLSTDES,
	BDM_CLSTDES_CHK,
	BDM_CLSTCHG,
	BDM_CLSTCHG_CHK,
} BeaconDebugMessage;

BeaconDebugState gbcnDebugState;

ParseTable* debugptis[] = {parse_ASTColor, parse_ASTPoint, parse_ASTLine, parse_ASTTri};

#if !PLATFORM_CONSOLE

typedef struct DebugConnection DebugConnection;
typedef struct DebugBeacon
{
	Vec3 pos;
	F32 ceilDist;
	DebugConnection** gConns;
	DebugConnection** rConns;

	F32 dijkstra;
	struct DebugBeacon *dbcn;

	SparseGridEntry *sgentry;

	U32 blockId;
	int galaxyCount;
	U32 galaxyIds[MAX_BEACON_GALAXY_GROUP_COUNT];
	U32 clusterId;

	U32 received : 1;
} DebugBeacon;

typedef struct DebugConnection
{
	DebugBeacon *src;
	DebugBeacon *dest;
	F32 minH;
	F32 maxH;
	F32 dijkstra;
	U32 bidir : 1;
	U32 cluster : 1;
	U32 disabled : 1;
	U32 destroyed : 1;
} DebugConnection;

typedef struct DebugBeaconBlockConnection DebugBeaconBlockConnection;
typedef struct DebugBeaconBlock
{
	int id;
	Vec3 pos;
	int galaxy;

	int *beacons;

	DebugBeaconBlockConnection **gConns;
	DebugBeaconBlockConnection **rConns;

	U32 received : 1;
} DebugBeaconBlock;

typedef struct DebugBeaconGalaxy
{
	int id;
	int parent;

	int *blocks;

	DebugBeaconBlockConnection **gConns;
	DebugBeaconBlockConnection **rConns;

	U32 color;

	U32 received : 1;
	U32 valid : 1;
} DebugBeaconGalaxy;

typedef struct DebugBeaconBlockConnection
{
	int src;
	int dst;

	U32 minH;
	U32 maxH;
	U32 minJumpH;

	int connCount;
	int blockCount;

	U32 bidir : 1;
} DebugBeaconBlockConnection;

typedef struct DebugBeaconCluster
{
	int id;

	int* galaxies;

	DebugConnection **conns;

	U32 received : 1;
	U32 valid : 1;
} DebugBeaconCluster;

DebugBeacon** gDebugBeacons;
DebugConnection** gDebugConnections;
DebugBeaconBlock** gDebugBeaconBlocks;
DebugBeaconGalaxy** gDebugGalaxies[MAX_BEACON_GALAXY_GROUP_COUNT];
DebugBeaconBlockConnection** gDebugBeaconBlockConnections;
Octree *gDebugBeaconOctree = NULL;
SparseGrid *gDebugBeaconSG = NULL;

BcnDebugger **bcnDebuggers;

#define HILITE(x,y,z,color) if(0){				\
	pktSendBitsPack(pak, 2, 0);					\
	pktSendF32(pak,x);							\
	pktSendF32(pak,y);							\
	pktSendF32(pak,z);							\
	pktSendBits(pak,32,color);					\
}

void sendLine(Packet* pak, F32 x1, F32 y1, F32 z1, F32 x2, F32 y2, F32 z2, U32 colorARGB1, U32 colorARGB2){
	pktSendBitsPack(pak, 2, 1);
	pktSendF32(pak,x1);
	pktSendF32(pak,y1);
	pktSendF32(pak,z1);
	pktSendF32(pak,x2);
	pktSendF32(pak,y2);
	pktSendF32(pak,z2);
	pktSendBits(pak,32,colorARGB1);
	if(colorARGB2 && colorARGB1 != colorARGB2){
		pktSendBits(pak,1,1);
		pktSendBits(pak,32,colorARGB2);
	}else{
		pktSendBits(pak,1,0);
	}
}

#define SEND_LINE(x1,y1,z1,x2,y2,z2,colorARGB1){			\
	sendLine(pak, x1, y1, z1, x2, y2, z2, colorARGB1, 0);	\
}

#define SEND_LINE_PT_PT(pt1,pt2,color)		SEND_LINE(pt1[0],pt1[1],pt1[2],pt2[0],pt2[1],pt2[2],color)
#define SEND_LINE_PT_XYZ(pt,x,y,z,color)	SEND_LINE(pt[0],pt[1],pt[2],x,y,z,color)
#define SEND_LINE_XYZ_PT(x,y,z,pt,color)	SEND_LINE(x,y,z,pt[0],pt[1],pt[2],color)

#define SEND_LINE_2(x1,y1,z1,x2,y2,z2,colorARGB1,colorARGB2){		\
	sendLine(pak, x1, y1, z1, x2, y2, z2, colorARGB1, colorARGB2);	\
}

#define SEND_LINE_PT_PT_2(pt1,pt2,color,color2)		SEND_LINE_2(pt1[0],pt1[1],pt1[2],pt2[0],pt2[1],pt2[2],color,color2)
#define SEND_LINE_PT_XYZ_2(pt,x,y,z,color,color2)	SEND_LINE_2(pt[0],pt[1],pt[2],x,y,z,color,color2)
#define SEND_LINE_XYZ_PT_2(x,y,z,pt,color,color2)	SEND_LINE_2(x,y,z,pt[0],pt[1],pt[2],color,color2)

#define SEND_BEACON(beacon,color){				\
	pktSendBitsPack(pak, 2, 2);					\
	pktSendF32(pak,vecX(beacon->pos));				\
	pktSendF32(pak,vecY(beacon->pos));				\
	pktSendF32(pak,vecZ(beacon->pos));				\
	pktSendBits(pak,32,color);				\
}

static Packet* createBeaconDebugPacket(void){
	return NULL;
}

static void BeaconCmdShowConnections(int useGroundConns, Beacon* b, Beacon* parent, int level, int skip, Packet* pak)
{

}

static Beacon* findNearestBeacon(Array* beacons, const Vec3 pos){
	Beacon* best = NULL;
	F32 bestDist = FLT_MAX;
	S32 i;

	for(i = 0; i < beacons->size; i++){
		Beacon* b = beacons->storage[i];
		F32 dist = distance3Squared(b->pos, pos);
		
		if(!best || dist < bestDist){
			best = b;
			bestDist = dist;
		}
	}
	
	return best;
}

void beaconPrintDebugInfo(void){
	U32 getAllocatedBeaconConnectionCount(void);
	
	char blockMemoryInfo[1000];
	int subBlockCount = 0;
	int beaconGroundConnCount = 0;
	int beaconRaisedConnCount = 0;
	int blockConnCount = 0;
	int i;
	int galaxyConnCount = 0;
	int clusterConnCount = 0;
	int trafficConnCount = 0;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
	
	// Count combat beacon conns.
	for(i = 0; i < combatBeaconArray.size; i++){
		Beacon* beacon = combatBeaconArray.storage[i];
		beaconGroundConnCount += beacon->gbConns.size;
		beaconRaisedConnCount += beacon->rbConns.size;
	}

	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
		BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		int j;

		subBlockCount += gridBlock->subBlockArray.size;

		//printf("sub-blocks: %d\n", gridBlock->subBlockArray.size);

		for(j = 0; j < gridBlock->subBlockArray.size; j++){
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];

			blockConnCount += subBlock->gbbConns.size + subBlock->rbbConns.size;

			//printf(	"  pos: (%f, %f, %f), beacons: %d, conns: %d+%d\n",
			//		vecParamsXYZ(subBlock->pos),
			//		subBlock->beaconArray.size,
			//		subBlock->gbbConns.size,
			//		subBlock->rbbConns.size);
		}
	}
	
	for(i = 0; i < partition->combatBeaconGalaxyArray[0].size; i++){
		BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[0].storage[i];
		
		galaxyConnCount += galaxy->gbbConns.size + galaxy->rbbConns.size;
	}

	for(i = 0; i < partition->combatBeaconClusterArray.size; i++){
		BeaconBlock* cluster = partition->combatBeaconClusterArray.storage[i];
		
		clusterConnCount += cluster->gbbConns.size;
	}
	
	printf(	"\n"
			"\n"
			"\n"
			"----------------------------------------------------------------\n"
			"-- Utterly Important Beacon Info -------------------------------\n"
			"----------------------------------------------------------------\n"
			"  File Version: %d\n"
			"  Beacons:      %d\n"
			"  Beacon Conns: %d + %d = %d\n"
			"  Total Conns:  %d\n"
			"  Grid Blocks:  %d (HT size: %d)\n"
			"  Sub Blocks:   %d\n"
			"  Sub Conns:    %d\n"
			"  Galaxies:     %d (%d conns)\n"
			"  Clusters:     %d (%d conns)\n"
			"  ChunkMemory:  %s\n",
			beaconGetReadFileVersion(),
			combatBeaconArray.size,
			beaconGroundConnCount,
			beaconRaisedConnCount,
			beaconGroundConnCount + beaconRaisedConnCount,
			getAllocatedBeaconConnectionCount(),
			partition->combatBeaconGridBlockArray.size,
			0, //stashGetMaxSize(combatBeaconGridBlockTable),
			subBlockCount,
			blockConnCount,
			partition->combatBeaconGalaxyArray[0].size,
			galaxyConnCount,
			partition->combatBeaconClusterArray.size,
			clusterConnCount,
			blockMemoryInfo);
			
	printf("\nCluster Galaxies (Blocks+Beacons): ");
	
	for(i = 0; i < partition->combatBeaconClusterArray.size; i++){
		BeaconBlock* cluster = partition->combatBeaconClusterArray.storage[i];
		int j;
		int beaconCount = 0;
		int blockCount = 0;
		
		for(j = 0; j < cluster->subBlockArray.size; j++){
			BeaconBlock* galaxy = cluster->subBlockArray.storage[j];
			int k;
			
			blockCount += galaxy->subBlockArray.size;
			
			for(k = 0; k < galaxy->subBlockArray.size; k++){
				BeaconBlock* subBlock = galaxy->subBlockArray.storage[k];
				
				beaconCount += subBlock->beaconArray.size;
			}			
		}
		
		printf(	"%d(%d+%d),",
				cluster->subBlockArray.size,
				blockCount,
				beaconCount);
	}

	printf("\nGalaxy sizes:");
	for(i = 0; i < beacon_galaxy_group_count; i++)
		printf(" %d", partition->combatBeaconGalaxyArray[i].size);
	
	printf(	"\n"
			"\n"
			"----------------------------------------------------------------\n"
			"\n"
			"\n");
}

static StashTable varTable;

int beaconGetDebugVar(char* var){
	int iResult;
	if (varTable && stashFindInt(varTable, var, &iResult))
		return iResult;
		
	return 0;
}

int beaconSetDebugVar(char* var, int value){
	StashElement element;
	
	if(!varTable){
		varTable = stashTableCreateWithStringKeys(100, StashDeepCopyKeys_NeverRelease);
	}
	
	if(stashFindElement(varTable, var, &element)){
		stashElementSetInt(element, value);
	}else{
		stashAddInt(varTable, var, value, false);
	}
	
	return value;
}

BeaconProcessDebugState* beaconGetProcessDebugState(void)
{
	if(beaconIsClient())
		return beaconClientGetProcessDebugState();
	else
		return beaconServerGetProcessDebugState();
}

void beaconSendLine(Packet* pak, F32 x1, F32 y1, F32 z1, F32 x2, F32 y2, F32 z2, U32 colorARGB1, U32 colorARGB2)
{
	if(pak)
	{
		ASTLine line = {0};
		BeaconProcessDebugState *debug_state = beaconGetProcessDebugState();

		setVec3(line.p1, x1, y1, z1);
		setVec3(line.p2, x2, y2, z2);
		line.c = colorARGB1;
		if(debug_state && debug_state->debug_dist>0 && 
		   distance3(line.p1, debug_state->debug_pos)>debug_state->debug_dist &&
		   distance3(line.p2, debug_state->debug_pos)>debug_state->debug_dist)
		{
			return;
		}
#ifdef BCNDBG_VERIFY_PKT
		pktSendBits(pak, 16, 0xDEAD);
#endif
		pktSendBits(pak, 32, 2);
		pktSendStruct(pak, &line, parse_ASTLine);
#ifdef BCNDBG_VERIFY_PKT
		pktSendBits(pak, 16, 0xFEED);
#endif
	}		
}

void beaconSendPoint(Packet *pak, F32 x, F32 y, F32 z, U32 colorARGB)
{
	if(pak) 
	{											
		ASTPoint point;				
		BeaconProcessDebugState *debug_state = beaconGetProcessDebugState();
		setVec3(point.p, x, y, z);							
		point.c = colorARGB;	
		if(debug_state && debug_state->debug_dist>0 && 
			distance3(point.p, debug_state->debug_pos)>debug_state->debug_dist)
		{
			return;
		}
#ifdef BCNDBG_VERIFY_PKT
		pktSendBits(pak, 16, 0xDEAD);
#endif
		pktSendBits(pak, 32, 1);						
		pktSendStruct(pak, &point, parse_ASTPoint);		
#ifdef BCNDBG_VERIFY_PKT
		pktSendBits(pak, 16, 0xFEED);
#endif
	}
}

static void beaconDebugDrawLine(DebugBeacon *src, DebugBeacon *dst, int argb1, int argb2)
{
	wlDrawLine3D_2(src->pos, argb1, dst->pos, argb2);
}

void beaconDijkstraHelper(DebugBeacon *bcn)
{
	int i;
	DebugBeacon **temp = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(bcn->dijkstra==0)
	{
		eaClear(&temp);
	}

	for(i=0; i<eaSize(&bcn->gConns); i++)
	{
		DebugConnection *conn = bcn->gConns[i];
		DebugBeacon *dest = conn->dest;
		F32 dist;

		if(!conn || !conn->dest)
		{
			continue;  // Streaming this in
		}

		if(conn->dijkstra<0)
		{
			conn->dijkstra = distance3(conn->src->pos, conn->dest->pos);
		}
		dist = bcn->dijkstra + conn->dijkstra;

		if(dist>gbcnDebugState.dijkstraDist)
		{
			continue;
		}

		if(dest->dijkstra<0 || dist<dest->dijkstra)
		{
			dest->dijkstra = dist;
			dest->dbcn = bcn;
			eaPush(&temp, dest);
		}
	}

	for(i=0; i<eaSize(&temp); i++)
	{
		DebugBeacon *t = temp[i];

		beaconDijkstraHelper(t);
	}

	eaDestroy(&temp);

	PERFINFO_AUTO_STOP();
}

void beaconDrawDijkstraHelper(DebugBeacon *bcn, BeaconDrawLineFunc func, void* userdata)
{
	int i;
	int c1, c2;
	F32 r1, r2;
	static colors[] = {0xFF0000, 0x00FF00};//, 0xFFFFFF00, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00};

	if(bcn->dijkstra < 0)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	r1 = 1-bcn->dijkstra/gbcnDebugState.dijkstraDist;
	c1 = (interpInt(r1, colors[0], colors[1]) & 0x00FFFF00) | 0xFF000000;
	
	for(i=0; i<eaSize(&bcn->gConns); i++)
	{
		DebugConnection *conn = bcn->gConns[i];
		DebugBeacon *dest;

		if(!conn || !conn->dest)
		{
			continue;  // Streaming this in
		}

		dest = conn->dest;
		r2 = 1-dest->dijkstra/gbcnDebugState.dijkstraDist;
		c2 = (interpInt(r2, colors[0], colors[1]) & 0x00FFFF00) | 0xFF000000;
		
		if(bcn == dest->dbcn && bcn->dijkstra < dest->dijkstra)
		{	
			func(bcn->pos, c1, dest->pos, c2, userdata);

			beaconDrawDijkstraHelper(dest, func, userdata);
		}
		else
		{
			//beaconDebugDrawLine(bcn, dest, colors[0]*r1, colors[0]*r2);
		}
	}

	PERFINFO_AUTO_STOP();
}

//typedef int (*OctreeSphereVisCallback)(void *node, int node_type, const Vec3 scenter, F32 sradius, void *user_data);
U32 beaconDebugBeaconVisible(void *node, int node_type, const Vec3 scenter, F32 sradius, void *user_data)
{
	return 1;
}

void beaconDebugIDColorReset(void)
{
	if(gbcnDebugState.blockColors == NULL)
		gbcnDebugState.blockColors = stashTableCreateInt(10);

	stashTableClear(gbcnDebugState.blockColors);
}

U32 beaconRandomColor(void)
{
	return 0xFF000000 | (randInt(0xFF) << 16) | randInt(0xFF) << 8 | randInt(0xFF);
}

U32 beaconDebugGetIDColor(U32 id)
{
	static U32 colors[] = {	0xff00ff00, 0xff0000ff, 0xffffff00, 0xffff00ff, 0xff00ffff, 0xffcc0000, 
							0xff00cc00, 0xff0000cc, 0xffffcc00, 0xffff00cc, 0xff00ffcc, 0xffccff00, 
							0xffcc00ff, 0xff00ccff, 0xffcccc00};
	U32 color = 0;

	id++;

	if(!stashIntFindInt(gbcnDebugState.blockColors, id, &color))
	{
		U32 colorid = stashGetCount(gbcnDebugState.blockColors);

		if(colorid < ARRAY_SIZE(colors))
			color = colors[colorid];
		else
			color = beaconRandomColor();

		stashIntAddInt(gbcnDebugState.blockColors, id, color, true);
	}

	return color;
}

void beaconDrawDebug(BeaconDebugInfo *info, Vec3 pos, BeaconDrawLineFunc line, void* linedata, BeaconDrawSphereFunc sphere, void* spheredata)
{
	static F32 lastdist = 0;

	PERFINFO_AUTO_START_FUNC();

	if(info && info->sendPath)
	{
		int i;
		int colors[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF};
		for(i=0; i<eaSize(&info->path.nodes)-1; i++)
		{
			BeaconDebugPathNode *n1 = info->path.nodes[i];
			BeaconDebugPathNode *n2 = info->path.nodes[i+1];
			line(n1->pos, colors[n2->type], n2->pos, colors[n2->type], linedata);
		}
	}

	if(gbcnDebugState.debugEnabled && eaSize(&gDebugBeacons))
	{
		int i, j;
		Vec3 offmin = {-0.1f, -0.1f, -0.1f};
		Vec3 offmax = { 0.1f,  0.1f,  0.1f};
		Vec3 min, max;
		Vec3 p1, p2;

		beaconDebugIDColorReset();
		
		if(gbcnDebugState.flags & (bdBeacons|bdGround|bdRaised|bdNearby|bdSubblock|bdGalaxy|bdCluster))
		{
			DebugBeacon *closest = NULL;
			static DebugBeacon **beaconArray = NULL;
			Vec3 off = {gbcnDebugState.dijkstraDist, 5000, gbcnDebugState.dijkstraDist};
			Vec3 posAt0;

			copyVec3(pos, min);
			copyVec3(pos, max);

			addVec3(max, off, max);
			subVec3(min, off, min);

			eaClear(&beaconArray);
			copyVec3(pos, posAt0);
			posAt0[1] = 0;
			sparseGridFindInSphereEA(gDebugBeaconSG, posAt0, gbcnDebugState.dijkstraDist, &beaconArray);
			
			for(i=0; i<eaSize(&beaconArray); i++)
			{
				DebugBeacon *bcn = beaconArray[i];

				if(gbcnDebugState.flags & bdNearby)
				{
					if(!closest)
					{
						closest = bcn;
					}
					else
					{
						if(distance3Squared(pos, bcn->pos) < distance3Squared(pos, closest->pos))
						{
							closest = bcn;
						}
					}
				}
				
				if(gbcnDebugState.flags & bdBeacons)
				{
					int color;
					addVec3(bcn->pos, offmin, min);
					addVec3(bcn->pos, offmax, max);
					if(info->entHeight && bcn->ceilDist<info->entHeight)
						color = 0xFFFF0000;
					else if(bcn->received)
						color = 0xFFFF00FF;
					else
						color = 0xFF00FF00;
					line(min, color, max, color, linedata);
				}

				if(gbcnDebugState.flags & bdGround)
				{
					copyVec3(bcn->pos, p1);
					p1[1] -= 0.01;

					for(j=0; j<eaSize(&bcn->gConns); j++)
					{
						DebugConnection *conn = bcn->gConns[j];

						assert(conn && conn->dest);

						copyVec3(conn->dest->pos, p2);
						p2[1] += 0.01;

						if(conn->destroyed)
						{
							line(p1, 0xFF0000FF, p2, 0xFF000000, linedata);
						}
						else if(conn->disabled)
						{
							line(p1, 0xFFFF0000, p2, 0xFFFF7F00, linedata);
						}
						else if(conn->bidir)
						{
							line(p1, 0xFF0000FF, p2, 0xFF0000FF, linedata);
						}
						else
						{
							line(p1, 0xFFFF00FF, p2, 0xFF00FF00, linedata);
						}
					}
				}

				if(gbcnDebugState.flags & bdRaised)
				{
					copyVec3(bcn->pos, p1);
					for(j=0; j<eaSize(&bcn->rConns); j++)
					{
						DebugConnection *conn = bcn->rConns[j];
						int colorMin = 0xFF00FF00;
						int colorMax = 0xFF0000FF;
						F32 bcnH;

						if(!conn || !conn->dest)
						{
							continue;
						}

						if(conn->disabled)
						{
							colorMin = colorMax = 0xFFFF0000;
						}
						else if(info->entHeight && (conn->maxH-conn->minH)<info->entHeight)
						{
							colorMin = colorMax = 0xFFFF7070;
						}

						copyVec3(conn->dest->pos, p2);

						if(bcn->pos[1] < conn->dest->pos[1])
							bcnH = conn->dest->pos[1];
						else
							bcnH = bcn->pos[1];

						p1[1] = bcnH + conn->minH;
						p2[1] = bcnH + conn->minH;

						line(p1, colorMin, p2, colorMin, linedata);

						p1[1] = bcnH + conn->maxH;
						p2[1] = bcnH + conn->maxH;

						line(p1, colorMax, p2, colorMax, linedata);

						copyVec3(bcn->pos, p2);
						p1[1] = bcn->pos[1];
						p2[1] = bcnH + conn->maxH;

						line(p1, 0xFFFFFFFF, p2, 0xFFFFFFFF, linedata);
					}
				}

				if(gbcnDebugState.flags & bdGalaxy)
				{
					Vec3 spherepos;

					copyVec3(bcn->pos, spherepos);
					for(j = 0; j < beacon_galaxy_group_count; j++)
					{
						U32 galaxyId = bcn->galaxyIds[j];
						DebugBeaconGalaxy *galaxy = gDebugGalaxies[j][galaxyId];

						if(!galaxy->valid)
							continue;

						spherepos[1] = bcn->pos[1] + (j * 1) * 0.5;
						sphere(spherepos, 0.25, galaxy->color, spheredata);
					}
				}

				if(gbcnDebugState.flags & bdSubblock)
				{
					U32 color = beaconDebugGetIDColor(bcn->blockId);

					FOR_EACH_IN_EARRAY(bcn->gConns, DebugConnection, dbgConn)
					{
						DebugBeacon *dst = dbgConn->dest;

						if(dbgConn->disabled)
							line(bcn->pos, 0xFFFF0000, dst->pos, 0xFFFF0000, linedata);
						else
							line(bcn->pos, color, dst->pos, color, linedata);
					}
					FOR_EACH_END;
				}

				if(gbcnDebugState.flags & bdCluster)
				{
					U32 color = beaconDebugGetIDColor(bcn->clusterId);

					FOR_EACH_IN_EARRAY(bcn->gConns, DebugConnection, dbgConn)
					{
						DebugBeacon *dst = dbgConn->dest;

						if(dbgConn->disabled)
							line(bcn->pos, 0xFFFF0000, dst->pos, 0xFFFF0000, linedata);
						else
							line(bcn->pos, color, dst->pos, color, linedata);
					}
					FOR_EACH_END;
				}
			}

			if(gbcnDebugState.flags & bdNearby)
			{
				if(closest)
				{
					if(gbcnDebugState.lastclosest!=closest || lastdist!=gbcnDebugState.dijkstraDist)
					{
						for(i=0; i<eaSize(&gDebugBeacons); i++)
						{
							gDebugBeacons[i]->dijkstra = -1;
						}

						closest->dijkstra = 0;
						beaconDijkstraHelper(closest);
					}

					gbcnDebugState.lastclosest = closest;
					lastdist = gbcnDebugState.dijkstraDist;

					beaconDrawDijkstraHelper(closest, line, linedata);
				}
			}
		}

		if(gbcnDebugState.flags & bdGalaxy)
		{
			int galaxySet = gbcnDebugState.dijkstraDist + 0.5;
			static int *blocks = NULL;
			beaconDebugIDColorReset();

			/*
			FOR_EACH_IN_EARRAY(gDebugGalaxies[galaxySet], DebugBeaconGalaxy, galaxy)
			{
				int color = beaconDebugGetIDColor((U32)(intptr_t)galaxy);
				eaiClear(&blocks);
				eaiPushEArray(&blocks, &galaxy->blocks);
				galaxySet--;
				while(galaxySet > 0)
				{
					
				}
			}
			FOR_EACH_END;
			*/
		}

		if(gbcnDebugState.flags & bdCluster)
		{
			for(i=0; i<eaSize(&gDebugConnections); i++)
			{
				DebugConnection *conn = gDebugConnections[i];

				if(!conn || !conn->dest)
				{
					continue;
				}

				line(conn->src->pos, 0xFF00F0F0, conn->dest->pos, 0xFF0F0F00, linedata);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void beaconDebugSendInitial(BcnDebugger *dbger, Packet *pkt)
{
	int i;

	BeaconStatePartition *partition = beaconStatePartitionGet(dbger->partitionIdx, false);

	pktSendU32(pkt, BDM_INIT);
	pktSendU32(pkt, combatBeaconArray.size);
	pktSendU32(pkt, partition->nextSubBlockIndex);
	pktSendU32(pkt, beacon_galaxy_group_count);
	for(i = 0; i < beacon_galaxy_group_count; i++)
	{
		pktSendU32(pkt, partition->nextGalaxyIndex[i]);
	}
	pktSendU32(pkt, BDM_INIT_CHK);

	pktSendU32(pkt, BDM_CLUST);
	pktSendU32(pkt, partition->combatBeaconClusterArray.size);

	for(i=0; i<partition->combatBeaconClusterArray.size; i++)
	{
		int j;
		BeaconBlock *cluster = partition->combatBeaconClusterArray.storage[i];

		assert(cluster->isCluster);

		pktSendU32(pkt, cluster->gbbConns.size);
		for(j=0; j<cluster->gbbConns.size; j++)
		{
			BeaconClusterConnection *bcc = cluster->gbbConns.storage[j];

			pktSendU32(pkt, bcc->source.beacon->globalIndex);
			pktSendU32(pkt, bcc->target.beacon->globalIndex);
		}

		pktSendU32(pkt, cluster->rbbConns.size);
		for(j=0; j<cluster->rbbConns.size; j++)
		{
			BeaconClusterConnection *bcc = cluster->rbbConns.storage[j];

			pktSendU32(pkt, bcc->source.beacon->globalIndex);
			pktSendU32(pkt, bcc->target.beacon->globalIndex);
		}
	}

	pktSendU32(pkt, BDM_CLUST_CHK);
}

static void beaconDebugSendBeacons(BcnDebugger *dbger, Packet *pkt)
{
	Beacon *bcn = combatBeaconArray.storage[dbger->bcnsSent];
	BeaconBlock *block = NULL;
	BeaconBlock *galaxy = NULL;
	BeaconBlock *cluster = NULL;
	BeaconStatePartition *partition = beaconStatePartitionGet(dbger->partitionIdx, true);
	int i;

	beaconGetBlockData(bcn, dbger->partitionIdx, &block, NULL, NULL);

	pktSendU32(pkt, BDM_BCNS);

	pktSendU32(pkt, bcn->globalIndex);
	pktSendVec3(pkt, bcn->pos);
	pktSendF32(pkt, beaconGetCeilingDistance(dbger->partitionIdx, bcn));
	pktSendU32(pkt, block->globalIndex);
	
	pktSendU32(pkt, beacon_galaxy_group_count);
	galaxy = block->galaxy;
	cluster = galaxy->cluster;
	for(i = 0; i < beacon_galaxy_group_count; i++)
	{
		assert(galaxy->globalIndex < partition->nextGalaxyIndex[i]);
		pktSendU32(pkt, galaxy->globalIndex);
		galaxy = galaxy->galaxy;
	}

	pktSendU32(pkt, cluster->globalIndex);

	pktSendU32(pkt, BDM_BCNS_CHK);
}

static void beaconDebugSendBlocks(BcnDebugger *dbger, Packet *pkt)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(dbger->partitionIdx, true);
	BeaconBlock *gridBlock = partition->combatBeaconGridBlockArray.storage[dbger->gridBlocksSent];
	int i;

	pktSendU32(pkt, BDM_SUBBLK);

	pktSendU32(pkt, gridBlock->subBlockArray.size);
	for(i=0; i<gridBlock->subBlockArray.size; i++)
	{
		BeaconBlock *subBlock = gridBlock->subBlockArray.storage[i];
		int j;

		pktSendU32(pkt, subBlock->globalIndex);

		pktSendU32(pkt, subBlock->beaconArray.size);
		for(j=0; j<subBlock->beaconArray.size; j++)
		{
			Beacon* b = subBlock->beaconArray.storage[j];

			pktSendU32(pkt, b->globalIndex);
		}

		pktSendU32(pkt, subBlock->gbbConns.size);
		for(j=0; j<subBlock->gbbConns.size; j++)
		{
			BeaconBlockConnection *blockConn = subBlock->gbbConns.storage[j];

			pktSendU32(pkt, blockConn->destBlock->globalIndex);

			pktSendU32(pkt, blockConn->connCount);
			pktSendU32(pkt, blockConn->blockCount);
		}

		pktSendU32(pkt, subBlock->rbbConns.size);
		for(j=0; j<subBlock->rbbConns.size; j++)
		{
			BeaconBlockConnection *blockConn = subBlock->rbbConns.storage[j];

			pktSendU32(pkt, blockConn->destBlock->globalIndex);

			pktSendU32(pkt, blockConn->minHeight);
			pktSendU32(pkt, blockConn->maxHeight);
			pktSendU32(pkt, blockConn->minJumpHeight);

			pktSendU32(pkt, blockConn->connCount);
			pktSendU32(pkt, blockConn->blockCount);
		}
	}

	pktSendU32(pkt, 0xdeadbeef);

	pktSendU32(pkt, BDM_SUBBLK_CHK);
}

void beaconDebugReceiveSubBlocks(Packet *pkt)
{
	int numSubBlocks = pktGetU32(pkt);
	int i;
	int test;

	for(i=0; i<numSubBlocks; i++)
	{
		int j;
		int idx = 0;
		int bcnCount;
		int connCount;
		Vec3 pos = {0};
		DebugBeaconBlock *dbgBlock = NULL;

		idx = pktGetU32(pkt);
		assert(idx < eaSize(&gDebugBeaconBlocks));
		dbgBlock = gDebugBeaconBlocks[idx];

		bcnCount = pktGetU32(pkt);
		eaiClearFast(&dbgBlock->beacons);
		for(j=0; j<bcnCount; j++)
		{
			int bidx;
			DebugBeacon *bcn = NULL;

			bidx = pktGetU32(pkt);
			eaiPush(&dbgBlock->beacons, bidx);

			bcn = eaGet(&gDebugBeacons, bidx);
			addVec3(bcn->pos, pos, pos);
		}
		scaleVec3(pos, 1.0 / bcnCount, dbgBlock->pos);

		connCount = pktGetU32(pkt);
		eaClearEx(&dbgBlock->gConns, NULL);
		for(j=0; j<connCount; j++)
		{
			DebugBeaconBlockConnection *blockConn = calloc(1, sizeof(DebugBeaconBlockConnection));
			int dstIdx;
			int blockConnCount;
			int blockCount;

			dstIdx = pktGetU32(pkt);
			blockConnCount = pktGetU32(pkt);
			blockCount = pktGetU32(pkt);
			blockConn->dst = dstIdx;
			blockConn->connCount = blockConnCount;
			blockConn->blockCount = blockCount;

			eaPush(&dbgBlock->gConns, blockConn);
		}

		connCount = pktGetU32(pkt);
		eaClearEx(&dbgBlock->rConns, NULL);
		for(j=0; j<connCount; j++)
		{
			DebugBeaconBlockConnection *blockConn = calloc(1, sizeof(DebugBeaconBlockConnection));
			int dstIdx;
			U32 minH, maxH, minJumpH, blockConnCount, blockCount;

			dstIdx = pktGetU32(pkt);
			blockConn->dst = dstIdx;

			minH = pktGetU32(pkt);
			maxH = pktGetU32(pkt);
			minJumpH = pktGetU32(pkt);

			blockConnCount = pktGetU32(pkt);
			blockCount = pktGetU32(pkt);

			blockConn->minH = minH;
			blockConn->maxH = maxH;
			blockConn->minJumpH = minJumpH;

			blockConn->connCount = blockConnCount;
			blockConn->blockCount = blockCount;

			eaPush(&dbgBlock->rConns, blockConn);
		}
	}

	test = pktGetU32(pkt);
	assert(test == 0xdeadbeef);
}

static void beaconDebugReceiveGalaxyCreate(Packet *pkt)
{
	BeaconGalaxyCreateChange *change = pktGetStruct(pkt, parse_BeaconGalaxyCreateChange);
	DebugBeaconGalaxy *galaxy = NULL;
	DebugBeaconGalaxy ***galaxySet = NULL;

	if(change->galaxySet < 0 || change->galaxySet > beacon_galaxy_group_count)
		goto cleanup;

	if(change->galaxyIdx < 0)
		goto cleanup;

	galaxySet = &gDebugGalaxies[change->galaxySet];
	if(change->galaxyIdx >= eaSize(galaxySet))
	{
		while(change->galaxyIdx >= eaSize(galaxySet))
		{
			DebugBeaconGalaxy *newgalaxy;

			newgalaxy = callocStruct(DebugBeaconGalaxy);
			newgalaxy->id = eaSize(galaxySet);
			eaPush(galaxySet, newgalaxy);
		}
	}
	galaxy = eaGet(galaxySet, change->galaxyIdx);

	galaxy->valid = true;
	galaxy->color = beaconRandomColor();
	
cleanup:
	free(change);
}

static void beaconDebugReceiveGalaxyDestroy(Packet *pkt)
{
	BeaconGalaxyDestroyChange *change = pktGetStruct(pkt, parse_BeaconGalaxyDestroyChange);
	DebugBeaconGalaxy *galaxy = NULL;

	if(change->galaxySet < 0 || change->galaxySet > beacon_galaxy_group_count)
		goto cleanup;

	galaxy = eaGet(&gDebugGalaxies[change->galaxySet], change->galaxyIdx);
	if(!galaxy)
		goto cleanup;

	galaxy->valid = false;
	galaxy->received = false;
	galaxy->color = 0xFFFF0000;

cleanup:
	free(change);
}

static void beaconDebugReceiveGalaxyChange(Packet* pkt)
{
	BeaconGalaxyChange *change = pktGetStruct(pkt, parse_BeaconGalaxyChange);

	if(change->galaxySet < 0 || change->galaxySet > beacon_galaxy_group_count)
		goto cleanup;

	if(change->galaxySet == 0)
	{
		DebugBeaconGalaxy *galaxy = eaGet(&gDebugGalaxies[change->galaxySet], change->galaxyIdx);
		DebugBeaconBlock *block = eaGet(&gDebugBeaconBlocks, change->blockIdx);
		DebugBeaconGalaxy *oldgalaxy = eaGet(&gDebugGalaxies[0], block->galaxy);
		
		// Break old link
		eaiFindAndRemoveFast(&oldgalaxy->blocks, block->id);

		// Create new link
		eaiPush(&galaxy->blocks, block->id);
		block->galaxy = galaxy->id;
	}
	else
	{
		DebugBeaconGalaxy *galaxy = eaGet(&gDebugGalaxies[change->galaxySet], change->galaxyIdx);
		DebugBeaconGalaxy *child = eaGet(&gDebugGalaxies[change->galaxySet-1], change->blockIdx);
		DebugBeaconGalaxy *oldparent = eaGet(&gDebugGalaxies[change->galaxySet], child->parent);
		
		// Break old link (may not exist)
		eaiFindAndRemoveFast(&oldparent->blocks, child->id);

		// Create new link
		eaiPush(&galaxy->blocks, child->id);
		child->parent = galaxy->id;
	}

cleanup:
	free(change);
}

static void beaconDebugReceiveSubBlockCreate(Packet *pkt)
{
	BeaconSubBlockCreateChange *change = pktGetStruct(pkt, parse_BeaconSubBlockCreateChange);
	DebugBeaconBlock *subblock = NULL;

	if(change->blockIdx < 0)
		goto cleanup;

	if(change->blockIdx >= eaSize(&gDebugBeaconBlocks))
	{
		while(change->blockIdx >= eaSize(&gDebugBeaconBlocks))
		{
			DebugBeaconBlock *newblock = NULL;
			newblock = callocStruct(DebugBeaconBlock);

			newblock->id = eaSize(&gDebugBeaconBlocks);
			newblock->received = 0;

			eaPush(&gDebugBeaconBlocks, newblock);
		}
	}

	subblock = eaGet(&gDebugBeaconBlocks, change->blockIdx);
	subblock->received = true;

cleanup:
	free(change);
}

static void beaconDebugReceiveSubBlockDestroy(Packet *pkt)
{
	BeaconSubBlockCreateChange *change = pktGetStruct(pkt, parse_BeaconSubBlockCreateChange);
	DebugBeaconBlock *subblock = NULL;

	if(change->blockIdx < 0 || change->blockIdx >= eaSize(&gDebugBeaconBlocks))
		goto cleanup;

	subblock = eaGet(&gDebugBeaconBlocks, change->blockIdx);
	subblock->received = false;

cleanup:
	free(change);
}

static void beaconDebugReceiveSubBlockChange(Packet* pkt)
{
	BeaconSubBlockChange *change = pktGetStruct(pkt, parse_BeaconSubBlockChange);
	DebugBeaconBlock *subblock = NULL;
	DebugBeaconBlock *oldblock = NULL;
	DebugBeacon *bcn = NULL;

	if(change->block < 0 || change->block >= eaSize(&gDebugBeaconBlocks))
		goto cleanup;

	if(change->bcn < 0 || change->bcn >= eaSize(&gDebugBeacons))
		goto cleanup;

	subblock = eaGet(&gDebugBeaconBlocks, change->block);

	bcn = eaGet(&gDebugBeacons, change->bcn);
	oldblock = eaGet(&gDebugBeaconBlocks, bcn->blockId);

	// Remove bcn->oldblock link
	eaiFindAndRemoveFast(&oldblock->beacons, change->bcn);

	// Add new link
	bcn->blockId = subblock->id;
	eaiPush(&subblock->beacons, change->bcn);

cleanup:
	free(change);
}

static void beaconDebugSendGalaxy(BcnDebugger *dbger, Packet *pkt)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(dbger->partitionIdx, true);
	BeaconBlock *galaxy = partition->combatBeaconGalaxyArray[dbger->galaxyLevel].storage[dbger->galaxiesSent];
	int i;

	devassert(galaxy->galaxySet == dbger->galaxyLevel);
	if(galaxy->galaxySet != dbger->galaxyLevel)
		return;

	pktSendU32(pkt, BDM_GALAXY);

	pktSendU32(pkt, galaxy->galaxySet);
	pktSendU32(pkt, galaxy->globalIndex);

	pktSendU32(pkt, galaxy->subBlockArray.size);
	for(i=0; i<galaxy->subBlockArray.size; i++)
	{
		BeaconBlock *subBlock = galaxy->subBlockArray.storage[i];

		pktSendU32(pkt, subBlock->globalIndex);
	}

	pktSendU32(pkt, galaxy->gbbConns.size);
	for(i=0; i<galaxy->gbbConns.size; i++)
	{
		BeaconBlockConnection *blockConn = galaxy->gbbConns.storage[i];

		pktSendU32(pkt, blockConn->destBlock->globalIndex);
		pktSendU32(pkt, blockConn->connCount);
		pktSendU32(pkt, blockConn->blockCount);
	}

	pktSendU32(pkt, galaxy->rbbConns.size);
	for(i=0; i<galaxy->rbbConns.size; i++)
	{
		BeaconBlockConnection *blockConn = galaxy->rbbConns.storage[i];

		pktSendU32(pkt, blockConn->destBlock->globalIndex);
		pktSendU32(pkt, blockConn->minHeight);
		pktSendU32(pkt, blockConn->maxHeight);
		pktSendU32(pkt, blockConn->minJumpHeight);

		pktSendU32(pkt, blockConn->connCount);
		pktSendU32(pkt, blockConn->blockCount);
	}

	pktSendU32(pkt, BDM_GALAXY_CHK);
}

static void beaconDebugReceiveGalaxy(Packet *pkt)
{
	int galaxySet;
	int galaxyIdx;
	int blockCount;
	int rConnCount;
	int gConnCount;
	int i;
	DebugBeaconGalaxy *galaxy = NULL;
	
	galaxySet = pktGetU32(pkt);
	galaxyIdx = pktGetU32(pkt);

	assert(galaxySet >= 0);
	assert(galaxySet < beacon_galaxy_group_count);
	assert(galaxyIdx >= 0);
	assert(galaxyIdx < eaSize(&gDebugGalaxies[galaxySet]));

	galaxy = eaGet(&gDebugGalaxies[galaxySet], galaxyIdx);

	galaxy->id = galaxyIdx;
	galaxy->valid = true;
	galaxy->color = beaconRandomColor();
	blockCount = pktGetU32(pkt);
	eaiClearFast(&galaxy->blocks);
	for(i=0; i<blockCount; i++)
	{
		int blockId;

		blockId = pktGetU32(pkt);

		eaiPush(&galaxy->blocks, blockId);
	}

	gConnCount = pktGetU32(pkt);
	eaClearEx(&galaxy->gConns, NULL);
	for(i=0; i<gConnCount; i++)
	{
		int dstId;
		int connCount;
		DebugBeaconBlockConnection *conn;

		dstId = pktGetU32(pkt);
		connCount = pktGetU32(pkt);
		blockCount = pktGetU32(pkt);

		conn = callocStruct(DebugBeaconBlockConnection);
		conn->src = galaxyIdx;
		conn->dst = dstId;

		conn->connCount = connCount;
		conn->blockCount = blockCount;

		eaPush(&galaxy->gConns, conn);
	}

	rConnCount = pktGetU32(pkt);
	eaClearEx(&galaxy->rConns, NULL);
	for(i=0; i<rConnCount; i++)
	{
		int dstId;
		int connCount;
		U32 minH, maxH, minJumpH;
		DebugBeaconBlockConnection *conn;

		dstId = pktGetU32(pkt);
		connCount = pktGetU32(pkt);
		blockCount = pktGetU32(pkt);

		minH = pktGetU32(pkt);
		maxH = pktGetU32(pkt);
		minJumpH = pktGetU32(pkt);

		conn = callocStruct(DebugBeaconBlockConnection);
		conn->src = galaxyIdx;
		conn->dst = dstId;

		conn->connCount = connCount;
		conn->blockCount = blockCount;

		conn->minH = minH;
		conn->maxH = maxH;
		conn->minJumpH = minJumpH;

		eaPush(&galaxy->rConns, conn);
	}
}

static U32 beaconDebugConnIsBidir(BcnDebugger *dbger, Beacon *bcn, BeaconConnection *conn)
{
	int i;
	BeaconPartitionData *partition = beaconGetPartitionData(bcn, dbger->partitionIdx, false);

	for(i=0; i<conn->destBeacon->gbConns.size; i++)
	{
		BeaconConnection *other = conn->destBeacon->gbConns.storage[i];

		if(other->destBeacon == bcn)
		{
			return 1;
		}
	}

	if(partition)
	{
		FOR_EACH_IN_EARRAY(partition->disabledConns, BeaconDynamicConnection, dynConn)
		{
			if(dynConn->conn->destBeacon==bcn)
				return 1;
		}
		FOR_EACH_END
	}

	return 0;
}

static void beaconDebugSendBeaconConns(BcnDebugger *dbger, Beacon *bcn, Packet *pkt)
{
	int i;
	BeaconPartitionData *partition = beaconGetPartitionData(bcn, dbger->partitionIdx, false);

	pktSendU32(pkt, BDM_CONNS);

	pktSendU32(pkt, bcn->globalIndex);
	
	pktSendU32(pkt, bcn->gbConns.size);
	for(i=0; i<bcn->gbConns.size; i++)
	{
		BeaconConnection *conn = bcn->gbConns.storage[i];

		pktSendU32(pkt, conn->destBeacon->globalIndex);

		pktSendU32(pkt, beaconDebugConnIsBidir(dbger, bcn, conn));	
	}

	pktSendU32(pkt, bcn->rbConns.size);
	for(i=0; i<bcn->rbConns.size; i++)
	{
		BeaconConnection *conn = bcn->rbConns.storage[i];

		pktSendU32(pkt, conn->destBeacon->globalIndex);
		pktSendF32(pkt, conn->minHeight);
		pktSendF32(pkt, conn->maxHeight);
	}

	if(!partition)
	{
		pktSendU32(pkt, 0);
	}
	else
	{
		pktSendU32(pkt, eaSize(&partition->disabledConns));
		FOR_EACH_IN_EARRAY(partition->disabledConns, BeaconDynamicConnection, dynConn)
		{
			pktSendU32(pkt, dynConn->conn->destBeacon->globalIndex);
			pktSendU32(pkt, dynConn->raised);
		}
		FOR_EACH_END
	}

	pktSendU32(pkt, BDM_CONNS_CHK);
}

void beaconDebugDynConnChange(int partitionIdx, Beacon *b, Beacon *d, int raised, int enabled)
{
	BeaconDynConnChange *change = calloc(1, sizeof(BeaconDynConnChange));

	change->partitionIdx = partitionIdx;
	change->src = b;
	change->dst = d;
	change->raised = raised;
	change->enabled = enabled;

	eaPush(&gbcnDebugState.dynChanges, change);
}

void beaconDebugSubBlockChange(int partitionIdx, Beacon* b, BeaconBlock *block)
{
	BeaconSubBlockChange *change = calloc(1, sizeof(BeaconSubBlockChange));

	change->partitionIdx = partitionIdx;
	change->bcn = b->globalIndex;
	change->block = block->globalIndex;

	eaPush(&gbcnDebugState.blockChanges, change);
}

void beaconDebugSubBlockCreate(int partitionIdx, BeaconBlock* block)
{
	int i;
	BeaconSubBlockCreateChange *change = NULL;

	if(!gbcnDebugState.debugEnabled)
		return;

	change = calloc(1, sizeof(BeaconSubBlockCreateChange));

	change->partitionIdx = partitionIdx;
	change->blockIdx = block->globalIndex;

	eaPush(&gbcnDebugState.subblockCreates, change);

	for(i=0; i<block->beaconArray.size; i++)
		beaconDebugSubBlockChange(partitionIdx, block->beaconArray.storage[i], block);
}

void beaconDebugSubBlockDestroy(int partitionIdx, BeaconBlock* destroy, BeaconBlock *merged)
{
	int i;
	BeaconSubBlockDestroyChange *change = NULL;

	if(!gbcnDebugState.debugEnabled)
		return;

	change = calloc(1, sizeof(BeaconSubBlockDestroyChange));

	change->partitionIdx = partitionIdx;
	change->blockIdx = destroy->globalIndex;

	eaPush(&gbcnDebugState.subblockDestroys, change);

	for(i=0; i<merged->beaconArray.size; i++)
		beaconDebugSubBlockChange(partitionIdx, merged->beaconArray.storage[i], merged);
}

void beaconDebugGalaxyChange(int partitionIdx, BeaconBlock *galaxy, BeaconBlock *block)
{
	BeaconGalaxyChange *change = NULL;

	if(!gbcnDebugState.debugEnabled)
		return;

	change = calloc(1, sizeof(BeaconGalaxyChange));
	change->galaxySet = galaxy->galaxySet;
	change->galaxyIdx = galaxy->globalIndex;
	change->blockIdx = block->globalIndex;

	eaPush(&gbcnDebugState.galaxyChanges, change);
}

void beaconDebugGalaxyCreate(int partitionIdx, BeaconBlock *galaxy)
{
	int i;
	BeaconGalaxyCreateChange *change = NULL;

	if(!gbcnDebugState.debugEnabled)
		return;

	change = calloc(1, sizeof(BeaconGalaxyCreateChange));
	change->galaxySet = galaxy->galaxySet;
	change->galaxyIdx = galaxy->globalIndex;

	eaPush(&gbcnDebugState.galaxyCreates, change);

	for(i=0; i<galaxy->subBlockArray.size; i++)
		beaconDebugGalaxyChange(partitionIdx, galaxy, galaxy->subBlockArray.storage[i]);
}

void beaconDebugGalaxyDestroy(int partitionIdx, BeaconBlock *destroy, BeaconBlock *merged)
{
	int i;
	BeaconGalaxyDestroyChange *change = NULL;

	if(!gbcnDebugState.debugEnabled)
		return;

	change = calloc(1, sizeof(BeaconGalaxyDestroyChange));
	change->galaxySet = destroy->galaxySet;
	change->galaxyIdx = destroy->globalIndex;

	eaPush(&gbcnDebugState.galaxyDestroys, change);

	for(i=0; i<merged->subBlockArray.size; i++)
		beaconDebugGalaxyChange(partitionIdx, merged, merged->subBlockArray.storage[i]);
}

static void beaconDebugSendConnDelete(BcnDebugger *dbger, Beacon *b, Packet *pkt)
{
	pktSendU32(pkt, BDM_DEL);
	pktSendU32(pkt, b->globalIndex);
	pktSendU32(pkt, BDM_DEL_CHK);
}

Beacon **debugBeaconArray = NULL;

// typedef void BeaconForEachBlockCallback(Array* beaconArray, void* userData);
void beaconDebugGatherBeacons(Array* beaconArray, BcnDebugger* dbger)
{
	int i;

	for(i=0; i<beaconArray->size; i++)
	{
		Beacon *b = beaconArray->storage[i];

		//if(distance3XZ(dbger->pos, b->pos)<dbger->max_dist*2)
		{
			eaPush(&debugBeaconArray, b);
		}
	}
}

void beaconDebugSendDynConnChange(BcnDebugger *dbger, Packet *pkt, BeaconDynConnChange *change)
{
	pktSendU32(pkt, BDM_DYN);
	pktSendU32(pkt, change->src->globalIndex);
	pktSendU32(pkt, change->dst->globalIndex);
	pktSendU32(pkt, change->raised);
	pktSendU32(pkt, change->enabled);
	pktSendU32(pkt, BDM_DYN_CHK);
}

void beaconDebugSendGalaxyCreateChange(BcnDebugger *dbger, Packet *pkt, BeaconGalaxyCreateChange *change)
{
	pktSendU32(pkt, BDM_GALCRT);
	pktSendStruct(pkt, change, parse_BeaconGalaxyCreateChange);
	pktSendU32(pkt, BDM_GALCRT_CHK);
}

void beaconDebugSendGalaxyDestroyChange(BcnDebugger *dbger, Packet *pkt, BeaconGalaxyDestroyChange *change)
{
	pktSendU32(pkt, BDM_GALDES);
	pktSendStruct(pkt, change, parse_BeaconGalaxyDestroyChange);
	pktSendU32(pkt, BDM_GALDES_CHK);
}

void beaconDebugSendSubBlockCreateChange(BcnDebugger *dbger, Packet *pkt, BeaconSubBlockCreateChange *change)
{
	pktSendU32(pkt, BDM_BLKCRT);
	pktSendStruct(pkt, change, parse_BeaconSubBlockCreateChange);
	pktSendU32(pkt, BDM_BLKCRT_CHK);
}

void beaconDebugSendSubBlockDestroyChange(BcnDebugger *dbger, Packet *pkt, BeaconSubBlockDestroyChange *change)
{
	pktSendU32(pkt, BDM_BLKDES);
	pktSendStruct(pkt, change, parse_BeaconSubBlockDestroyChange);
	pktSendU32(pkt, BDM_BLKDES_CHK);
}

void beaconDebugSendSubBlockChange(BcnDebugger *dbger, Packet *pkt, BeaconSubBlockChange *change)
{
	pktSendU32(pkt, BDM_BLKCHG);
	pktSendStruct(pkt, change, parse_BeaconSubBlockChange);
	pktSendU32(pkt, BDM_BLKCHG_CHK);	
}

void beaconDebugSendGalaxyChange(BcnDebugger *dbger, Packet *pkt, BeaconGalaxyChange *change)
{
	pktSendU32(pkt, BDM_GALCHG);
	pktSendStruct(pkt, change, parse_BeaconGalaxyChange);
	pktSendU32(pkt, BDM_GALCHG_CHK);
}

void beaconDebugUpdateDebugger(BcnDebugger *dbger, Packet *pkt, F32 timeElapsed)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	eaClear(&debugBeaconArray);

	if(!dbger->initial_send)
	{
		dbger->initial_send = 1;

		beaconDebugSendInitial(dbger, pkt);
	}

	dbger->last_updated += timeElapsed;
	if(dbger->last_updated > 0)
	{
		F32 sqrDistSend = SQR(dbger->max_dist*1.5);
		F32 sqrDistDel = SQR(dbger->max_dist*2);
		BeaconStatePartition *partition = beaconStatePartitionGet(dbger->partitionIdx, true);
		dbger->last_updated = 0;

		while(dbger->bcnsSent < combatBeaconArray.size && pktGetSize(pkt) < dbger->max_pkt_size)
		{
			beaconDebugSendBeacons(dbger, pkt);
			dbger->bcnsSent++;
		}

		while(dbger->gridBlocksSent < partition->combatBeaconGridBlockArray.size && pktGetSize(pkt) < dbger->max_pkt_size)
		{
			beaconDebugSendBlocks(dbger, pkt);
			dbger->gridBlocksSent++;
		}

		while(dbger->galaxyLevel < beacon_galaxy_group_count && pktGetSize(pkt) < dbger->max_pkt_size)
		{
			while(	dbger->galaxiesSent < partition->combatBeaconGalaxyArray[dbger->galaxyLevel].size && 
					dbger->galaxyLevel < beacon_galaxy_group_count &&
					pktGetSize(pkt) < dbger->max_pkt_size)
			{
				beaconDebugSendGalaxy(dbger, pkt);
				dbger->galaxiesSent++;
			}

			if(dbger->galaxiesSent >= partition->combatBeaconGalaxyArray[dbger->galaxyLevel].size)
			{
				dbger->galaxyLevel++;
				dbger->galaxiesSent = 0;
			}
		}

		PERFINFO_AUTO_START("StreamUpdate",1);
		beaconForEachBlock(beaconStatePartitionGet(0, false), dbger->pos, dbger->max_dist*3, dbger->max_dist*100, dbger->max_dist*3, beaconDebugGatherBeacons, dbger);

		for(i=0; i<eaSize(&gbcnDebugState.dynChanges); i++)
		{
			BeaconDynConnChange *change = gbcnDebugState.dynChanges[i];

			if(dbger->partitionIdx!=change->partitionIdx)
				continue;

			if(dbger->connsSent[change->src->globalIndex])
			{
				beaconDebugSendDynConnChange(dbger, pkt, change);
			}
		}

		FOR_EACH_IN_EARRAY_FORWARDS(gbcnDebugState.subblockCreates, BeaconSubBlockCreateChange, change)
		{
			if(dbger->partitionIdx != change->partitionIdx)
				continue;

			beaconDebugSendSubBlockCreateChange(dbger, pkt, change);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(gbcnDebugState.subblockDestroys, BeaconSubBlockDestroyChange, change)
		{
			if(dbger->partitionIdx != change->partitionIdx)
				continue;

			beaconDebugSendSubBlockDestroyChange(dbger, pkt, change);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(gbcnDebugState.galaxyCreates, BeaconGalaxyCreateChange, change)
		{
			if(dbger->partitionIdx != change->partitionIdx)
				continue;

			beaconDebugSendGalaxyCreateChange(dbger, pkt, change);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(gbcnDebugState.galaxyDestroys, BeaconGalaxyDestroyChange, change)
		{
			if(dbger->partitionIdx != change->partitionIdx)
				continue;

			beaconDebugSendGalaxyDestroyChange(dbger, pkt, change);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(gbcnDebugState.blockChanges, BeaconSubBlockChange, change)
		{
			if(dbger->partitionIdx!=change->partitionIdx)
				continue;

			beaconDebugSendSubBlockChange(dbger, pkt, change);
		}
		FOR_EACH_END;

		FOR_EACH_IN_EARRAY_FORWARDS(gbcnDebugState.galaxyChanges, BeaconGalaxyChange, change)
		{
			if(dbger->partitionIdx != change->partitionIdx)
				continue;

			beaconDebugSendGalaxyChange(dbger, pkt, change);
		}
		FOR_EACH_END;

		for(i=0; i<eaSize(&debugBeaconArray) && pktGetSize(pkt) < dbger->max_pkt_size; i++)
		{
			Beacon *b = debugBeaconArray[i];
			int index = b->globalIndex;
			F32 dist = distance3SquaredXZ(b->pos, dbger->pos);

			if(dist < sqrDistSend && !dbger->connsSent[index])
			{
				beaconDebugSendBeaconConns(dbger, b, pkt);
				dbger->connsSent[index] = 1;
			}
			else if(dist > sqrDistDel && dbger->connsSent[index])
			{
				beaconDebugSendConnDelete(dbger, b, pkt);
				dbger->connsSent[index] = 0;
			}
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

void beaconDebugFrameEnd(void)
{
	eaClearEx(&gbcnDebugState.dynChanges, NULL);
	eaClearEx(&gbcnDebugState.blockChanges, NULL);
}

void beaconDebugDestroyBeacon(DebugBeacon *bcn)
{
	eaDestroyEx(&bcn->gConns, NULL);
	eaDestroyEx(&bcn->rConns, NULL);

	free(bcn);
}

void beaconHandleDebugMsg(Packet* pkt)
{ 
	int i;
	static int totalBeacons = 0;
	static int numBeacons = 0;
	BeaconDebugMessage msg;
	BeaconDebugMessage msg_chk;

	if(pktEnd(pkt))
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	do {
		msg = pktGetU32(pkt);

		switch(msg)
		{
			xcase BDM_INIT: {
				int size = pktGetU32(pkt);

				numBeacons = 0;
				totalBeacons = size;

				for(i=0; i<size; i++)
				{
					DebugBeacon *bcn;

					bcn = callocStruct(DebugBeacon);

					eaPush(&gDebugBeacons, bcn);
				}

				size = pktGetU32(pkt);
				for(i=0; i<size; i++)
				{
					DebugBeaconBlock *subBlock;
					subBlock = callocStruct(DebugBeaconBlock);

					eaPush(&gDebugBeaconBlocks, subBlock);
				}

				size = pktGetU32(pkt);
				devassert(size == beacon_galaxy_group_count);
				for(i=0; i<size; i++)
				{
					int count = pktGetU32(pkt);
					int j;

					for(j=0; j<count; j++)
					{
						DebugBeaconGalaxy *galaxy;
						galaxy = callocStruct(DebugBeaconGalaxy);

						galaxy->id = j;
						eaPush(&gDebugGalaxies[i], galaxy);
					}

					assert(eaSize(&gDebugGalaxies[i]) == count);
				}

				gDebugBeaconOctree = octreeCreate();
				gDebugBeaconSG = sparseGridCreate(128, 1024);
			}

			xcase BDM_BCNS: {
				int index = pktGetU32(pkt);
				DebugBeacon *bcn = NULL;
				OctreeEntry *octEnt = NULL;
				Vec3 posAt0;
				int j;

				if(!gDebugBeacons || eaSize(&gDebugBeacons)<index)
				{
					continue;
				}
				bcn = gDebugBeacons[index];

				pktGetVec3(pkt, bcn->pos);
				bcn->ceilDist = pktGetF32(pkt);

				copyVec3(bcn->pos, posAt0);
				posAt0[1] = 0;
				sparseGridMove(gDebugBeaconSG, &bcn->sgentry, bcn, posAt0, 0);

				bcn->blockId = pktGetU32(pkt);
				bcn->galaxyCount = pktGetU32(pkt);
				assert(bcn->galaxyCount <= beacon_galaxy_group_count);

				for(j=0; j<bcn->galaxyCount; j++)
				{
					int id = pktGetU32(pkt);

					assert(id < eaSize(&gDebugGalaxies[j]));
					bcn->galaxyIds[j] = id;
				}

				bcn->clusterId = pktGetU32(pkt);

				numBeacons++;
			}

			xcase BDM_DYN: {
				int srcIdx = pktGetU32(pkt);
				int dstIdx = pktGetU32(pkt);
				int raised = pktGetU32(pkt);
				int enabled = pktGetU32(pkt);
				
				DebugBeacon *src = eaGet(&gDebugBeacons, srcIdx);
				DebugBeacon *dst = eaGet(&gDebugBeacons, dstIdx);
				DebugConnection ***conns = raised ? &src->rConns : &src->gConns;

				FOR_EACH_IN_EARRAY(*conns, DebugConnection, conn)
				{
					if(conn->dest==dst)
					{
						conn->disabled = !enabled;
						break;
					}
				}
				FOR_EACH_END
			}

			xcase BDM_GALCRT: {
				beaconDebugReceiveGalaxyCreate(pkt);
			}

			xcase BDM_GALDES: {
				beaconDebugReceiveGalaxyDestroy(pkt);
			}

			xcase BDM_GALCHG: {
				beaconDebugReceiveGalaxyChange(pkt);
			}

			xcase BDM_BLKCHG: {
				beaconDebugReceiveSubBlockChange(pkt);
			}

			xcase BDM_BLKCRT: {
				beaconDebugReceiveSubBlockCreate(pkt);
			}
			
			xcase BDM_BLKDES: {
				beaconDebugReceiveSubBlockDestroy(pkt);
			}

			xcase BDM_SUBBLK: {
				beaconDebugReceiveSubBlocks(pkt);
			}

			xcase BDM_GALAXY: {
				beaconDebugReceiveGalaxy(pkt);
			}

			xcase BDM_CONNS: {
				int index = pktGetU32(pkt);
				int gsize, rsize, dsize;
				DebugBeacon *bcn = NULL;

				if(!gDebugBeacons || eaSize(&gDebugBeacons)<index)
				{
					continue;
				}

				bcn = gDebugBeacons[index];

				bcn->received = 1;

				gsize = pktGetU32(pkt);
				for(i=0; i<gsize; i++)
				{
					int target;
					DebugConnection *conn = NULL;
					conn = callocStruct(DebugConnection);

					target = pktGetU32(pkt);
					conn->src = bcn;
					conn->dest = gDebugBeacons[target];
					conn->bidir = !!pktGetU32(pkt);
					conn->dijkstra = -1;

					eaPush(&bcn->gConns, conn);
				}

				rsize = pktGetU32(pkt);
				for(i=0; i<rsize; i++)
				{
					DebugConnection *conn = NULL;
					conn = callocStruct(DebugConnection);

					conn->src = bcn;
					conn->dest = gDebugBeacons[pktGetU32(pkt)];
					conn->minH = pktGetF32(pkt);
					conn->maxH = pktGetF32(pkt);
					eaPush(&bcn->rConns, conn);
				}

				dsize = pktGetU32(pkt);
				for(i=0; i<dsize; i++)
				{
					U32 raised;
					int target;
					int j;
					DebugConnection **conns;
					DebugBeacon *targetBcn;

					target = pktGetU32(pkt);
					raised = pktGetU32(pkt);

					if(raised)
						conns = bcn->rConns;
					else
						conns = bcn->gConns;

					targetBcn = gDebugBeacons[target];
					for(j=0; j<eaSize(&conns); j++)
					{
						DebugConnection *conn = conns[j];

						if(conn->dest==targetBcn)
						{
							conn->disabled = true;
							break;
						}
					}
				}
			}

			xcase BDM_DEL: {
				int index = pktGetU32(pkt);
				DebugBeacon *bcn = NULL;

				if(!gDebugBeacons || eaSize(&gDebugBeacons)<index)
				{
					continue;
				}

				bcn = gDebugBeacons[index];

				bcn->received = 0;

				eaDestroyEx(&bcn->gConns, NULL);
				eaDestroyEx(&bcn->rConns, NULL);
			}

			xcase BDM_CLUST: {
				int idxs, idxt;
				int clustcount;
				DebugConnection *conn;

				clustcount = pktGetU32(pkt);

				for(i=0; i<clustcount; i++)
				{
					int j;
					int conncount = pktGetU32(pkt);

					for(j=0; j<conncount; j++)
					{
						conn = callocStruct(DebugConnection);
						conn->cluster = 1;

						idxs = pktGetU32(pkt);
						idxt = pktGetU32(pkt);
						assert(idxs < eaSize(&gDebugBeacons) && idxt < eaSize(&gDebugBeacons));

						conn->src = gDebugBeacons[idxs];
						conn->dest = gDebugBeacons[idxt];

						eaPush(&gDebugConnections, conn);
					}

					conncount = pktGetU32(pkt);
					for(j=0; j<conncount; j++)
					{
						conn = callocStruct(DebugConnection);
						conn->cluster = 1;

						idxs = pktGetU32(pkt);
						idxt = pktGetU32(pkt);
						assert(idxs < eaSize(&gDebugBeacons) && idxt < eaSize(&gDebugBeacons));

						conn->src = gDebugBeacons[idxs];
						conn->dest = gDebugBeacons[idxt];

						eaPush(&gDebugConnections, conn);
					}
				}
			}
		}

		msg_chk = pktGetU32(pkt);
		assert(msg+1==msg_chk);
	} while(!pktEnd(pkt));

	if(gbcnDebugState.callback)
	{
		gbcnDebugState.callback(totalBeacons, numBeacons);
	}

	PERFINFO_AUTO_STOP();
}

#endif

int beaconBlockGetMemorySize(BeaconBlock* block, int beacons)
{
	int memory = 0;
	int i;

	//memory += sizeof(BeaconBlock);

	if(beacons)
	{
		memory += block->beaconArray.size * (sizeof(Beacon*) + sizeof(Beacon));

		for(i=0; i<block->beaconArray.size; i++)
		{
			Beacon *b = block->beaconArray.storage[i];

			memory += b->gbConns.size * (sizeof(BeaconConnection*) + sizeof(BeaconConnection));
			memory += b->rbConns.size * (sizeof(BeaconConnection*) + sizeof(BeaconConnection));
		}
	}

	//memory += block->rbbConns.size * (sizeof(BeaconBlockConnection*) + sizeof(BeaconBlockConnection));
	//memory += block->gbbConns.size * (sizeof(BeaconBlockConnection*) + sizeof(BeaconBlockConnection));

	// Don't count the beacon block, because the recursion will
	memory += block->subBlockArray.size * (sizeof(BeaconBlock*));	
	for(i=0; i<block->subBlockArray.size; i++)
	{
		BeaconBlock *subblock = block->subBlockArray.storage[i];

		// Don't count beacons in subblocks, because they are counted above
		memory += beaconBlockGetMemorySize(subblock, false);
	}

	return memory;
}

AUTO_COMMAND ACMD_SERVERONLY;
void beaconDebugLocalizeMemory(void)
{
#if !_XBOX && !_PS3
	int memoryHit = 0, memoryNotHit = 0;
	int i;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);

	beaconTestGeoProximity(worldGetAnyCollPartitionIdx());

	for(i=0; i<partition->combatBeaconGridBlockArray.size; i++)
	{
		BeaconBlock *block = partition->combatBeaconGridBlockArray.storage[i];

		if(stashAddressFindInt(beaconGeoProximityStash, block, NULL))
			memoryHit += beaconBlockGetMemorySize(block, true);
		else
			memoryNotHit += beaconBlockGetMemorySize(block, true);
	}

	printf("MemoryHit: %d, MemoryNotHit: %d, Hit/Not: %.2f", memoryHit, memoryNotHit, ((F32)memoryHit)/memoryNotHit);
#endif
}

AUTO_COMMAND ACMD_SERVERONLY;
void beaconDebugCountArrays(void)
{
#if !PLATFORM_CONSOLE
	int i;
	int count = 1;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);

	count += 4 * partition->combatBeaconGridBlockArray.size;
	for(i=0; i<partition->combatBeaconGridBlockArray.size; i++)
	{
		BeaconBlock *block = partition->combatBeaconGridBlockArray.storage[i];

		count += 4 * block->subBlockArray.size;
	}

	count += 2 * combatBeaconArray.size;

	count += 4 * partition->combatBeaconClusterArray.size;

	count += beacon_galaxy_group_count;
	for(i = 0; i < beacon_galaxy_group_count; i++)
		count += 4 * partition->combatBeaconGalaxyArray[i].size;

	printf("%d arrays\n", count);
#endif
}

static void beaconDebugDestroyBlockConnection(DebugBeaconBlockConnection *conn)
{
	free(conn);
}

static void beaconDebugDestroyGalaxy(DebugBeaconGalaxy *galaxy)
{
	eaDestroyEx(&galaxy->gConns, beaconDebugDestroyBlockConnection);
	eaDestroyEx(&galaxy->rConns, beaconDebugDestroyBlockConnection);
	eaiDestroy(&galaxy->blocks);
	free(galaxy);
}

static void beaconDebugDestroyBlock(DebugBeaconBlock *block)
{
	eaDestroyEx(&block->gConns, beaconDebugDestroyBlockConnection);
	eaDestroyEx(&block->rConns, beaconDebugDestroyBlockConnection);
	eaiDestroy(&block->beacons);
	free(block);
}

void beaconDebugMapUnloadWorldClient(void)
{
#if !PLATFORM_CONSOLE
	int i;
	eaDestroyEx(&gDebugBeacons, beaconDebugDestroyBeacon);
	for(i = 0; i < beacon_galaxy_group_count; i++)
	{
		eaDestroyEx(&gDebugGalaxies[i], beaconDebugDestroyGalaxy);
	}
	eaDestroyEx(&gDebugBeaconBlocks, beaconDebugDestroyBlock);
	eaDestroyEx(&gDebugConnections, NULL);
#endif
}

void beaconDebugMapLoadWorldClient(ZoneMap *map)
{
#if !PLATFORM_CONSOLE
	
#endif
}

void beaconDebugToggleFlag(BeaconDebugFlags flag)
{
	gbcnDebugState.flags ^= flag;
}

void beaconDebugSetFlag(BeaconDebugFlags flag)
{
	gbcnDebugState.flags = flag;
}

AUTO_COMMAND;
void beaconDebugSetLevel(F32 level)
{
	gbcnDebugState.dijkstraDist = level;
}

void beaconDebugEnable(int enable)
{
	gbcnDebugState.debugEnabled = !!enable;
}

bool beaconDebugIsEnabled(void)
{
	return gbcnDebugState.debugEnabled;
}

void beaconSetMessageCallback(BcnMsgCB cb)
{
	gbcnDebugState.callback = cb;
}

AUTO_COMMAND;
void beaconSetDebugBeacon(int id, Vec3 pos)
{
	if(id>=0 && id<BEACON_MAX_DBG_IDS)
		copyVec3(pos, beacon_state.debugBeaconPos[id]);
}

#if !PLATFORM_CONSOLE

AUTO_COMMAND;
void beaconDebugPrintInfo(void)
{
	beaconPrintDebugInfo();
}

#endif

#include "AutoGen/beaconDebug_h_ast.c"