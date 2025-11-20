#pragma once
GCC_SYSTEM

#ifndef BEACONDEBUG_H
#define BEACONDEBUG_H

//#include "entity.h"

typedef struct Beacon Beacon;
typedef struct BeaconBlock BeaconBlock;
typedef struct DebugBeacon DebugBeacon;
typedef struct Packet Packet;
typedef struct ZoneMap ZoneMap;
typedef struct StashTableImp StashTableImp;
typedef struct StashTableImp* StashTable;
typedef U32	EntityRef;

typedef void (*BcnMsgCB)(int totalBcns, int numBcns);

typedef enum BeaconDebugFlags
{
	bdBeacons = 1 << 0,
	bdGround = 1 << 1,
	bdRaised = 1 << 2,
	bdNearby = 1 << 3,
	bdSubblock = 1 << 4,
	bdGalaxy = 1 << 5,
	bdCluster = 1 << 6,
} BeaconDebugFlags;

typedef struct BeaconDynConnChange {
	Beacon *src;
	Beacon *dst;
	int raised;
	int enabled;
	int partitionIdx;
} BeaconDynConnChange;

AUTO_STRUCT;
typedef struct BeaconSubBlockChange {
	int partitionIdx;		NO_AST
	int bcn;
	int block;
} BeaconSubBlockChange;

AUTO_STRUCT;
typedef struct BeaconSubBlockCreateChange {
	int partitionIdx;		NO_AST
	int blockIdx;
} BeaconSubBlockCreateChange;

AUTO_STRUCT;
typedef struct BeaconSubBlockDestroyChange {
	int partitionIdx;		NO_AST
	int blockIdx;
} BeaconSubBlockDestroyChange;

AUTO_STRUCT;
typedef struct BeaconGalaxyChange {
	int partitionIdx;		NO_AST
	int galaxySet;
	int blockIdx;
	int galaxyIdx;
} BeaconGalaxyChange;

AUTO_STRUCT;
typedef struct BeaconGalaxyCreateChange {
	int partitionIdx;		NO_AST
	int galaxySet;
	int galaxyIdx;
} BeaconGalaxyCreateChange;

AUTO_STRUCT;
typedef struct BeaconGalaxyDestroyChange {
	int partitionIdx;		NO_AST
	int galaxySet;
	int galaxyIdx;
} BeaconGalaxyDestroyChange;

typedef struct BeaconDebugState
{
	U32 debugEnabled : 1;
	U32 debugReceived : 1;
	F32 dijkstraDist;

	U32 flags;
	DebugBeacon *lastclosest;
	BeaconDynConnChange **dynChanges;
	BeaconGalaxyCreateChange **galaxyCreates;
	BeaconGalaxyDestroyChange **galaxyDestroys;
	BeaconSubBlockCreateChange **subblockCreates;
	BeaconSubBlockDestroyChange **subblockDestroys;
	BeaconGalaxyChange **galaxyChanges;
	BeaconSubBlockChange **blockChanges;
	StashTable blockColors;

	BcnMsgCB callback;
} BeaconDebugState;

typedef struct BcnDebugger {
	F32 KBpf; 
	EntityRef ref;
	int partitionIdx;

	Vec3 pos;
	F32 max_dist;
	int bcnsSent;
	int gridBlocksSent;
	int galaxyLevel;
	int galaxiesSent;
	int *connsSent;
	int *blockConnsSent;
	U32 max_pkt_size;
	F32 last_updated;

	U32		beacon_send		: 1;
	U32		initial_send	: 1;
} BcnDebugger;

AUTO_STRUCT;
typedef struct BeaconDebugPathNode {
	Vec3 pos;
	int type;
} BeaconDebugPathNode;

AUTO_STRUCT;
typedef struct BeaconDebugPath {
	BeaconDebugPathNode **nodes;
} BeaconDebugPath;

AUTO_STRUCT;
typedef struct BeaconDebugInfo {
	BeaconDebugPath path;
	Vec3 endPos;
	Vec3 startPos;
	F32 pathJumpHeight;
	F32 entHeight;

	U32 sendPath : 1;
} BeaconDebugInfo;

typedef enum BeaconDebugMsg {
	BDMSG_RESET = 1,
	BDMSG_DATA,
	BDMSG_POS,
} BeaconDebugMsg;

typedef enum BeaconDebugDataOrigin {
	BDO_GEN,
	BDO_COMBAT,
	BDO_WALK,
	BDO_PRUNE,
	BDO_PRE_REBUILD,
	BDO_POST_REBUILD,
	BDO_MAX,
} BeaconDebugDataOrigin;

#define SEND_LINE_VEC3(po1, po2, color)		beaconSendLine(pak, vecParamsXYZ(po1), vecParamsXYZ(po2), color, 0)
#define SEND_LINE_XYZ(x1, y1, z1, x2, y2, z2, color)	beaconSendLine(pak, x1, y1, z1, x2, y2, z2, color, 0)

#define SEND_POINT(po, color)	beaconSendPoint(pak, vecParamsXYZ(po), color);

extern ParseTable* debugptis[];
extern BcnDebugger **bcnDebuggers;

typedef struct ClientLink		ClientLink;

//void	beaconCmd(Entity* ent, char* params);

void	beaconPrintDebugInfo(void);

void	beaconGotoCluster(ClientLink* client, int index);
int		beaconGotoNPCCluster(ClientLink* client, int num);
                                                      
int		beaconGetDebugVar(char* var);
int		beaconSetDebugVar(char* var, int value);

// This does all the magical drawing!
typedef void (*BeaconDrawLineFunc)(Vec3 world_src, int color_src, Vec3 world_dst, int color_dst, void* userdata);
typedef void (*BeaconDrawSphereFunc)(Vec3 world, F32 radius, int color, void* userdata);
void	beaconDrawDebug(BeaconDebugInfo *info, Vec3 pos, BeaconDrawLineFunc func, void* linedata, BeaconDrawSphereFunc spherefunc, void* spheredata);

void	beaconSendLine(Packet* pak, F32 x1, F32 y1, F32 z1, F32 x2, F32 y2, F32 z2, U32 colorARGB1, U32 colorARGB2);
void	beaconSendPoint(Packet *pak, F32 x, F32 y, F32 z, U32 colorARGB);

// Dbginfo is automagically sent to the client.  Pkt is for manual information.  It should be inited and set to
// TOCLIENT_DEBUG_BEACONSTUFF
void	beaconSetDebugInfo(Packet* pkt);

void	beaconDebugDynConnChange(int partitionIdx, Beacon *b, Beacon *d, int raised, int enabled);
void	beaconDebugSubBlockChange(int partitionIdx, Beacon* b, BeaconBlock *block);
void	beaconDebugGalaxyCreate(int partitionIdx, BeaconBlock *galaxy);
void	beaconDebugGalaxyDestroy(int partitionIdx, BeaconBlock *destroy, BeaconBlock *merged);
void	beaconDebugSubBlockCreate(int partitionIdx, BeaconBlock *subblock);
void	beaconDebugSubBlockDestroy(int partitionIdx, BeaconBlock *destroy, BeaconBlock *merged);

void	beaconDebugToggleFlag(BeaconDebugFlags flag);
void	beaconDebugSetFlag(BeaconDebugFlags flag);
void	beaconDebugSetLevel(F32 level);
void	beaconDebugEnable(int enable);
bool	beaconDebugIsEnabled(void);

void	beaconDebugUpdateDebugger(BcnDebugger *dbger, Packet *pkt, F32 timeElapsed);
void	beaconDebugFrameEnd(void);
void	beaconSetMessageCallback(BcnMsgCB cb);
void	beaconDebugMapLoadWorldClient(ZoneMap *map);
void	beaconDebugMapUnloadWorldClient(void);

#ifndef GAMESERVER
void	beaconHandleDebugMsg(Packet* pkt);
#endif

#endif