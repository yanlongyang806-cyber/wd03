#pragma once

#include "MapRevealCommon.h"
#include "Powers.h"
#include "contact_enums.h"
#include "mission_enums.h"
#include "inputLibEnums.h"

#include "gclUIGen.h"

typedef struct MapRevealInfo MapRevealInfo;
typedef struct DebugDrawUserData DebugDrawUserData;
typedef struct RoomPartition RoomPartition;
typedef struct RoomConnGraph RoomConnGraph;
typedef enum MapNotificationType MapNotificationType;
// Funcs for debug drawing
typedef void (*DrawLineFunc)(Vec3 world_src, int color_src, Vec3 world_dst, int color_dst, DebugDrawUserData *userdata);
typedef void (*DrawSphereFunc)(Vec3 pos, F32 radius, int color, DebugDrawUserData *userdata);
typedef void (*UIDebugDrawFunc)(Entity *pPlayer, void *userdata, DrawLineFunc line_func, DebugDrawUserData *line_data, DrawSphereFunc sphere_func, DebugDrawUserData *spheredata);

void ui_MapAddDebugDrawer(UIDebugDrawFunc func, void *userdata);
void ui_MapRemoveDebugDrawer(UIDebugDrawFunc func);

void ui_GenMapSetScale(UIGen *pGen, F32 fScale, bool bSave);
void ui_GenMapSetPixelsPerWorldUnit(UIGen *pGen, F32 fPixelPerWorldUnit, bool bSave);

void GenMapIconGetMissionLabel(MinimapWaypoint *pWaypoint, char **ppchLabel);
void gclGenGetMapKeyIcons(SA_PARAM_NN_VALID UIGen *pGen);
AUTO_ENUM AEN_APPEND_TO(MouseButton);
typedef enum UIGenMouseButton
{
	MS_MOUSENONE = -1, ENAMES(MouseNone)
} UIGenMouseButton;

AUTO_ENUM;
typedef enum UIGenMapZoomMode
{
	UIGenMapZoomModeNone, ENAMES(None)
	UIGenMapZoomModeScaled, ENAMES(Scaled Scale)
	UIGenMapZoomModeFilled, ENAMES(Filled Fill)
	UIGenMapZoomMode_MAX, EIGNORE
} UIGenMapZoomMode;

AUTO_ENUM;
typedef enum UIGenMapScaleMode
{
	UIGenMapScaleWorldUnitsPerPixel,
	UIGenMapScalePixelsPerWorldUnit,
} UIGenMapScaleMode;

#define UI_GEN_MAP_MAX_HIGHRES 9

AUTO_STRUCT;
typedef struct UIGenMapIconDef
{
	const char *pchIcon; AST(POOL_STRING RESOURCEDICT(Textures) NAME(Icon) NAME(Texture))
		// The thing shows this icon on the map, if no other is available.

	const char *pchFormatIcon; AST(POOL_STRING RESOURCEDICT(Textures) NAME(FormatIcon) NAME(FormatTexture))
		// If formatted with the icon as an object results in a valid texture, use that.

	U32 uiColor; AST(SUBTABLE(ColorEnum) NAME(Color) DEFAULT(0xFFFFFFFF))
		// The icon has this color.

	REF_TO(Message) hLabel; AST(NAME(Label))
		// Show this message instead of the entity's name.

	REF_TO(UIStyleFont) hLabelFont; AST(NAME(LabelFont))

	REF_TO(UIStyleFont) hHighlightFont; AST(NAME(HighlightFont))

	UIDirection eLabelAlignment; AST(NAME(LabelAlignment) DEFAULT(UIRight))

		F32 fFrameDuration; AST(DEFAULT(-1))
		S32 iFrameCount; AST(DEFAULT(1))

	F32 fScaleMultiplier; AST(DEFAULT(1.0))
		// The multiplier to apply to the scale once the scale has been calculated 
		// for the zoom level, but before the min scale is applied

	F32 fMinScale; AST(DEFAULT(0.2))
		// Don't scale down below this; only matters if Scale 1 is set.

	F32 fPixelsPerWorldUnit; AST(DEFAULT(2))
		// Fit map this many pixels of the texture to a single world unit

	F32 fBaseScale; AST(DEFAULT(1.0))
		// The base scale of the icon

	S32 iMaxCount;

	S32 iCount; NO_AST
		// Volatile, tracks how many of this def have been instantiated.

	UIGenAction *pOnLeftClick;
	UIGenAction *pOnRightClick;
	UIGenAction *pOnLeftDoubleClick;
		// Actions to run on mouse clicks. A variable, MapIcon, is available, which
		// is the UIGenMapIcon associated with this def.

	S16 iZ; AST(NAME(Z))
		// This icon appears here in the Z order (larger = on top).

	bool bOutOfBounds;
		// This icon is being clamped by either the boundaries or the clamp radius

	bool bClampToEdge;
		// The entity's icon should clamp to the edge of the map.

	bool bRotate;
		// This icon should rotate with whatever it represents.

	bool bScale;
		// This icon should scale roughly with the map.

	bool bScaleToWorld;
		// This icon should be scaled based on the world size

	bool bClip;
		// This icon should clip with the map's box/mask.

	bool bPushable;
		// This icon is pushable if too many icons are close.

	bool bAlwaysShowLabel;
		// Show labels even if the icon is not moused over.

	bool bIgnoreWorldSize;
		// Ignore the world size of this thing and treat it as 0x0x0.

	AtlasTex *pTexture; NO_AST
		// Cached copy of the texture for this IconDef.

} UIGenMapIconDef;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenMapEntityIconDef
{
	TargetType eTargetType; AST(NAME(TargetType) FLAGS)
		// The entity must match all set target types.

	ContactIndicator eContactIndicator; AST(NAME(ContactIndicator))
		// The entity must have this specific contact indicator showing.

	ContactFlags eContactFlags; AST(NAME(ContactFlags) FLAGS)
		// The entity must match all set contact flags.

	MapNotificationType *eaiNotificationTypes;	AST(NAME(NotificationTypes) SUBTABLE(MapNotificationTypeEnum))
		// The entity must have at least one of these notifications

	bool bEscort;
		// The entity must be escorted by the player.

	bool bSavedEntity;
		// The entity must be a saved entity (e.g. a player or an officer or a nemesis).

	bool bGuildmate;
		// The entity must be in the same guild.

	bool bMustPerceive;
		// The entity must be within the player's (powers) perception radius to show.

	bool bPlayerPet;
		// The entity must be the active player's pet.

	UIGenMapIconDef Def; AST(EMBEDDED_FLAT)
		// Entity-agnostic display flags.

} UIGenMapEntityIconDef;

AUTO_STRUCT;
typedef struct UIGenMapKeyIcon
{
	ContactIndicator eContactIndicator; AST(NAME(ContactIndicator) DEFAULT(ContactIndicator_NoInfo))

	MinimapWaypointType eWaypointType; AST(NAME(WaypointType) DEFAULT(MinimapWaypointType_None))

	const char *pchLabel; AST(NAME(Label) POOL_STRING)

	S32 iKeyIndex; AST(NAME(KeyIndex))
		//This is number that appears next to the item in the key, it corresponds to one or more icons on the map

	bool bIsHeader; AST(NAME(IsHeader))
		//If this is set to true, this row is just a header for a particular contact indicator or waypoint type

	const char *pchTexture; AST(POOL_STRING NAME(Icon) NAME(Texture))
} UIGenMapKeyIcon;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenMapNodeIconDef
{
	TargetType eTargetType; AST(NAME(TargetType) FLAGS)
		// The node must match all set target types.

	const char **eaCategory; AST(POOL_STRING NAME(Category))
		// The node must match all categories.

	const char **eaTag; AST(POOL_STRING NAME(Tag))
		// The node must match all tags.

	F32 fInteractDistance;
		// Player must be within this many units of the interact distance.

	bool bMustPerceive;
		// The entity must be within the player's (powers) perception radius to show.

	UIGenMapIconDef Def; AST(EMBEDDED_FLAT)
		// Object-agnostic display flags.

} UIGenMapNodeIconDef;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenMapWaypointIconDef
{
	MinimapWaypointType eType; AST(NAME(Type) DEFAULT(MinimapWaypointType_None))
		// Mission waypoint must match this type.

	const char **eaMission; AST(POOL_STRING RESOURCEDICT(MissionDef) NAME(Mission))
		// Mission must match one of these ref names (or its root must).

	const char **eaContact; AST(POOL_STRING RESOURCEDICT(ContactDef) NAME(Contact))
		// Contact must match one of these def names.

	bool bPrimary;
		// Mission must be the primary mission if set.

	bool bSelected;
		// Mission was clicked in the mission journal or helper and is highlighted. 

	bool bAreaWaypoint;
		// Waypoint must have a non-zero width/height.

	UIGenMapIconDef Def; AST(EMBEDDED_FLAT)
		// Waypoint-agnostic display flags.

} UIGenMapWaypointIconDef;


AUTO_STRUCT;
typedef struct UIGenMapPvPDominationIconDef
{
	UIGenMapIconDef		unowned;
	UIGenMapIconDef		unownedContestedFriendly;
	UIGenMapIconDef		unownedContestedEnemy;
	UIGenMapIconDef		ownedFriendly;
	UIGenMapIconDef		ownedEnemy;
	UIGenMapIconDef		ownedFriendlyContested;
	UIGenMapIconDef		ownedEnemyContested;

	char *pchGroup1Faction;		AST(NAME(Group1Faction))

	char *pchGroup2Faction;		AST(NAME(Group2Faction))

} UIGenMapPvPDominationIconDef;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenMapIcon
{
	EntityRef hEntity; AST(NAME(Entity))
		// Set for entities and nodes that have become entities.

	const char *pchNode; AST(NAME(Node) POOL_STRING)
		// Set for nodes and entities that were once nodes.

	const char *pchMission; AST(NAME(Mission) POOL_STRING)
		// Set for waypoints with associated missions.

	const char *pchTexture; AST(POOL_STRING)
		// Set for things with a texture override (e.g. landmarks)

	Vec3 v3WorldPos; AST(NAME(WorldPos))
	Vec3 v3WorldSize;  AST(NAME(WorldSize))
	Vec2 v2DesiredScreenPos;
	F32 fRotation;
	F32 fZ;
	bool bMouseOver;
	F32 fPushX;
	F32 fPushY;

	char *pchLabel; AST(NAME(Label) ESTRING)
	UIDirection eLabelAlignment; AST(NAME(LabelAlignment) DEFAULT(UIRight))
		// From pDef, or parsed out of the label text.

	UIGenMapIconDef *pDef; AST(UNOWNED)
	CBox ScreenBox; NO_AST

	CBox DesiredScreenBox; NO_AST
		// If the icon was pushed, this was the original box.

	F32 fMouseDistanceSqd;

	bool bHideUnlessRevealed;

	AtlasTex *pTex; NO_AST

	S32 iKeyIndex; AST(NAME(KeyIndex))
		// This is the index that corresponds to the map key for waypoints and contacts

	char *pchIndexLabel; AST(NAME(IndexLabel) ESTRING)
		// This will contain the indices for this icon plus any icons in the same position

	S32 *eaIndices;
		// This is a list of all the indices for the icons that share the same location on the map

	bool bVisited;
} UIGenMapIcon;

AUTO_STRUCT;
typedef struct UIGenMapFakeZoneHighlightBox
{
	F32 fLeft; AST(STRUCTPARAM)
	F32 fTop; AST(STRUCTPARAM)
	F32 fWidth; AST(STRUCTPARAM)
	F32 fHeight; AST(STRUCTPARAM)
} UIGenMapFakeZoneHighlightBox;

AUTO_STRUCT;
typedef struct UIGenMapFakeZoneHighlight
{
	const char *pchMap; AST(STRUCTPARAM POOL_STRING)
		// Name of the source map that this highlight applies to

	UIGenMapFakeZoneHighlightBox Source; AST(NAME(Source))
		// The extents of the area defined by this highlight in the source map.
		// If this region is defined and the source's location is outside of
		// this region, then this highlight won't be applied.

	UIGenMapFakeZoneHighlightBox Mapped; AST(NAME(Mapped))
		// The extents of the above area in the fake zone.
		// If defined, this will allow the map to draw an icon at the virtual location.

	UIGenMapFakeZoneHighlightBox Highlight; AST(NAME(Highlight))
		// The area on the fake map to highlight, if the player is in the above
		// Map and Source area.

	U32 uiColor; AST(DEFAULT(0xffffff80) NAME(Color) FORMAT_COLOR SUBTABLE(ColorEnum))

	bool bShowIcons;
		// Will cause icons to appear in this fake zone.

} UIGenMapFakeZoneHighlight;

AUTO_STRUCT;
typedef struct UIGenMapFakeZoneHighlights
{
	Vec2 v2Size; AST(NAME(Size) REQUIRED)
		// Extents of the numeric space used by Mapped and Highlight boxes,
		//   will be transformed to the FakeZone's v2Size

	UIGenMapFakeZoneHighlight **eaHighlights; AST(NAME(Highlight))

	UIGenMapIconDef Marker;

} UIGenMapFakeZoneHighlights;

AUTO_STRUCT;
typedef struct UIGenMapFakeZone
{
	const char *pchName; AST(KEY POOL_STRING STRUCTPARAM REQUIRED)

	REF_TO(Message) hDisplayName; AST(NAME(DisplayName))

	const char *pchMapFormat; AST(POOL_STRING REQUIRED)
		// e.g. "GalaxyMap{Row}_{Column}"

	const char *pchSmallMap;  AST(POOL_STRING RESOURCEDICT(Texture) REQUIRED)
		// e.g. "GalaxyMap_Small"

	S16 aiTileCount[2]; AST(NAME(TileCount))
		// Number of columns and rows in the map.

	Vec2 v2WorldSize; AST(NAME(WorldSize) REQUIRED)
		// Extents of the map in "map units", should be roughly commensurate with other scales.

	Vec2 v2WorldOffset; AST(NAME(WorldOffset))
		// Extents of the map in "map units", should be roughly commensurate with other scales.

	bool bShowIcons;
		// Will cause icons to appear in all fake zone highlights.

	UIGenMapFakeZoneHighlights *pHighlights; AST(NAME(Highlights))
		// Definitions of highlight areas that show the player's virtual location. For example,
		//   if they are on a child map "inside" of another map the highlight can show the
		//   parent's map, or the parent's parent's map, etc.

} UIGenMapFakeZone;

AUTO_STRUCT;
typedef struct UIGenMapRadius
{
	F32 fClampRadius; AST(STRUCTPARAM REQUIRED)
		// Clamp the map to a circle this large (scaled by gen scale); if -1,
		// calculate the radius automatically.

	Vec2 v2Offset; AST(NAME(Offset))
} UIGenMapRadius;

// This is a special texture assembly
AUTO_STRUCT;
typedef struct UIGenMapTextureAssembly
{
	UIGenTextureAssembly MapAssembly; AST(EMBEDDED_FLAT)
	
	S16 iZ; AST(NAME(Z))
	
} UIGenMapTextureAssembly;


AUTO_STRUCT;
typedef struct UIGenMap
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeMap))
	UIGenMapEntityIconDef **eaEntityIcons; AST(NAME(EntityIcon))
	UIGenMapNodeIconDef **eaNodeIcons; AST(NAME(NodeIcon))
	UIGenMapWaypointIconDef **eaWaypointIcons; AST(NAME(WaypointIcon))
	UIGenMapTextureAssembly *pMapAssembly; AST(NAME(MapAssembly))
	UIGenMapPvPDominationIconDef *pPvPDominationIcons; AST(NAME(PvPDominationIcons))	

	UIGenMapIconDef *pPlayerIcon;
	UIGenMapIconDef *pCameraIcon;

	UIGenMapFakeZone **eaFakeZones; AST(NAME(FakeZone))

	UIGenMapScaleMode eScaleMode; AST(DEFAULT(UIGenMapScaleWorldUnitsPerPixel))

	F32 fScaleDefault;
	F32 fScaleMax; AST(DEFAULT(1.5))
	F32 fScaleMin; AST(DEFAULT(0.1))
		// Scale factor, in World Units per Pixel.
		// Map scale factor, in percentage of map size (*NOT* any map units).
		// For example, a ScaleMax of 2 means the entire map should be able to
		// tile about 2x2 in the visible map area; 0.1 means that about
		// 10% of the map's width/height (e.g. 1% of total area) should be
		// visible. This helps visible map area stay roughly commensurate
		// between map moves.
		// e.g. a scale of 10 would mean 10 feet = 1 pixel

	F32 fScaleStep; AST(DEFAULT(0.1))

	UIGenMapRadius ClampRadius;
	S16 uiLeftIconPadding; AST(NAME(LeftIconPadding) SUBTABLE(UISizeEnum))
	S16 uiTopIconPadding; AST(NAME(TopIconPadding) SUBTABLE(UISizeEnum))
	S16 uiRightIconPadding; AST(NAME(RightIconPadding) SUBTABLE(UISizeEnum))
	S16 uiBottomIconPadding; AST(NAME(BottomIconPadding) SUBTABLE(UISizeEnum))

	U32 uiFogColor; AST(NAME(OutdoorFogColor) SUBTABLE(ColorEnum))
		// Color for fog of war overlay; don't specify to disable fog of war.
	U32 uiRoomFogColor; AST(NAME(RoomFogColor) SUBTABLE(ColorEnum))
		// Color for room fog overlays; don't specify to disable fog.
	U32 uiRoomInactiveColor;  AST(NAME(RoomInactiveColor) SUBTABLE(ColorEnum))
		// Color for rooms that are not fogged but aren't active; default is RoomActiveColor.
	U32 uiRoomActiveColor;  AST(NAME(RoomActiveColor) SUBTABLE(ColorEnum) DEFAULT(0xFFFFFFFF))
		// Color for room tiles.

	REF_TO(UIStyleFont) hLabelFont; AST(NAME(LabelFont))
	REF_TO(UIStyleFont) hHighlightFont; AST(NAME(HighlightFont))

	const char *pchBackgroundTex; AST(NAME(BackgroundTexture) POOL_STRING RESOURCEDICT(Texture))
	U32 uiBackgroundColor; AST(NAME(BackgroundColor) SUBTABLE(ColorEnum) DEFAULT(0xFFFFFFFF))
		// Special background texture that has the mask applied.
		// For a normal background, use the assembly field.

	const char *pchMask; AST(NAME(Mask) POOL_STRING RESOURCEDICT(Texture))
		// The mask to apply to the background as well as all unclamped map icons

	UIGenAction *pOnLeftClick;
	UIGenAction *pOnRightClick;
	UIGenAction *pOnLeftDoubleClick;
		// Actions to run when the user interacts with an unmarked point.
		// A special MapIcon is constructed when running these.

	F32 fRangeY; AST(DEFAULT(10))
	const char *pchUpwardIcon; AST( POOL_STRING RESOURCEDICT(Texture))
	const char *pchDownwardIcon; AST( POOL_STRING RESOURCEDICT(Texture))
	U32 uiUpwardIconColor; AST(NAME(UpwardIconColor) SUBTABLE(ColorEnum) FORMAT_COLOR)
	U32 uiDownwardIconColor; AST(NAME(DownwardIconColor) SUBTABLE(ColorEnum) FORMAT_COLOR)
		// Annotations to put on icons if they are outside of a certain Y range, indicating
		// whether they are up or down.

	MouseButton ePannable; AST(DEFAULT(MS_LEFT) SUBTABLE(MouseButtonEnum))
		// The mouse button that must be used  to pan the map
	bool bFollowPlayer;
		// If true, follow the player by default; otherwise, follow nothing.
	F32 fFollowSpeed; AST(DEFAULT(.5))
		// The rate at which the camera will move toward the target when following. 

	UIGenMapZoomMode eMapZoomMode;
		// Determines a set of zooming and panning modes for the map

	bool bRememberScales;

	bool bShowMapKey; AST(DEFAULT(false))

	bool bShowMissionNumbers;
		// If True, shows mission numbers. GenMapDrawMissionNumbers must also be 1 for this to take effect.
} UIGenMap;

AUTO_STRUCT;
typedef struct UIGenMapState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeMap))
	UIGenMapIcon **eaIcons;

	F32 fMapScale;
		// Scale factor, in percentage units (see notes on ScaleMax/ScaleMin/ScaleStep).

	F32 fPixelsPerWorldUnit;		AST(DEFAULT(.5))
		// Pixels per world unit; gen scale is already applied.

	F32 fSavedPixelsPerWorldUnit;
		// Pixels per world unit; saved regardless of clamping

	Vec3 v3WorldCenter;
		// Current center of the map. (Not really - RMARR [2/6/12])

	Vec3 v3Target;
		// Center of the "active thing" - e.g. the player. Used for some distance
		// sorting.

	Vec3 v3TargetPrev;
		// Previous location of the "active thing". Used for determining if the player 
		// has moved since the previous frame. 

	Vec3 v3DragStartTarget;
		// Start point of a mouse drag, in world coordinates.

	Vec3 v3DelayedTarget;
		// Used to store a position we will center on after the gen has been created.

	bool bHasDelayedTarget;
		// Whether we have a delayed target set.

	S32 iGrabbedX;
	S32 iGrabbedY;
		// Start point of a mouse drag, in screen coordinates.

	S32 hTarget;
		// Follow this entity. If 0, follow the player. If -1, follow nothing.

	bool bFollowing;
		// Determines if we are currently following the target

	const char *pchFakeZone; AST(POOL_STRING)
	UIGenMapFakeZone *pFakeZone; NO_AST
		// Currently active fake zone map.

	MapRevealInfo FakeZoneReveal;
		// Reveal info for the fake zone - really just bounding info.

	S32 *eaiRevealBits;
		// Map revealed bits, used to see when to regenerate the map texture.

	MapRevealInfo *pReveal; NO_AST
		// Revealed map area, also contains canonical bounds info.

	AtlasTex *pBackgroundTex; NO_AST
	AtlasTex *pMask; NO_AST
	AtlasTex *pUpwardIcon; NO_AST
	AtlasTex *pDownwardIcon; NO_AST
		// Cached textures loaded.

	void *pFogTexData; NO_AST
	BasicTexture *pFogOverlay; NO_AST
		// Fog texture data and handle.

	const char* pchSelectedMissionRefString; AST( POOL_STRING )
		// Keeps track of which mission to highlight

	WorldRegionType ePreviousRegionType;
	UIGenAction **eaQueuedActions;

	const char *pchOldMapName; AST( POOL_STRING )
		//The name of the map last frame; used to detect map change

	WorldRegionType eOldRegionType;
		//The type of the region last frame; also used for map change detection

	F32 fSaveScaleTimer;
		// After the scale size has been changed, wait a few seconds, then 
		// send the server command to save the scale. 

} UIGenMapState;

AUTO_STRUCT;
typedef struct UIGenMapTexture
{
	RoomPartition *pPartition; NO_AST
	RoomConnGraph *pGraph; NO_AST
	AtlasTex *pTex; NO_AST
	CBox TexBox; NO_AST
	int iRoomIndex;
	float fDistFromCenterSq;
} UIGenMapTexture;

bool GenMapGetNextPositionForKeyIndex(UIGen *pGen, UIGenMapKeyIcon *pIcon, Vec3 *v3pPos);