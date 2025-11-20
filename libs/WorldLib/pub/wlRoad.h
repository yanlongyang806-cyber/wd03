#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _WLROAD_H_
#define _WLROAD_H_

#if 0

#include "referencesystem.h"

#include "wlCurve.h"
#include "wlCurveScripts.h"

typedef struct HeightMap HeightMap;
typedef struct DictionaryEArrayStruct DictionaryEArrayStruct;

#define ROAD_DATA		TOK_USEROPTIONBIT_1

// Set of parameters for roads system

AUTO_STRUCT;
typedef struct RoadTemplateName
{
	char *name;		AST( STRUCTPARAM )
} RoadTemplateName;

AUTO_STRUCT;
typedef struct RoadTemplateList
{
	RoadTemplateName **templates;
} RoadTemplateList;

AUTO_STRUCT AST_IGNORE(road_ecotype) AST_IGNORE(sidewalk_ecotype);
typedef struct RoadSystemParameters
{
	char* name;									AST(KEY)
	const char* filename;						AST(CURRENTFILE)

	F32		road_dig_height;

	char*	road_template;
	char*	signpost_template;
	char*	traffic_post_template;
	char*	traffic_lamp_template;
	char*	street_sign_texture;
	char*	street_sign_template;
	Vec3	street_sign_offset;
	char*	traffic_pole_template;
	char*	traffic_long_pole_template;
	char*	traffic_mount_template;
	Vec3	traffic_mount_offset;
	Vec3	traffic_mount_long_offset;
	char*	traffic_light_template;
	char*	traffic_crosswalk_mount_template;
	char*	traffic_crosswalk_A_template;
	Vec3	traffic_crosswalk_A_offset;
	char*	traffic_crosswalk_B_template;
	Vec3	traffic_crosswalk_B_offset;
	char*	crosswalk_template;
	char*	crosswalk_wide_template;
	char*	sidewalk_10ft_template;
	char*	sidewalk_20ft_template;
	char*	sidewalk_corner_template;
	char*	streetlight_template;
	char*	signs_one_way_A_template;
	char*	signs_one_way_B_template;
	char*	stripe_white_broken_template;
	char*	stripe_yellow_broken_template;
	char*	stripe_yellow_template;
	char*	parking_space_template;
	char*	no_parking_template;
	char*	fire_hydrant_template;
	char*	bus_stop_begin_template;
	char*	bus_stop_end_template;
	char*	tree_dirt_notree_template;
	char*	tree_grate_notree_template;
	char*	tree_dirt_template;
	char*	tree_grate_cage_template;
	char*	tree_grate_template;
	char*	block_filler_standard_template;
	char*	block_filler_slums_template;
	char*	block_filler_fancy_template;
} RoadSystemParameters;

AUTO_STRUCT;
typedef struct RoadDisplayParameter
{
	char *name;
	char *value;
	bool inherited;
	char *inherited_value;
} RoadDisplayParameter;

// Info for drawing into the terrain
AUTO_STRUCT;
typedef struct RoadSegmentInfo
{
	int segment_idx;
	Spline *spline;
	bool has_road_tint;
	Color road_tint;
	bool has_sidewalk_tint;
	Color sidewalk_tint;
	F32 width;
	F32 cut_width;
	F32 dig_height;
} RoadSegmentInfo;

extern ParseTable parse_RoadSegmentInfo[];
#define TYPE_parse_RoadSegmentInfo RoadSegmentInfo

AUTO_STRUCT;
typedef struct RoadDigInfo
{
	RoadSegmentInfo **road_segments;
	RoadSegmentInfo **intersection_segments;
} RoadDigInfo;

AUTO_STRUCT;
typedef struct RoadIntersectionConnector
{
	char *logical_name;
	char *attachment_1_name;
	char *attachment_2_name;
} RoadIntersectionConnector;

AUTO_STRUCT;
typedef struct RoadIntersection
{
	int							parent_idx;			AST( KEY )
	bool						updated;

	bool						reverse;			AST(USERFLAG(ROAD_DATA)) // Rotate the intersection 180 degrees along primary road
	int							active_connector;	AST(USERFLAG(ROAD_DATA)) // Index in the connectors list of the primary crossing road
	RoadIntersectionConnector	**connectors;		AST(USERFLAG(ROAD_DATA)) // Connector names to match by
	char						*template_name;		AST(USERFLAG(ROAD_DATA)) // Template to apply

	U32							mod_time;			NO_AST
} RoadIntersection;

AUTO_STRUCT;
typedef struct DisplayIntersection
{
	char *name;
	char *template_name;
	Vec3 position;								NO_AST
	CurveScriptNode *node;						NO_AST
	RoadIntersection *intersection;
} DisplayIntersection;

#define ROAD_GAP_ROAD			(1<<0)
#define ROAD_GAP_L_SIDEWALK		(1<<1)
#define ROAD_GAP_R_SIDEWALK		(1<<2)
#define ROAD_GAP_L_BUILDING		(1<<3)
#define ROAD_GAP_R_BUILDING		(1<<4)
#define ROAD_GAP_ALL			(ROAD_GAP_ROAD | ROAD_GAP_L_SIDEWALK | ROAD_GAP_R_SIDEWALK | ROAD_GAP_L_BUILDING | ROAD_GAP_R_BUILDING)


AUTO_STRUCT;
typedef struct Road
{
	int					parent_idx;			AST( KEY )
	bool				updated;

	char				*logical_name;		AST(USERFLAG(ROAD_DATA))
	char				*visible_name;		AST(USERFLAG(ROAD_DATA))
	int					*segment_idxs;		AST(USERFLAG(ROAD_DATA))		// indexes of RoadSegment children in list

	U32					mod_time;			NO_AST
	RoadSegment			**segments;			NO_AST
} Road;

AUTO_STRUCT;
typedef struct RoadList
{
	RoadSegment			**road_segments;
	RoadIntersection	**road_intersections; // Overrides for the defaults
	Road				**roads;
	RoadDigInfo			road_dig_info;			NO_AST // regenerated from source
} RoadList;

AUTO_STRUCT;
typedef struct RoadPointObject {
	Road *road;
	RoadSegment *segment;
	int index;
} RoadPointObject;

AUTO_STRUCT;
typedef struct RoadGapObject {
	RoadSegment *parent;
	Vec3 position;
	RoadGap *gap;
} RoadGapObject;

extern ParseTable parse_RoadDisplayParameter[];
#define TYPE_parse_RoadDisplayParameter RoadDisplayParameter
extern ParseTable parse_RoadSegment[];
#define TYPE_parse_RoadSegment RoadSegment
extern ParseTable parse_Road[];
#define TYPE_parse_Road Road
extern ParseTable parse_RoadGap[];
#define TYPE_parse_RoadGap RoadGap
extern ParseTable parse_RoadList[];
#define TYPE_parse_RoadList RoadList
extern ParseTable parse_DisplayIntersection[];
#define TYPE_parse_DisplayIntersection DisplayIntersection
extern ParseTable parse_RoadIntersection[];
#define TYPE_parse_RoadIntersection RoadIntersection
extern ParseTable parse_RoadIntersectionConnector[];
#define TYPE_parse_RoadIntersectionConnector RoadIntersectionConnector

extern ParseTable parse_RoadSystemParameters[];
#define TYPE_parse_RoadSystemParameters RoadSystemParameters
extern ParseTable parse_RoadPointObject[];
#define TYPE_parse_RoadPointObject RoadPointObject
extern ParseTable parse_RoadGapObject[];
#define TYPE_parse_RoadGapObject RoadGapObject


void roadsInitialize(void);
void roadsDoDigRoadsHeightmap(RoadDigInfo *dig_info, HeightMap *heightmap);
void roadsUpdateAll(RoadSystemParameters *params, RoadSegment **roads, RoadIntersection **road_intersections, RoadDigInfo *dig_info, CurveList *dest_curve_list);
RoadSegment *roadsSegmentBinarySearch(RoadSegment **list, int index);
Road *roadsBinarySearch(Road **list, int index);
void roadsFixupPointers(RoadList *list);
void roadsOncePerFrameClient();
RoadIntersection *roadsFindIntersection(RoadIntersection **road_intersections, CurveScriptNode *node);


// Road parameters library
void roadParamReloadLibrary(void);
DictionaryEArrayStruct *roadsGetParameterList();
RoadSystemParameters *roadsGetParametersByName(const char *name);

#endif

#endif // _WLROAD_H_
