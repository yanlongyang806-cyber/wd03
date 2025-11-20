#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "referencesystem.h"
#include "WorldLibStructs.h"
#include "WorldLibEnums.h"
#include "wlGenesis.h"

typedef struct DoorTransitionSequenceDef    DoorTransitionSequenceDef;
typedef struct GenesisBackdrop				GenesisBackdrop;
typedef struct GenesisEcosystem				GenesisEcosystem;
typedef struct GenesisToPlaceObject			GenesisToPlaceObject;
typedef struct GroupChild					GroupChild;
typedef struct GroupDef						GroupDef;
typedef struct GroupDefRef					GroupDefRef;
typedef struct GenesisMissionRequirements	GenesisMissionRequirements;
typedef struct SolarSystemSunsInfo			SolarSystemSunsInfo;
typedef struct Spline						Spline;
typedef struct WorldDebrisFieldProperties	WorldDebrisFieldProperties;
typedef struct WorldPlanetProperties		WorldPlanetProperties;
typedef struct ZoneMap						ZoneMap;
typedef struct ZoneMapLayer					ZoneMapLayer;
typedef struct GenesisZoneMapInfo			GenesisZoneMapInfo;
typedef struct GenesisRuntimeErrorContext	GenesisRuntimeErrorContext;
typedef struct GenesisLayoutCommonData		GenesisLayoutCommonData;
typedef struct GroupDefLib					GroupDefLib;
typedef struct UGCGenesisBackdropSun		UGCGenesisBackdropSun;
#define SOLAR_SYSTEM_ENCOUNTER_DIST 1250.0f

//////////////////////////////////////////////////////////////////////////
// Structures shared between phases
//////////////////////////////////////////////////////////////////////////

AUTO_ENUM;
typedef enum PointListFacingDirection {
	PLFD_Random=0,
	PLFD_Parent,
} PointListFacingDirection;
extern StaticDefineInt PointListFacingDirectionEnum[];

AUTO_ENUM;
typedef enum ShoeboxPointListType {
	SBLT_ZigZag=0,
	SBLT_Orbit,
} ShoeboxPointListType;
extern StaticDefineInt ShoeboxPointListTypeEnum[];

AUTO_STRUCT;
typedef struct SSOffset {
	Vec3 offset_min;							AST(NAME("OffsetMin"))
	Vec3 offset_max;							AST(NAME("OffsetMax"))
	bool detached;								AST(NAME("Detached"))//Forces not to be on a curve
	F32 min_dist;								AST(NAME("MinDist"))
	F32 max_dist;								AST(NAME("MaxDist"))
	int min_count;								AST(NAME("MinCount"))//Only used during transmogrify, only in some places.
	int max_count;								AST(NAME("MaxCount")) 
	Vec3 pos;									NO_AST
	F32 radius;									NO_AST
} SSOffset;
extern ParseTable parse_SSOffset[];
#define TYPE_parse_SSOffset SSOffset

AUTO_STRUCT;
typedef struct SSLibObj {
	// exactly one of these should be filled out
	GroupDefRef obj;							AST(EMBEDDED_FLAT)
	REF_TO(DoorTransitionSequenceDef) start_spawn_using_transition; AST(NAME("StartSpawnUsingTransition"))

	GenesisChallengeType challenge_type;		AST(NAME("ChallengeType"))
	char *challenge_name;						AST(NAME("ExternalName"))
	int challenge_id;							AST(NAME("ChallengeID"))
	SSOffset offset;							AST(NAME("Offset"))
	GenesisSpacePatrolType patrol_type;			AST(NAME("PatrolType"))
	char *patrol_ref_name;						AST(NAME("PatrolRefName"))
	GroupDef *cached_def;						NO_AST

	GenesisRuntimeErrorContext* source_context; AST(NAME("SourceContext"))
} SSLibObj;
extern ParseTable parse_SSLibObj[];
#define TYPE_parse_SSLibObj SSLibObj

AUTO_STRUCT AST_FIXUPFUNC(fixupSSTagObj);
typedef struct SSTagObj {
	char *oldTags;								AST(STRUCTPARAM)
	char **tags;								AST(NAME("Tags"))
	SSOffset offset;							AST(NAME("Offset"))
} SSTagObj;
extern ParseTable parse_SSTagObj[];
#define TYPE_parse_SSTagObj SSTagObj

AUTO_STRUCT;
typedef struct SSClusterObject {
	SSLibObj lib_obj;							AST(EMBEDDED_FLAT)
	int count;									AST(NAME("Count"))
	int spawn_count;							AST(NAME("SpawnCount"))
} SSClusterObject;
extern ParseTable parse_SSClusterObject[];
#define TYPE_parse_SSClusterObject SSClusterObject

AUTO_STRUCT;
typedef struct SSCluster {
	SSClusterObject **cluster_objects;			AST(NAME("ClusterObject"))
	F32 height;									AST(NAME("Height"))
	F32 radius;									AST(NAME("Radius"))
	F32 min_dist;								AST(NAME("MinDist"))
	F32 max_dist;								AST(NAME("MaxDist"))
} SSCluster;
extern ParseTable parse_SSCluster[];
#define TYPE_parse_SSCluster SSCluster

AUTO_STRUCT;
typedef struct SSObjSet {
	int mission_uid;							AST(NAME("MissionUID"))  //Set for missions only
	char *mission_name;							AST(NAME("MissionName"))
	SSLibObj **group_refs;						AST(NAME("LibObject"))	//Object Library pieces 
	SSTagObj **object_tags;						AST(NAME("ObjectTags"))	//Transmog only
	SSCluster *cluster;							AST(NAME("Cluster"))	//we could possibly add these to the GroupDef at some point
	bool has_portal;							AST(NAME("HasPortal"))
} SSObjSet;
extern ParseTable parse_SSObjSet[];
#define TYPE_parse_SSObjSet SSObjSet

AUTO_STRUCT AST_IGNORE("DoNotScale");
typedef struct SolarSystemSuns
{
	SSLibObj **suns;							AST(NAME("Sun"), ADDNAMES("LibObject"))
} SolarSystemSuns;

AUTO_STRUCT;
typedef struct SolarSystemSunsInfo
{
	SolarSystemSuns *sun_list;					AST(NAME("SunList"), ADDNAMES("FarSystemRep"))
	//Orbit of planets around the suns
	F32 oribit_min;								AST(NAME("OrbitMin"))
	F32 oribit_max;								AST(NAME("OrbitMax"))
} SolarSystemSunsInfo;

AUTO_STRUCT;
typedef struct SpaceBackdropLight
{
	Vec3 light_diffuse_hsv;						AST(NAME("LightDiffuseHSV"))
	Vec3 light_secondary_diffuse_hsv;			AST(NAME("LightSecondaryDiffuseHSV"))
	Vec3 light_specular_hsv;					AST(NAME("LightSpecularHSV"))
	Vec3 light_ambient_hsv;						AST(NAME("LightAmbientHSV"))
	bool cast_shadows;							AST(NAME("CastShadows")) // Currently UGC only
} SpaceBackdropLight;
extern ParseTable parse_SpaceBackdropLight[];
#define TYPE_parse_SpaceBackdropLight SpaceBackdropLight

AUTO_STRUCT;
typedef struct ShoeboxPoint {
	char *name;									AST(NAME("Name")) //Name to be used in Mission Desc File
	SSObjSet *point_rep;						AST(NAME("Rep")) //If in GenesisSolSysZoneMap then this is the actual objects to place
	SSObjSet **missions;						AST(NAME("Mission"))
	F32 dist_from_last;							AST(NAME("DistFromLast"))
	//Cluster Params
	F32 radius;									AST(NAME("Radius"))
	F32 min_dist;								AST(NAME("MinDist"))
	F32 max_dist;								AST(NAME("MaxDist"))

	PointListFacingDirection face_dir;			AST(NAME("FacingDir"))//Otherwise use this direction
	F32 face_offset;							AST(NAME("FacingOffset"))

	Vec3 pos;									NO_AST
	Vec3 dir;									NO_AST
	bool pos_offset_right;						NO_AST
} ShoeboxPoint;
extern ParseTable parse_ShoeboxPoint[];
#define TYPE_parse_ShoeboxPoint ShoeboxPoint

AUTO_STRUCT;
typedef struct ShoeboxPointList {

	U8 equidist			: 1;					AST(NAME("Equidist"))
	U8 follow_points	: 1;					AST(NAME("FollowPoints"))

	char *name;									AST(NAME("Name"))
	ShoeboxPointListType list_type;				AST(NAME("Type"))
	SSObjSet *orbit_object;						AST(NAME("OrbitObject"))
	F32 min_rad;								AST(NAME("MinRad"))//Min radius of orbit
	F32 max_rad;								AST(NAME("MaxRad"))//Max radius of orbit
	F32 min_tilt;								AST(NAME("MinTilt"))//Min tilt of orbit, or pitch of zig/zag
	F32 max_tilt;								AST(NAME("MaxTilt"))//Max tilt of orbit, or pitch of zig/zag
	F32 min_yaw;								AST(NAME("MinYaw"))//Min yaw diff on entry
	F32 max_yaw;								AST(NAME("MaxYaw"))//Max yaw diff on entry
	F32 min_horiz;								AST(NAME("MinHoriz"))//Min horizontal distance difference
	F32 max_horiz;								AST(NAME("MaxHoriz"))//Min horizontal distance difference
	F32 min_vert;								AST(NAME("MinVert"))//Min vertical distance difference
	F32 max_vert;								AST(NAME("MaxVert"))//Max vertical distance difference
	SSLibObj **curve_objects;					AST(NAME("CurveObject"))
	SSTagObj **curve_objects_tags;				AST(NAME("CurveObjectTags"))

	ShoeboxPoint **points;						AST(NAME("Point"))

	Spline *spline;								NO_AST
	Mat4 orbit_mat;								NO_AST
	F32 orbit_radius;							NO_AST
} ShoeboxPointList;
extern ParseTable parse_ShoeboxPointList[];
#define TYPE_parse_ShoeboxPointList ShoeboxPointList

//////////////////////////////////////////////////////////////////////////
// Structs used at the Zone Map Phase
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GenesisShoebox
{
	ShoeboxPointList **point_lists;				AST(NAME("PointList"))
	SSLibObj **detail_objects;					AST(NAME("DetailObject"))

	Vec3 overview_pos;							//Filled in when zmap is binned
	Vec3 layer_center;							NO_AST
	Vec3 layer_min;								NO_AST
	Vec3 layer_max;								NO_AST
	Vec3 search_min;							NO_AST
	Vec3 search_max;							NO_AST
	int detail_placed;							NO_AST
} GenesisShoebox;
extern ParseTable parse_GenesisShoebox[];
#define TYPE_parse_GenesisShoebox GenesisShoebox

//This would be embedded inside a zone map
AUTO_STRUCT;
typedef struct GenesisSolSysZoneMap
{
	char *layout_name;							AST(NAME("LayoutName"))
	U32 layout_seed;							AST(NAME("LayoutSeed"))
	U32 tmog_version;							AST(NAME("TransmogrifyVersion"))
	GenesisBackdrop *backdrop;					AST(NAME("Backdrop"))//TODO: review seed sending
	GenesisShoebox shoebox;						AST(NAME("Shoebox"))
	bool no_sharing_detail;						AST(NAME("NoSharingDetail"))
} GenesisSolSysZoneMap;
extern ParseTable parse_GenesisSolSysZoneMap[];
#define TYPE_parse_GenesisSolSysZoneMap GenesisSolSysZoneMap

//////////////////////////////////////////////////////////////////////////
// Structs used at the Just Written Phase
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT AST_IGNORE("Name") AST_IGNORE("OrbitTreeSpecifier");
typedef struct GenesisShoeboxLayout
{
	SSLibObj **detail_objects;					AST(NAME("DetailObject"))
	SSTagObj **detail_objects_tags;				AST(NAME("DetailObjectTags"))
	ShoeboxPointList **point_lists;				AST(NAME("PointList"))
} GenesisShoeboxLayout;

extern ParseTable parse_GenesisShoeboxLayout[];
#define TYPE_parse_GenesisShoeboxLayout GenesisShoeboxLayout

//This would be in the layout file for the just written phase
AUTO_STRUCT AST_FIXUPFUNC(fixupSolarSystemLayout);
typedef struct GenesisSolSysLayout
{
	char *name;									AST(NAME("Name"))
	char *old_environment_tags;					AST(NAME("EnvironmentTags"))
	char **environment_tags;					AST(NAME("EnvironmentTags2"))
	GenesisShoeboxLayout shoebox;				AST(NAME("Shoebox"))
	GenesisLayoutCommonData common_data;		AST(NAME("CommonData"))
} GenesisSolSysLayout;

extern ParseTable parse_GenesisSolSysLayout[];
#define TYPE_parse_GenesisSolSysLayout GenesisSolSysLayout

#ifndef NO_EDITORS

//////////////////////////////////////////////////////////////////////////
// Global Functions
//////////////////////////////////////////////////////////////////////////

void solarSystemCalculateShoeboxPositions(ZoneMap *zmap, GenesisSolSysZoneMap *solar_system, int layer_idx);
void solarSystemPopulateMiniLayer(ZoneMap *zmap, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer, GenesisSolSysZoneMap *solar_system, int layer_idx);
void solarSystemPopulateLayer(ZoneMap *zmap, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer, GenesisSolSysZoneMap *solar_system, int layer_idx);
SSObjSet* solarSystemFindObjSet(SSObjSet** missions, const char* mission_name);

// Shared functions
GroupDef *solarSystemMakeGroupDef(GroupDefLib *def_lib, const char *name);
GenesisToPlaceObject *solarSystemPlaceGroupDef(GroupDef *def, const char *name, const F32 *pos, float fRot, GenesisToPlaceObject *parent_object, GenesisToPlaceState *to_place);
void solarSystemInitVolume(GroupDef *volume_def, Vec3 min, Vec3 max);
void solarSystemCreateCommonObjects(GenesisShoebox *shoebox, GenesisBackdrop *backdrop, const char *layout_name,
									GroupDefLib *def_lib, GenesisToPlaceState *to_place, GenesisMissionRequirements **mission_reqs);

#endif
