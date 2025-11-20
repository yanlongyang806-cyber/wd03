//// UGC custom interior support, cross-game
////
//// This is just enough data to expose the custom room data to the
//// WorldEditor.
#pragma once

#include "ReferenceSystem.h"

typedef struct GroupDef GroupDef;
typedef struct HeightMapExcludeGrid HeightMapExcludeGrid;
typedef struct Message Message;

AUTO_STRUCT;
typedef struct UGCRoomDetailDef
{
	int iChildCount;
	const char *astrParameter;			AST(POOL_STRING)
	const char **eaNames; 				AST(POOL_STRING) // Category, Value, Value, etc.
} UGCRoomDetailDef;
extern ParseTable parse_UGCRoomDetailDef[];
#define TYPE_parse_UGCRoomDetailDef UGCRoomDetailDef

AUTO_STRUCT;
typedef struct UGCRoomPopulateObject
{
	int iGroupUID;						AST(NAME(GroupUID))
	const char* astrGroupDebugName;		AST(NAME(GroupDebugName), POOL_STRING)
	Vec3 vPos;							AST(NAME(Pos))
	F32 fRot;							AST(NAME(Rot))
} UGCRoomPopulateObject;
extern ParseTable parse_UGCRoomPopulateObject[];
#define TYPE_parse_UGCRoomPopulateObject UGCRoomPopulateObject

AUTO_STRUCT;
typedef struct UGCRoomPopulateDef
{
	REF_TO(Message) hDisplayName;		AST(NAME(DisplayName))
	UGCRoomPopulateObject **eaObjects;	AST(NAME(Object))
} UGCRoomPopulateDef;
extern ParseTable parse_UGCRoomPopulateDef[];
#define TYPE_parse_UGCRoomPopulateDef UGCRoomPopulateDef

AUTO_STRUCT;
typedef struct UGCRoomDoorInfo
{
	IVec3 pos;
	F32 rot;
	U32 door_id;
	const char* astrScopePath;			AST(POOL_STRING)
	const char* astrScopeName;			AST(POOL_STRING)
	int *eaiDoorTypeIDs;	
} UGCRoomDoorInfo;
extern ParseTable parse_UGCRoomDoorInfo[];
#define TYPE_parse_UGCRoomDoorInfo UGCRoomDoorInfo

typedef struct UGCRoomInfo
{
	IVec2 footprint_min;
	IVec2 footprint_max;
	U8 *footprint_buf;
	UGCRoomDoorInfo **doors;
	HeightMapExcludeGrid **platform_grids;

	// Detail info
	UGCRoomDetailDef **details;
	UGCRoomPopulateDef **populates;

	const char *astrLevelParameter;
	int iNumLevels;
	int *uLevelGroupIDs;
} UGCRoomInfo;

//////////////////////////////////////////////////////////////////////
// Game specific info, in STOUGCInteriorCommon.c or
// NWUGCInteriorCommon.c
UGCRoomInfo* ugcRoomGetRoomInfo( const GroupDef *room_group );
UGCRoomInfo* ugcRoomAllocRoomInfo( const GroupDef* room_group );
void ugcRoomFreeRoomInfo( UGCRoomInfo* roomInfo );
