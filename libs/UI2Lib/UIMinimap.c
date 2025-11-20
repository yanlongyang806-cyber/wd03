#include"UIMinimap.h"

#include"../RoomConnPrivate.h"
#include"GfxClipper.h"
#include"ReferenceSystem.h"
#include"StringCache.h"
#include"WorldGrid.h"
#include"GfxSprite.h"
#include"GfxTexAtlas.h"
#include"inputMouse.h"
#include"GfxSpriteText.h"
#include"GfxMapSnap.h"
#include"MapSnap.h"

typedef struct RoomPartitionParsed RoomPartitionParsed;
typedef struct ZoneMapEncounterRegionInfo ZoneMapEncounterRegionInfo;

static int ui_MinimapRoomPartitionSort( const RoomPartitionParsed** room1, const RoomPartitionParsed** room2 );
static bool ui_MinimapRoomInRegion( RoomPartitionParsed* room, ZoneMapEncounterRegionInfo* region );
static void ui_MinimapZoneMapRegisterDynamicTexture( UIMinimap* minimap, const char* texName, bool isForMini );
static void ui_MinimapZoneMapUnregisterDynamicTexture( UIMinimap* minimap, const char* texName, bool isForMini );
static AtlasTex* ui_MinimapZoneMapTexture( UIMinimap* minimap, const char* texName, bool isForMini );
static void ui_MinimapComputedLayoutRegionDestroy(UIMinimapComputedLayoutRegion *region);
static void ui_MinimapCalcLayout(UIMinimap *minimap);
	
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

bool g_minimapForceLowRes = false;
AUTO_CMD_INT(g_minimapForceLowRes, MinimapForceLowRes);


UIMinimap* ui_MinimapCreate(void)
{
	UIMinimap* accum = calloc( 1, sizeof( *accum ));
	ui_WidgetInitialize( UI_WIDGET( accum ), ui_MinimapTick, ui_MinimapDraw, ui_MinimapFreeInternal, NULL, NULL );
	return accum;
}

void ui_MinimapFreeInternal(UIMinimap* minimap)
{
	ui_MinimapSetMap( minimap, NULL );
	eaDestroyEx(&minimap->layout_regions, ui_MinimapComputedLayoutRegionDestroy);
	ui_WidgetFreeInternal(UI_WIDGET(minimap));
}

const char* ui_MinimapGetMap(const UIMinimap* minimap)
{
	if( minimap->mapName ) {
		return minimap->mapName;
	} else if( minimap->mapInfo ) {
		return minimap->mapInfo->map_name;
	} else {
		return NULL;
	}
}

// Intentionally ignore the fileloader callback since the widget may
// be freed by then.
static void ui_MinimapIgnoredCB( const char* filenameIgnored, UserData ignored2 )
{
}

bool ui_MinimapSetMapAndRestrictToRegion(SA_PARAM_NN_VALID UIMinimap* minimap, const char* mapNameRaw, const char *regionName, Vec3 regionMin, Vec3 regionMax)
{
	// These must be called before the SetMap if we are going to be restricted:
	minimap->regionRestricted = true;
	minimap->regionName = regionName;
	copyVec3(regionMin, minimap->regionMin);
	copyVec3(regionMax, minimap->regionMax);

	// Now we are good to do the reset of the SetMap work
	return(ui_MinimapSetMap(minimap, mapNameRaw));
}

void ui_MinimapSetMapHighlightArea( SA_PARAM_NN_VALID UIMinimap* minimap, Vec3 min, Vec3 max )
{
	if( min && max ) {
		copyVec3( min, minimap->highlightAreaMin );
		copyVec3( max, minimap->highlightAreaMax );
		minimap->highlightAreaSet = true;
	} else {
		zeroVec3( minimap->highlightAreaMin );
		zeroVec3( minimap->highlightAreaMax );
		minimap->highlightAreaSet = false;
	}
}

bool ui_MinimapSetMap(UIMinimap* minimap, const char* mapNameRaw)
{
	const char* mapName = allocAddString( mapNameRaw );

	if( mapName == minimap->mapName ) {
		return false;
	}

	minimap->layout_calculated = false;

	{
		ZoneMapExternalMapSnap* prevMap = NULL;
		ZoneMapExternalMapSnap* newMap = NULL;
		ZoneMapInfo* newMapInfo = NULL;

		if( minimap->mapName ) {
			prevMap = RefSystem_ReferentFromString( "ZoneMapExternalMapSnap", minimap->mapName );
		}
		if( mapName ) {
			newMap = RefSystem_ReferentFromString( "ZoneMapExternalMapSnap", mapName );
			newMapInfo = RefSystem_ReferentFromString( "ZoneMap", mapName );
		}

		// Unregister old map
		if( prevMap ) {
			if( minimap->mapIsLoaded ) {
				int roomIt;
				int imageIt;
				for( roomIt = 0; roomIt != eaSize( &prevMap->mapRooms ); ++roomIt ) {
					for( imageIt = 0; imageIt != eaSize( &prevMap->mapRooms[roomIt]->mapSnapData.image_name_list ); ++imageIt ) {
						ui_MinimapZoneMapUnregisterDynamicTexture( minimap, prevMap->mapRooms[roomIt]->mapSnapData.image_name_list[imageIt], false );
					}
					if( prevMap->mapRooms[roomIt]->mapSnapData.overview_image_name ) {
						ui_MinimapZoneMapUnregisterDynamicTexture( minimap, prevMap->mapRooms[roomIt]->mapSnapData.overview_image_name, false );
					}
				}
				minimap->mapIsLoaded = false;
			}
			if( minimap->mapIsMiniLoaded ) {
				int roomIt;
				for( roomIt = 0; roomIt != eaSize( &prevMap->mapRooms ); ++roomIt ) {
					if ( prevMap->mapRooms[roomIt]->mapSnapData.overview_image_name ) {
						ui_MinimapZoneMapUnregisterDynamicTexture( minimap, prevMap->mapRooms[roomIt]->mapSnapData.overview_image_name, true );
					} else if( eaSize( &prevMap->mapRooms[roomIt]->mapSnapData.image_name_list ) == 1 ) {
						ui_MinimapZoneMapUnregisterDynamicTexture( minimap, prevMap->mapRooms[roomIt]->mapSnapData.image_name_list[ 0 ], true );
					}
				}
				minimap->mapIsMiniLoaded = false;
			}
		}
		assert( !minimap->mapIsLoaded && !minimap->mapIsMiniLoaded );

		minimap->mapName = mapName;
		minimap->mapInfo = NULL;

		// Register the new one -- now going to happen asynchronously
		if( newMap && newMapInfo ) {
			char buffer[ MAX_PATH ];
			sprintf( buffer, "bin/geobin/%s/Map_Snap_Mini.Hogg", zmapInfoGetFilename( newMapInfo ));
			fileLoaderRequestAsyncExec( allocAddFilename( buffer ), FILE_MEDIUM_PRIORITY, false, ui_MinimapIgnoredCB, NULL );
			sprintf( buffer, "bin/geobin/%s/Map_Snap.Hogg", zmapInfoGetFilename( newMapInfo ));
			fileLoaderRequestAsyncExec( allocAddFilename( buffer ), FILE_MEDIUM_PRIORITY, false, ui_MinimapIgnoredCB, NULL );
			
			minimap->mapIsLoaded = false;
			minimap->mapIsMiniLoaded = false;
		}
	}

	if( minimap->autosize) {
		ui_MinimapCalcLayout( minimap );
		minimap->widget.width = minimap->layout_size[0] + minimap->widget.leftPad + minimap->widget.rightPad;
		minimap->widget.height = minimap->layout_size[1] + minimap->widget.topPad + minimap->widget.bottomPad;
		minimap->widget.widthUnit = UIUnitFixed;
		minimap->widget.heightUnit = UIUnitFixed;
	}

	return true;
}


bool ui_MinimapSetMapInfo(SA_PARAM_NN_VALID UIMinimap* minimap, ZoneMapEncounterInfo *mapInfo)
{
	ui_MinimapSetMap( minimap, NULL );
	minimap->mapInfo = mapInfo;
	return true;
}

void ui_MinimapClearObjects(UIMinimap* minimap)
{
	int it;
	for( it = 0; it != eaSize( &minimap->objects ); ++it ) {
		free( minimap->objects[ it ]->text );
		free( minimap->objects[ it ]);
	}

	eaDestroy( &minimap->objects );

	minimap->hoverObject = NULL;
	minimap->selectedObject = NULL;
	minimap->layout_calculated = false;
}

void ui_MinimapAddObject(UIMinimap* minimap, Vec3 pos, const char* text, const char* icon, UserData data)
{
	UIMinimapObject* accum = calloc( 1, sizeof( *accum ));
	copyVec3( pos, accum->pos );
	accum->text = strdup( text );
	accum->icon = allocAddString( icon );
	accum->data = data;
	minimap->layout_calculated = false;

	eaPush( &minimap->objects, accum );
}

void ui_MinimapSetSelectedObject(UIMinimap* minimap, UserData data, bool callCallback)
{
	int it;
	for( it = eaSize( &minimap->objects ) - 1; it >= 0; --it ) {
		if( minimap->objects[ it ]->data == data )  {
			break;
		}
	}

	minimap->selectedObject = eaGet( &minimap->objects, it );
	if( callCallback && minimap->selectedObject && minimap->clickFn ) {
		minimap->clickFn( minimap, minimap->clickData, minimap->selectedObject->data );
	}
}


void ui_MinimapSetObjectClickCallback(UIMinimap* minimap, UIActivation2Func clickFn, UserData clickData)
{
	minimap->clickFn = clickFn;
	minimap->clickData = clickData;
}

void ui_MinimapSetScale(SA_PARAM_NN_VALID UIMinimap* minimap, F32 scale)
{
	minimap->scale = scale;
}

typedef struct UIMinimapComputedLayoutRegion
{
	const char *region_name;
	Vec2 region_min;
	Vec2 region_max;
	F32 x_offset;
	WorldRegionType	type;
	F32 fGroundFocusHeight;
	UIMinimapObject **objects;
	RoomPartitionParsed **rooms;
} UIMinimapComputedLayoutRegion;

static void ui_MinimapComputedLayoutRegionDestroy(UIMinimapComputedLayoutRegion *region)
{
	eaDestroy(&region->objects);
	eaDestroyStruct(&region->rooms, parse_RoomPartitionParsed);
	SAFE_FREE(region);
}

static void ui_MinimapCalcRegionSkew(UIMinimap *minimap, WorldRegionType type, Vec2 skew)
{
	if((!minimap->mapInfo || !minimap->mapInfo->ugc_picker_widget) && (!mapSnapMapNameIsUGC(minimap->mapInfo ? minimap->mapInfo->map_name : minimap->mapName)))
	{
		if(type == WRT_Indoor) {
			skew[0] = gConf.fMapSnapIndoorOrthoSkewX;
			skew[1] = gConf.fMapSnapIndoorOrthoSkewZ;
		} else {
			skew[0] = gConf.fMapSnapOutdoorOrthoSkewX;
			skew[1] = gConf.fMapSnapOutdoorOrthoSkewZ;
		}
	}
	else
		zeroVec2(skew);
}

static void ui_MinimapCalcLayout(UIMinimap *minimap)
{
	ZoneMapEncounterInfo* selectedZMInfo = NULL;
	ZoneMapExternalMapSnap* selectedZMMapSnap = NULL;

	if (minimap->mapInfo) {
		selectedZMInfo = minimap->mapInfo;
	} else if ( minimap->mapName ) {
		selectedZMInfo = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", minimap->mapName );
		selectedZMMapSnap = RefSystem_ReferentFromString( "ZoneMapExternalMapSnap", minimap->mapName );
	}

	eaDestroyEx(&minimap->layout_regions, ui_MinimapComputedLayoutRegionDestroy);

	minimap->hoverObject = NULL;
	if( !selectedZMInfo || (!selectedZMMapSnap && !minimap->mapInfo) ) {
		// Do nothing
	} else {
		float xOffset = 0;
		int regionIt;
		int roomIt;
		int objectIt;
		RoomPartitionParsed** sortedRooms = NULL;
		
		float widthAccum = 0;
		float heightAccum = 0;

		if (selectedZMMapSnap)
		{
			eaPushEArray( &sortedRooms, &selectedZMMapSnap->mapRooms );
			eaQSort( sortedRooms, ui_MinimapRoomPartitionSort );
		}

		for( regionIt = 0; regionIt != eaSize( &selectedZMInfo->regions ); ++regionIt ) {
			ZoneMapEncounterRegionInfo* region = selectedZMInfo->regions[ regionIt ];
			Vec3 regionRoomMin = { FLT_MAX, FLT_MAX, FLT_MAX };
			Vec3 regionRoomMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			Vec2 regionExtentsMin, regionExtentsMax;
			UIMinimapComputedLayoutRegion *layout_region;
			RoomPartitionParsed **rooms_in_region = NULL;

			Vec2 skew;
			ui_MinimapCalcRegionSkew(minimap, region->type, skew);

			if (minimap->regionRestricted && region->regionName != minimap->regionName)
				continue;

			// determine min and max
			if (minimap->regionRestricted)
			{
				copyVec3(minimap->regionMin, regionRoomMin);
				copyVec3(minimap->regionMax, regionRoomMax);
				for( roomIt = 0; roomIt != eaSize( &sortedRooms ); ++roomIt ) {
					RoomPartitionParsed* room = sortedRooms[ roomIt ];
					if( ui_MinimapRoomInRegion( room, region )) {
						eaPush(&rooms_in_region, room);
					}
				}
			}
			else
			{
				if (eaSize(&sortedRooms) > 0)
				{
					for( roomIt = 0; roomIt != eaSize( &sortedRooms ); ++roomIt ) {
						RoomPartitionParsed* room = sortedRooms[ roomIt ];

						if( !ui_MinimapRoomInRegion( room, region )) {
							continue;
						}

						eaPush(&rooms_in_region, room);

						vec3RunningMin( room->bounds_min, regionRoomMin );
						vec3RunningMax( room->bounds_max, regionRoomMax );
					}
				}
				else
				{
					copyVec3(region->min, regionRoomMin);
					copyVec3(region->max, regionRoomMax);
				}
			}

			if(   regionRoomMin[ 0 ] > regionRoomMax[ 0 ] || regionRoomMin[ 1 ] > regionRoomMax[ 1 ]
				  || regionRoomMin[ 2 ] > regionRoomMax[ 2 ]) {
				eaDestroy(&rooms_in_region);
				continue;
			}

			//compensate for map skew at bounds
			mapSnapGetExtendedBounds(regionRoomMin, regionRoomMax, skew, region->fGroundFocusHeight, regionExtentsMin, regionExtentsMax);

			layout_region = calloc(1, sizeof(UIMinimapComputedLayoutRegion));
			layout_region->region_name = region->regionName;
			copyVec2(regionExtentsMin, layout_region->region_min);
			copyVec2(regionExtentsMax, layout_region->region_max);

			eaCopyStructs(&rooms_in_region, &layout_region->rooms, parse_RoomPartitionParsed);
			layout_region->x_offset = xOffset;
			layout_region->type = region->type;
			layout_region->fGroundFocusHeight = region->fGroundFocusHeight;
			eaPush(&minimap->layout_regions, layout_region);

			eaDestroy(&rooms_in_region);


			widthAccum = MAX( widthAccum, regionExtentsMax[0] - regionExtentsMin[0] + xOffset );
			heightAccum = MAX( heightAccum, regionExtentsMax[1] - regionExtentsMin[1] );

			for( objectIt = 0; objectIt != eaSize( &minimap->objects ); ++objectIt ) {
				UIMinimapObject* object = minimap->objects[ objectIt ];
				AtlasTex* tex = NULL;
				CBox texBox = { 0 };

				if( !pointBoxCollision( object->pos, region->min, region->max )) {
					continue;
				}

				object->layout_pos[0] = (object->pos[0] - regionExtentsMin[0]) + skew[0] * (object->pos[1] - region->fGroundFocusHeight) + xOffset;
				object->layout_pos[1] = (regionExtentsMax[1] - object->pos[2]) - skew[1] * (object->pos[1] - region->fGroundFocusHeight);
				eaPush(&layout_region->objects, object);

				tex = atlasLoadTexture( object->icon );
			}

			xOffset += (regionExtentsMax[0] - regionExtentsMin[0]) + 4;
		}
		eaDestroy( &sortedRooms );

		minimap->layout_size[0] = widthAccum;
		minimap->layout_size[1] = heightAccum;
	}

	minimap->layout_calculated = true;
}

void ui_MinimapGetObjectPos(UIMinimap *minimap, UIMinimapObject *object, Vec2 out_world_pos)
{
	if (!minimap->layout_calculated)
		ui_MinimapCalcLayout(minimap);
	
	out_world_pos[0] = object->layout_pos[0] * ((minimap->scale > 0) ? minimap->scale : 1);
	out_world_pos[1] = object->layout_pos[1] * ((minimap->scale > 0) ? minimap->scale : 1);
}

static void ui_MinimapSelectedFillDrawingDescription( UIMinimap* minimap, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( minimap );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrMinimapSelectedStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->styleBorderName = "Default_Capsule_Filled";
	}
}

void ui_MinimapTick(UIMinimap* minimap, UI_PARENT_ARGS)
{
	ZoneMapEncounterInfo* selectedZMInfo = NULL;
	ZoneMapExternalMapSnap* selectedZMMapSnap = NULL;
	F32 map_scale;

	UI_GET_COORDINATES(minimap);

	if (!minimap->layout_calculated)
		ui_MinimapCalcLayout(minimap);

	if (minimap->scale > 0)
		map_scale = minimap->scale * scale;
	else
		map_scale = scale;

	if (minimap->mapInfo) {
		selectedZMInfo = minimap->mapInfo;
	} else if ( minimap->mapName ) {
		selectedZMInfo = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", minimap->mapName );
		selectedZMMapSnap = RefSystem_ReferentFromString( "ZoneMapExternalMapSnap", minimap->mapName );
	}

	minimap->hoverObject = NULL;
	if( !selectedZMInfo || (!selectedZMMapSnap && !minimap->mapInfo) ) {
		if( minimap->autosize ) {
			minimap->widget.width = 1;
			minimap->widget.height = 1;
			minimap->widget.widthUnit = UIUnitPercentage;
			minimap->widget.heightUnit = UIUnitPercentage;
		}
	} else {
		F32 closest_distance = 1e8;
		S32 xp, yp;
		bool selected_object = false;
		mousePos(&xp, &yp);

		FOR_EACH_IN_EARRAY(minimap->layout_regions, UIMinimapComputedLayoutRegion, layout_region)
		{
			FOR_EACH_IN_EARRAY(layout_region->objects, UIMinimapObject, object)
			{
				AtlasTex* tex = NULL;
				CBox texBox = { 0 };
				tex = atlasLoadTexture( object->icon );
				texBox.lx = object->layout_pos[0] * map_scale - tex->width / 2 + box.lx;
				texBox.ly = object->layout_pos[1] * map_scale - tex->height / 2 + box.ly;
				texBox.hx = texBox.lx + tex->width;
				texBox.hy = texBox.ly + tex->height;

				if( mouseCollision( &texBox )) {
					S32 px = object->layout_pos[0] * map_scale + box.lx;
					S32 py = object->layout_pos[1] * map_scale + box.ly;
					F32 distance;
					distance = SQR(px-xp) + SQR(py-yp);
					if (distance < closest_distance)
					{
						minimap->hoverObject = object;
						if( mouseDown( MS_LEFT )) {
							minimap->selectedObject = object;
							selected_object = true;
						}
						closest_distance = distance;
					}
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		if( minimap->autosize )
		{
			minimap->widget.width = minimap->layout_size[0] + minimap->widget.leftPad + minimap->widget.rightPad;
			minimap->widget.height = minimap->layout_size[1] + minimap->widget.topPad + minimap->widget.bottomPad;
			minimap->widget.widthUnit = UIUnitFixed;
			minimap->widget.heightUnit = UIUnitFixed;
		}

		if (selected_object && minimap->clickFn)
		{
			minimap->clickFn( minimap, minimap->clickData, minimap->selectedObject->data );
			inpHandled();
			return;
		}
	}
}

int ui_MinimapRoomPartitionSort( const RoomPartitionParsed** room1, const RoomPartitionParsed** room2 )
{
	return (*room1)->bounds_max[1] - (*room2)->bounds_max[1];
}

bool ui_MinimapRoomInRegion( RoomPartitionParsed* room, ZoneMapEncounterRegionInfo* region )
{
	if( room->bounds_min[0] > region->max[0] || room->bounds_min[1] > region->max[1] || room->bounds_min[2] > region->max[2] ) {
		return false;
	}
	if( room->bounds_max[0] < region->min[0] || room->bounds_max[1] < region->min[1] || room->bounds_max[2] < region->min[2] ) {
		return false;
	}
	
	return true;
}

void ui_MinimapDraw(UIMinimap* minimap, UI_PARENT_ARGS)
{
	UISkin* skin = UI_GET_SKIN( minimap );
	ZoneMapEncounterInfo* encounterInfo = NULL;
	ZoneMapExternalMapSnap* externalMapSnap = NULL;
	F32 map_scale;
	UI_GET_COORDINATES(minimap);

	// Demand-load the dynamic textures
	if( minimap->mapName ) {
		ZoneMapInfo* zmInfo = RefSystem_ReferentFromString( "ZoneMap", minimap->mapName );
		ZoneMapExternalMapSnap* map = RefSystem_ReferentFromString( "ZoneMapExternalMapSnap", minimap->mapName );
		char mapSnapHogg[ MAX_PATH ];
		char mapSnapMiniHogg[ MAX_PATH ];

		if( !zmInfo || !map ) {
			return;
		}
		
		sprintf( mapSnapHogg, "bin/geobin/%s/Map_Snap.Hogg", zmapInfoGetFilename( zmInfo ));
		sprintf( mapSnapMiniHogg, "bin/geobin/%s/Map_Snap_Mini.Hogg", zmapInfoGetFilename( zmInfo ));

		if( (isProductionMode() || fileExists( mapSnapHogg )) && !fileNeedsPatching( mapSnapHogg )) {
			if( !minimap->mapIsLoaded ) {
				int roomIt;
				int imageIt;
				for( roomIt = 0; roomIt != eaSize( &map->mapRooms ); ++roomIt ) {
					for( imageIt = 0; imageIt != eaSize( &map->mapRooms[roomIt]->mapSnapData.image_name_list ); ++imageIt ) {
						ui_MinimapZoneMapRegisterDynamicTexture( minimap, map->mapRooms[roomIt]->mapSnapData.image_name_list[imageIt], false );
					}
					if( map->mapRooms[roomIt]->mapSnapData.overview_image_name ) {
						ui_MinimapZoneMapRegisterDynamicTexture( minimap, map->mapRooms[roomIt]->mapSnapData.overview_image_name, false );
					}
				}
			}

			minimap->mapIsLoaded = true;
		}
		if( (isProductionMode() || fileExists( mapSnapMiniHogg )) && !fileNeedsPatching( mapSnapMiniHogg )) {
			if( !minimap->mapIsMiniLoaded ) {
				int roomIt;
				for( roomIt = 0; roomIt != eaSize( &map->mapRooms ); ++roomIt ) {
					if( map->mapRooms[roomIt]->mapSnapData.overview_image_name ) {
						ui_MinimapZoneMapRegisterDynamicTexture( minimap, map->mapRooms[roomIt]->mapSnapData.overview_image_name, true );
					} else if( eaSize( &map->mapRooms[roomIt]->mapSnapData.image_name_list ) == 1 ) {
						ui_MinimapZoneMapRegisterDynamicTexture( minimap, map->mapRooms[roomIt]->mapSnapData.image_name_list[0], true );
					}
				}

				minimap->mapIsMiniLoaded = true;
			}
		}
	}

	UI_DRAW_EARLY(minimap);

	if (!minimap->layout_calculated)
		ui_MinimapCalcLayout(minimap);

	if (minimap->scale > 0)
		map_scale = minimap->scale * scale;
	else
		map_scale = scale;

	if (minimap->mapInfo) {
		encounterInfo = minimap->mapInfo;
	} else if ( (minimap->mapIsLoaded || minimap->mapIsMiniLoaded) && minimap->mapName ) {
		encounterInfo = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", minimap->mapName );
		externalMapSnap = RefSystem_ReferentFromString( "ZoneMapExternalMapSnap", minimap->mapName );
	}

	if( encounterInfo && (externalMapSnap || minimap->mapInfo) ) {

		// Perhaps not the best way to do this, but I'm short on time right now -- TomY
		if (!minimap->mapInfo)
			display_sprite_box( atlasFindTexture("white"), &box, z, 0x000000FF );

		FOR_EACH_IN_EARRAY(minimap->layout_regions, UIMinimapComputedLayoutRegion, layout_region)
		{
			Vec2 skew;
			ui_MinimapCalcRegionSkew(minimap, layout_region->type, skew);

			FOR_EACH_IN_EARRAY(layout_region->rooms, RoomPartitionParsed, room)
			{
				CBox texBox = { 0 };
				Vec2 roomBoundsSkewedMin, roomBoundsSkewedMax;
				bool highRes = true;

				mapSnapGetExtendedBounds(room->bounds_min, room->bounds_max, skew, layout_region->fGroundFocusHeight, roomBoundsSkewedMin, roomBoundsSkewedMax);

				if (eaSize(&room->mapSnapData.image_name_list) <= 1) {
					highRes = false;
				}
				if (room->mapSnapData.image_width * map_scale < MS_OV_IMAGE_SIZE) {
					highRes = false;
				}
				if (!minimap->mapIsLoaded || g_minimapForceLowRes) {
					highRes = false;
				}

				texBox.lx = ((roomBoundsSkewedMin[0] - layout_region->region_min[0]) + layout_region->x_offset) * map_scale + box.lx;
				texBox.ly = (layout_region->region_max[1] - roomBoundsSkewedMax[1]) * map_scale + box.ly;
				texBox.hx = ((roomBoundsSkewedMax[0] - layout_region->region_min[0]) + layout_region->x_offset) * map_scale + box.lx;
				texBox.hy = (layout_region->region_max[1] - roomBoundsSkewedMin[1]) * map_scale + box.ly;

				if (highRes)
				{
					S32 curX = 0, curY = room->mapSnapData.image_height; // In texture coords
					int imageIt;
					CBox subTexBox = { 0 };
					F32 x_scale = (roomBoundsSkewedMax[0]-roomBoundsSkewedMin[0]) * map_scale / room->mapSnapData.image_width;
					F32 y_scale = (roomBoundsSkewedMax[1]-roomBoundsSkewedMin[1]) * map_scale / room->mapSnapData.image_height;

					for (imageIt = 0; imageIt < eaSize(&room->mapSnapData.image_name_list); imageIt++)
					{
						AtlasTex* tex = ui_MinimapZoneMapTexture( minimap, room->mapSnapData.image_name_list[imageIt], false );
						subTexBox.lx = texBox.lx + curX*x_scale;
						subTexBox.ly = texBox.ly + (curY-tex->height)*y_scale;
						subTexBox.hx = texBox.lx + (curX+tex->width)*x_scale;
						subTexBox.hy = texBox.ly + curY*y_scale;
						display_sprite_box( tex, &subTexBox, z, 0xFFFFFFFF );
						curX += tex->width;
						if (curX >= room->mapSnapData.image_width)
						{
							curX = 0;
							curY -= tex->height;
						}
					}
				}
				else
				{
					const char* texName = NULL;
					AtlasTex* tex = NULL;
				
					if( !texName ) {
						texName = room->mapSnapData.overview_image_name;
					}
					if( !texName && eaSize( &room->mapSnapData.image_name_list )) {
						texName = room->mapSnapData.image_name_list[ 0 ];
					}
					if( !texName ) {
						continue;
					}

					if( !minimap->mapIsLoaded || g_minimapForceLowRes ) {
						tex = ui_MinimapZoneMapTexture( minimap, texName, true );
					} else {
						tex = ui_MinimapZoneMapTexture( minimap, texName, false );
					}
					if( tex ) {
						display_sprite_box( tex, &texBox, z, 0xFFFFFFFF );
					}
				}
			}
			FOR_EACH_END;

			if( minimap->highlightAreaSet ) {
				AtlasTex* tex = atlasFindTexture( "white" );
				CBox highlightBox = { 0 };
				highlightBox.lx = (minimap->highlightAreaMin[0] - layout_region->region_min[0]) * map_scale + box.lx;
				highlightBox.ly = (layout_region->region_max[1] - minimap->highlightAreaMax[2]) * map_scale + box.ly;
				highlightBox.hx = (minimap->highlightAreaMax[0] - layout_region->region_min[0]) * map_scale + box.lx;
				highlightBox.hy = (layout_region->region_max[1] - minimap->highlightAreaMin[2]) * map_scale + box.ly;

				// Draw the inverse of the box, we want to obscure
				// what is not in the box.
				{
					CBox texBox;
					
					// top
					texBox.lx = box.lx;
					texBox.ly = box.ly;
					texBox.hx = box.hx;
					texBox.hy = highlightBox.ly;
					display_sprite_box( tex, &texBox, z + 0.2, 0xFFFFFF20 );

					// bottom
					texBox.lx = box.lx;
					texBox.ly = highlightBox.hy;
					texBox.hx = box.hx;
					texBox.hy = box.hy;
					display_sprite_box( tex, &texBox, z + 0.2, 0xFFFFFF20 );

					// left
					texBox.lx = box.lx;
					texBox.ly = highlightBox.ly;
					texBox.hx = highlightBox.lx;
					texBox.hy = highlightBox.hy;
					display_sprite_box( tex, &texBox, z + 0.2, 0xFFFFFF20 );

					// right
					texBox.lx = highlightBox.hx;
					texBox.ly = highlightBox.ly;
					texBox.hx = box.hx;
					texBox.hy = highlightBox.hy;
					display_sprite_box( tex, &texBox, z + 0.2, 0xFFFFFF20 );
				}
			}

			FOR_EACH_IN_EARRAY(layout_region->objects, UIMinimapObject, object)
			{
				AtlasTex* tex = NULL;
				CBox texBox = { 0 };
				bool objectIsFocused = false;

				tex = atlasLoadTexture( object->icon );
				texBox.lx = floorf( object->layout_pos[0] * map_scale - tex->width * skin->fMinimapIconScale / 2 + box.lx );
				texBox.ly = floorf( object->layout_pos[1] * map_scale - tex->height * skin->fMinimapIconScale / 2 + box.ly );
				texBox.hx = texBox.lx + tex->width * skin->fMinimapIconScale;
				texBox.hy = texBox.ly + tex->height * skin->fMinimapIconScale;

				if( minimap->hoverObject ) {
					objectIsFocused = (object == minimap->hoverObject);
				} else {
					objectIsFocused = (object == minimap->selectedObject);
				}
				
				if( objectIsFocused ) {
					UIDrawingDescription selectedDesc = { 0 };
					CBox selectedBox = texBox;

					ui_MinimapSelectedFillDrawingDescription( minimap, &selectedDesc );
					ui_DrawingDescriptionOuterBox( &selectedDesc, &selectedBox, 1 );
					ui_DrawingDescriptionDraw( &selectedDesc, &selectedBox, 1, z + 0.5, 255, ColorWhite, ColorWhite );
					display_sprite_box( tex, &texBox, z + 0.5, 0xFFFFFFFF );
				} else {
					display_sprite_box( tex, &texBox, z, 0xFFFFFFFF );
				}

				if( objectIsFocused ) {
					UIStyleFont* font = ui_StyleFontGet( "Game" );
					ui_StyleFontUse( font, false, 0 );
					if( object->text ) {
						gfxfont_Print( (texBox.lx + texBox.hx) / 2, texBox.hy + 4 + ui_StyleFontLineHeight(font, 1.f), z + 0.5, 1, 1, CENTER_X, object->text );
					}
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}

	UI_DRAW_LATE(minimap);
}

void ui_MinimapZoneMapRegisterDynamicTexture( UIMinimap* minimap, const char* texName, bool isForMini )
{
	char dynamicName[ MAX_PATH * 2 ];
	ZoneMapInfo* zmInfo = RefSystem_ReferentFromString( "ZoneMap", minimap->mapName );

	if( !zmInfo ) {
		return;
	}

	sprintf( dynamicName, "bin/geobin/%s/Map_Snap%s.Hogg", zmapInfoGetFilename( zmInfo ), (isForMini ? "_Mini" : "") );
	if (!fileExists( dynamicName )) {
		ErrorFilenamef( zmapInfoGetFilename( zmInfo ), "Zone map references map texture that doesn't exist: %s", dynamicName);
		return;
	}

	sprintf( dynamicName, "#bin/geobin/%s/Map_Snap%s.Hogg#%s",
			 zmapInfoGetFilename( zmInfo ), (isForMini ? "_Mini" : ""), texName );
	{
		BasicTexture* tex = texRegisterDynamic( dynamicName );
		tex->bt_texopt_flags |= TEXOPT_CLAMPS;
		tex->bt_texopt_flags |= TEXOPT_CLAMPT;
	}
}

void ui_MinimapZoneMapUnregisterDynamicTexture( UIMinimap* minimap, const char* texName, bool isForMini )
{
	char dynamicName[ MAX_PATH * 2 ];
	ZoneMapInfo* zmInfo = RefSystem_ReferentFromString( "ZoneMap", minimap->mapName );

	if( !zmInfo ) {
		return;
	}

	sprintf( dynamicName, "bin/geobin/%s/Map_Snap%s.Hogg", zmapInfoGetFilename( zmInfo ), (isForMini ? "_Mini" : "") );
	if (!fileExists( dynamicName )) {
		return;
	}

	sprintf( dynamicName, "#bin/geobin/%s/Map_Snap%s.Hogg#%s",
			 zmapInfoGetFilename( zmInfo ), (isForMini ? "_Mini" : ""), texName );
	texUnregisterDynamic( dynamicName );
}

AtlasTex* ui_MinimapZoneMapTexture( UIMinimap* minimap, const char* texName, bool isForMini )
{
	char dynamicName[ MAX_PATH * 2 ];
	ZoneMapInfo* zmInfo = RefSystem_ReferentFromString( "ZoneMap", minimap->mapName );

	if( !zmInfo ) {
		return NULL;
	}
	
	sprintf( dynamicName, "#bin/geobin/%s/Map_Snap%s.Hogg#%s", zmapInfoGetFilename( zmInfo ), (isForMini ? "_Mini" : ""), texName );
	return atlasFindTexture( dynamicName );
}

