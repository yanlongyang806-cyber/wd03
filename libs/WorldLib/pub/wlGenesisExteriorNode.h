#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "WorldLibEnums.h"
#include "wlGenesis.h"
#include "referencesystem.h"

typedef struct GenesisBackdrop GenesisBackdrop;
typedef struct GenesisDetailKit GenesisDetailKit;
typedef struct GenesisEcosystem GenesisEcosystem;
typedef struct GenesisGeotype GenesisGeotype;
typedef struct GenesisGeotypeNodeData GenesisGeotypeNodeData;
typedef struct GenesisMissionRequirements GenesisMissionRequirements;
typedef struct GenesisNode GenesisNode;
typedef struct GenesisObject GenesisObject;
typedef struct GenesisRoomMission GenesisRoomMission;
typedef struct GenesisTerrainAttribute GenesisTerrainAttribute;
typedef struct GenesisToPlaceObject GenesisToPlaceObject;
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct GroupDef GroupDef;
typedef struct ModelLOD ModelLOD;
typedef struct Spline Spline;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct TerrainTaskQueue TerrainTaskQueue;

#define GENESIS_DEFAULT_ROAD_RADIUS 75.f
#define GENESIS_ROAD_RADIUS_VARIANCE 0.2f

AUTO_ENUM;
typedef enum GenesisConstraintFuncName {
	GCF_MinDist=0,
	GCF_MaxDist,
	GCF_MaxVertAngleDiff,
	GCF_IsBetween,
	GCF_OtherSideOf,
	GCF_Connected,
	GCF_GlobalHeight,
	GCF_Road,
	GCF_VistaBox,
	GCF_Uphill,
} GenesisConstraintFuncName;

AUTO_ENUM;
typedef enum GenesisParamType
{
	GENESIS_PARAM_Dist, //F32
	GENESIS_PARAM_Angle, //F32
	GENESIS_PARAM_Start, // Room/Node Ref
	GENESIS_PARAM_Mid, // Room/Node Ref
	GENESIS_PARAM_End, // Room/Node Ref
	GENESIS_PARAM_COUNT,
} GenesisParamType;

AUTO_ENUM;
typedef enum GenesisNodeType
{
	GENESIS_NODE_Clearing = 1,
	GENESIS_NODE_SideTrail,
	GENESIS_NODE_Nature,
	GENESIS_NODE_OffMap,
} GenesisNodeType;

AUTO_STRUCT;
typedef struct GenesisConstraintParam
{
	GenesisParamType name;	AST( NAME("Name"))
	char *value;			AST( NAME("Value") )
} GenesisConstraintParam;
extern ParseTable parse_GenesisConstraintParam[];
#define TYPE_parse_GenesisConstraintParam GenesisConstraintParam

typedef struct GenesisParamValues
{
	bool invert;
	F32 angle;
	F32 dist;
	F32 min;
	F32 max;
	GenesisNode *start;
	GenesisNode *mid;
	GenesisNode *end;
	GenesisNode *side1;
	GenesisNode *side2;

} GenesisParamValues;

AUTO_STRUCT;
typedef struct GenesisRoomConstraint
{
	GenesisConstraintFuncName function;				AST( NAME("Type") )
	GenesisConstraintParam **params;				AST( NAME("Param"))
} GenesisRoomConstraint;
extern ParseTable parse_GenesisRoomConstraint[];
#define TYPE_parse_GenesisRoomConstraint GenesisRoomConstraint

AUTO_STRUCT;
typedef struct GenesisNodeConstraint
{
	GenesisConstraintFuncName function;				AST( NAME("Type") )
	GenesisParamValues values;						NO_AST
} GenesisNodeConstraint;
extern ParseTable parse_GenesisNodeConstraint[];
#define TYPE_parse_GenesisNodeConstraint GenesisNodeConstraint

AUTO_STRUCT;
typedef struct GenesisNodeObject
{
	U32 seed;										AST(NAME("Seed"))
	U32 actual_size;								AST(NAME("ActualSize"))
	U32 draw_size;									AST(NAME("DrawSize"))
	F32 offset;										AST(NAME("Offset"))
	int path_idx;									AST(NAME("PathIdx"))
	GenesisObject *object;							AST(NAME("StaticObject"))
} GenesisNodeObject;
extern ParseTable parse_GenesisNodeObject[];
#define TYPE_parse_GenesisNodeObject GenesisNodeObject

AUTO_STRUCT;
typedef struct GenesisNodePatrol
{
	int path_idx;									AST(NAME("PathIdx"))
	GenesisPatrolObject *patrol;					AST(NAME("Patrol"))
} GenesisNodePatrol;
extern ParseTable parse_GenesisNodePatrol[];
#define TYPE_parse_GenesisNodePatrol GenesisNodePatrol

AUTO_STRUCT;
typedef struct GenesisNodeMission
{
	char *mission_name;								AST(NAME("MissionName"))
	GenesisNodeObject **objects;					AST(NAME("Object"))
	GenesisNodePatrol **patrols;					AST(NAME("Patrol"))
} GenesisNodeMission;
extern ParseTable parse_GenesisNodeMission[];
#define TYPE_parse_GenesisNodeMission GenesisNodeMission

AUTO_STRUCT;
typedef struct GenesisNodePathPoint
{
	F32 offset;										AST(NAME("Offset"))
	F32 radius;										AST(NAME("Radius"))
	Vec3 rel_pos;									AST(NAME("RelativePosition"))
} GenesisNodePathPoint;
extern ParseTable parse_GenesisNodePathPoint[];
#define TYPE_parse_GenesisNodePathPoint GenesisNodePathPoint

AUTO_STRUCT AST_IGNORE_STRUCT(TerrainType);
typedef struct GenesisNodeConnection
{
	U32 start_uid;									AST(NAME("Start"))
	U32 end_uid;									AST(NAME("End"))
	F32 radius;										AST(NAME("Radius"))
	GenesisNodePathPoint **path_points;				AST(NAME("PathPoint"))
	Spline *path;									AST(NAME("SplinePath"))
	U8 to_off_map : 1;								AST(NAME("ToOffMap"))
	F32 min_length;									NO_AST
	F32 max_length;									NO_AST
} GenesisNodeConnection;
extern ParseTable parse_GenesisNodeConnection[];
#define TYPE_parse_GenesisNodeConnection GenesisNodeConnection

AUTO_STRUCT;
typedef struct GenesisNodeConnectionGroup
{
	char *name;										AST(NAME("Name"))
	GenesisNodeConnection **connections;			AST(NAME("Connection"))
	U8 side_trail : 1;								AST(NAME("SideTrail"))
	GenesisNodeMission **missions;					AST(NAME("Mission"))
	GenesisRuntimeErrorContext* source_context;	AST(NAME("SourceContext"))
} GenesisNodeConnectionGroup;
extern ParseTable parse_GenesisNodeConnectionGroup[];
#define TYPE_parse_GenesisNodeConnectionGroup GenesisNodeConnectionGroup

AUTO_STRUCT;
typedef struct GenesisNodeBorder
{
	bool horiz;										AST(NAME("Horizontal"))//Otherwise Vertical
	Vec3 start;										AST(NAME("Start"))
	Vec3 end;										AST(NAME("End"))
	F32 step;										AST(NAME("Step"))
	F32 *heights;									AST(NAME("Heights"))
}GenesisNodeBorder;
extern ParseTable parse_GenesisNodeBorder[];
#define TYPE_parse_GenesisNodeBorder GenesisNodeBorder

AUTO_STRUCT AST_IGNORE(BoarderType) AST_IGNORE_STRUCT(TerrainType) AST_IGNORE(NonPlayArea) AST_IGNORE(SideTrail) AST_IGNORE_STRUCT(PriorityObject) AST_IGNORE_STRUCT(LightDetail);
typedef struct GenesisNode
{
	char *name;										AST(NAME("Name"))
	U32 seed;										AST(NAME("Seed"))
	U32 uid;										AST(NAME("UID"))
	U32 off_map_uid;								AST(NAME("OffMapUID"))  //If type is out of bounds, then uid of paired node
	Vec3 pos;										AST(NAME("Position"))
	U32 actual_size;								AST(NAME("ActualSize"))
	U32 draw_size;									AST(NAME("DrawSize"))
	GenesisNodeType node_type;						AST(NAME("NodeType"))
	GenesisObject **static_objects;					AST(NAME("Object"))
	GenesisRoomMission **missions;					AST(NAME("Mission"))		// Per-mission object placement data
	GenesisDetailKitAndDensity detail_kit_1;		AST(EMBEDDED_FLAT)			
	GenesisDetailKitAndDensity detail_kit_2;		AST(NAME("Detail2"))			
	U8 do_not_pop : 1;								AST(NAME("DoNotPopulate"))
	GenesisRuntimeErrorContext* source_context;	AST(NAME("SourceContext"))

	GenesisNodeConstraint **final_constraints;		NO_AST
	GenesisNode **parents;							NO_AST
	bool placed;									NO_AST
	GenesisNode *nature_parent;						NO_AST
	void *user_data;								NO_AST
	GenesisNode *prev_node;							NO_AST
	U32 priority;									NO_AST
}GenesisNode;
extern ParseTable parse_GenesisNode[];
#define TYPE_parse_GenesisNode GenesisNode

#ifndef NO_EDITORS

GenesisZoneNodeLayout *genesisCreateNodeLayoutFromWorld();

void genesisNodesFixup(GenesisZoneNodeLayout *layout);
void genesisDoNodesToDesign(TerrainEditorSource *source, GenesisZoneNodeLayout *layout);
void genesisMoveNodesToDesign(TerrainTaskQueue *queue, TerrainEditorSource *source, GenesisZoneNodeLayout *layout, int flags);
void genesisMoveNodesToDesign_Temp(TerrainTaskQueue *queue, TerrainEditorSource *source, GenesisGeotype *geotype, int flags);
void genesisNodeConstraintDestroy(GenesisNodeConstraint *constaint);
void genesisNodeDestroy(GenesisNode *constaint);
void genesisNodesLayoutDestroy(GenesisZoneNodeLayout *node_layout);

bool genesisNodeDoObjectPlacement(int iPartitionIdx, GenesisZoneMapData *genesis_data, GenesisToPlaceState *to_place, GenesisZoneNodeLayout *layout, GenesisMissionRequirements **mission_reqs, bool detail, bool no_sharing);

void genesisMakeNodeBorders(GenesisZoneNodeLayout *dest, GenesisZoneNodeLayout *src, Vec2 min_pos, Vec2 max_pos);
void genesisCenterNodesInsideBorders(GenesisZoneNodeLayout *layout);

GenesisNodeConstraint* genesisMakeMinDistConstraint(GenesisNode *target1, GenesisNode *target2, F32 dist);
GenesisNodeConstraint* genesisMakeMaxDistConstraint(GenesisNode *target1, GenesisNode *target2, F32 dist);
GenesisNodeConstraint* genesisMakeAngleConstraint(GenesisNode *target1, GenesisNode *target2, F32 angle);
//GenesisNodeConstraint* genesisMakeIsBetweenConstraint(GenesisNode *target1, GenesisNode *target2);

void genesisPlaceNodes(GenesisNode **node_list, GenesisNodeConstraint **constraints);

int genesisGetNodeIndex(GenesisZoneNodeLayout *layout, const char *name);
GenesisNode* genesisGetNodeFromUID(GenesisZoneNodeLayout *layout, U32 uid);

#endif
