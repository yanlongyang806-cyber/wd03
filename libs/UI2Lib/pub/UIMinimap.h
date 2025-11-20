//// A widget that contains an external map stored in it.  Place this
//// in a UIScrollArea to give it scroll-support.
#pragma once
GCC_SYSTEM

#include "UILib.h"

typedef struct ZoneMapEncounterInfo ZoneMapEncounterInfo;
typedef struct UIMinimapComputedLayoutRegion UIMinimapComputedLayoutRegion;

typedef struct UIMinimapObject
{
	Vec3 pos;
	char* text;
	const char* icon;

	UserData data;

	Vec2 layout_pos;
} UIMinimapObject;

typedef struct UIMinimap
{
	UIWidget widget;
	ZoneMapEncounterInfo* mapInfo; // If this is set use mapInfo->map_name instead of mapName.
	const char* mapName;
	unsigned mapIsLoaded : 1;
	unsigned mapIsMiniLoaded : 1;
	
	const char* regionName;
	Vec3 regionMin;
	Vec3 regionMax;
	bool regionRestricted;
	
	Vec3 highlightAreaMin;
	Vec3 highlightAreaMax;
	bool highlightAreaSet;
	
	bool autosize;
	F32 scale;
	UIMinimapObject* selectedObject;
	UIMinimapObject* hoverObject;
	
	UIActivation2Func clickFn;
	UserData clickData;
	
	UIMinimapObject** objects;

	UIMinimapComputedLayoutRegion **layout_regions;
	Vec2 layout_size;
	bool layout_calculated;
} UIMinimap;

SA_RET_NN_VALID UIMinimap* ui_MinimapCreate(void);
void ui_MinimapFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIMinimap* minimap);

const char* ui_MinimapGetMap(SA_PARAM_NN_VALID const UIMinimap* minimap);
bool ui_MinimapSetMap(SA_PARAM_NN_VALID UIMinimap* minimap, const char* mapName);
bool ui_MinimapSetMapAndRestrictToRegion(SA_PARAM_NN_VALID UIMinimap* minimap, const char* mapNameRaw, const char *regionName, Vec3 regionMin, Vec3 regionMax);
void ui_MinimapSetMapHighlightArea( SA_PARAM_NN_VALID UIMinimap* minimap, Vec3 min, Vec3 max );
bool ui_MinimapSetMapInfo(SA_PARAM_NN_VALID UIMinimap* minimap, ZoneMapEncounterInfo *mapInfo);
void ui_MinimapClearObjects(SA_PARAM_NN_VALID UIMinimap* minimap);
void ui_MinimapAddObject(SA_PARAM_NN_VALID UIMinimap* minimap, Vec3 pos, const char* text, const char* icon, UserData data);
void ui_MinimapSetSelectedObject(SA_PARAM_NN_VALID UIMinimap* minimap, UserData data, bool callCallback);
void ui_MinimapSetObjectClickCallback(SA_PARAM_NN_VALID UIMinimap* minimap, UIActivation2Func clickFn, UserData clickData);
void ui_MinimapSetScale(SA_PARAM_NN_VALID UIMinimap* minimap, F32 scale);

void ui_MinimapTick(SA_PARAM_NN_VALID UIMinimap* minimap, UI_PARENT_ARGS);
void ui_MinimapDraw(SA_PARAM_NN_VALID UIMinimap* minimap, UI_PARENT_ARGS);

void ui_MinimapGetObjectPos(UIMinimap *minimap, UIMinimapObject *object, Vec2 out_world_pos);

