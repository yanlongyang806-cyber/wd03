/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include "GenesisMapDescriptionEditor.h"

#include "AnimList_Common.h"
#include "Color.h"
#include "DoorTransitionCommon.h"
#include "EditLibUIUtil.h"
#include "EditorPrefs.h"
#include "EntityMovementDoor.h"
#include "GameActionEditor.h"
#include "Genesis.h"
#include "GfxDebug.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "logging.h"
#include "MultiEditField.h"
#include "NameGen.h"
#include "ObjectLibrary.h"
#include "ResourceSearch.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "WorldEditorUI.h"
#include "WorldLibStructs.h"
#include "contact_common.h"
#include "gameaction_common.h"
#include "gameeditorshared.h"
#include "gimmeDLLWrapper.h"
#include "interaction_common.h"
#include "partition_enums.h"
#include "tokenstore.h"
#include "rand.h"
#include "StringUtil.h"
#include "wlGenesis.h"
#include "wlGenesisExterior.h"
#include "wlGenesisExteriorDesign.h"
#include "wlGenesisInterior.h"
#include "wlGenesisMissions.h"
#include "wlGenesisPopulate.h"
#include "wlGenesisRoom.h"
#include "wlGenesisSolarSystem.h"
#include "worldgrid.h"
#include "EString.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static bool gIndexChanged = false;

static char **geaScopes = NULL;
static const char **geaOptionalActionCategories = NULL;

extern EMEditor *s_MapDescEditor;

static UISkin *gBoldExpanderSkin;

static char **gGeoTypeTags = NULL;
static char **gEcosystemTags = NULL;
static char **gInteriorKitTags = NULL;
static char **gBackdropTags = NULL;
static char **gRoomTags = NULL;
static char **gPathTags = NULL;
static char **gDetailTags = NULL;
static char **gObjectLibTags = NULL;
static char **gSolarSystemEnvTags = NULL;
static char **gChallengeTags = NULL;
static const char **gChallengeNames = NULL;
static char **gPhraseNames = NULL;

static MapDescEditDoc *gEmbeddedDoc = NULL;

/// Clipboard data
static GenesisMissionChallenge g_GMDClipboardChallenge;
static GenesisMissionPrompt g_GMDClipboardPrompt;
static GenesisMissionObjective g_GMDClipboardObjective;
static GenesisMissionDescription g_GMDClipboardMission;
static GenesisMapType g_GMDClipboardLayoutType = GenesisMapType_None;
static GenesisSolSysLayout g_GMDClipboardSolSys;
static GenesisInteriorLayout g_GMDClipboardInterior;
static GenesisExteriorLayout g_GMDClipboardExterior;
static GenesisLayoutRoom g_GMDClipboardRoom;
static GenesisLayoutPath g_GMDClipboardPath;
static GenesisMissionPortal g_GMDClipboardPortal;
static ShoeboxPointList g_GMDClipboardPointList;
static ShoeboxPoint g_GMDClipboardPoint;
	
//---------------------------------------------------------------------------------------------------
// Function Prototypes and type definitions
//---------------------------------------------------------------------------------------------------

#define X_OFFSET_BASE    15
#define X_OFFSET_INDENT  15
#define X_OFFSET_CONTROL 125

#define STANDARD_ROW_HEIGHT  26
#define LABEL_ROW_HEIGHT  20
#define SEPARATOR_HEIGHT      11

static void GMDFieldChangedCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc);
static bool GMDFieldPreChangeCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc);
static void GMDMapDescChanged(MapDescEditDoc *pDoc, bool bUndoable);
static void GMDMapDescPreSaveFixup(GenesisMapDescription *pMapDesc);
static void GMDUpdateDisplay(MapDescEditDoc *pDoc);
static F32 GMDRefreshObjectTag(MapDescEditDoc *pDoc, GMDObjectTagGroup *pGroup, F32 y, int index, SSTagObj ***peaTags, SSTagObj *pTag, SSTagObj *pOrigTag, bool include_offset, bool include_dist, bool include_count);
static F32 GMDRefreshObjectRef(MapDescEditDoc *pDoc, GMDObjectRefGroup *pGroup, F32 y, int index, SSLibObj ***peaObjects, SSLibObj *pObj, SSLibObj *pOrigObj, bool include_offset, bool include_dist, bool include_count);
static void GMDFreeObjectRefGroup(GMDObjectRefGroup *pGroup);
static void GMDFreeObjectTagGroup(GMDObjectTagGroup *pGroup);
static bool GMDQueryIfNotGenesisDirs( char** dirs, char** names );

// iteration functions
static GenesisMissionChallenge* GMDGetNextChallenge(GenesisMissionChallenge **ppChallenges, const char *pcLayoutName, int *idx, bool backwards);
static GenesisMissionPrompt* GMDGetNextPrompt(GenesisMissionPrompt **ppPrompts, const char *pcLayoutName, int *idx, bool backwards);
static GenesisMissionPortal* GMDGetNextPortal(GenesisMissionPortal **ppPortals, const char *pcLayoutName, int *idx, bool backwards);

//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

static int GMDStringCompare(const char** left, const char** right)
{
	return stricmp(*left,*right);
}

static int GMDRoomLayoutPairCompare(const GMDRoomLayoutPair** left, const GMDRoomLayoutPair** right)
{
	int result = stricmp((*left)->layout, (*right)->layout );
	if( result != 0 ) {
		return result;
	}

	result = stricmp((*left)->room, (*right)->room);
	return result;
}


static void GMDSplitRoomLayoutEntry( const char* entry, char** outRoom, char** outLayout )
{
	char buffer[ 512 ];
	strcpy( buffer, entry );

	if( buffer[0] == '\0' ) {
		*outRoom = NULL;
		*outLayout = NULL;
	} else if( strchrCount( buffer, ':' ) != 1 ) {
		return;
	} else {
		char* ctx = NULL;
		char* token = strtok_s( buffer, ":", &ctx );
		StructCopyString( outLayout, token );
		token = strtok_s( NULL, ":", &ctx );
		StructCopyString( outRoom, token );
	}
}

static void GMDRenderRoomLayoutEntry( UIComboBox* cb, S32 row, bool inBox, UserData ignored, char** outEstr )
{
	const GMDRoomLayoutPair*** allRooms = (const GMDRoomLayoutPair***)cb->model;
	
	if( row < 0 || row >= eaSize(allRooms)) {
		return;
	}

	if( (*allRooms)[row]->layout == NULL && (*allRooms)[row]->room == NULL ) {
		estrPrintf( outEstr, "" );
	}

	estrPrintf(outEstr, "%s:%s", (*allRooms)[row]->layout, (*allRooms)[row]->room);
}

static void GMDRefreshTextEntryRoomLayoutPair(
		UITextEntry** ppEntry, MapDescEditDoc* pDoc, UIActivationFunc cbChange, UserData data,
		UIWidget* pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight )
{
	if( !*ppEntry ) {
		UIComboBox* cb = ui_ComboBoxCreate( 0, 0, 1, NULL, NULL, NULL );
		*ppEntry = ui_TextEntryCreate( "", 0, 0 );
		ui_TextEntrySetComboBox( *ppEntry, cb );
		ui_ComboBoxSetModelNoCallback( cb, NULL, &pDoc->eaAllRooms );
		cb->drawSelected = true;
		ui_ComboBoxSetTextCallback( cb, GMDRenderRoomLayoutEntry, NULL );
		ui_TextEntrySetFinishedCallback( *ppEntry, cbChange, data );
		ui_WidgetAddChild( pParent, UI_WIDGET( *ppEntry ));
	}

	ui_WidgetSetPaddingEx( UI_WIDGET( *ppEntry ), 0, padRight, 0, 0 );
	ui_WidgetSetWidthEx( UI_WIDGET( *ppEntry ), w, wUnit );
	ui_WidgetSetPositionEx( UI_WIDGET( *ppEntry ), x, y, xPercent, 0, UITopLeft );
}

static void GMDTextEntrySetRoomLayout( UITextEntry* textEntry, const char* room, const char* layout, const char* orig_room, const char* orig_layout )
{
	if( room || layout ) {
		char buffer[ 512 ];
		sprintf( buffer, "%s:%s", layout, room );
		ui_TextEntrySetText( textEntry, buffer );
	}

	ui_SetChanged( UI_WIDGET(textEntry), stricmp_safe( room, orig_room ) != 0 || stricmp_safe( layout, orig_layout ));
}

static void GMDTextEntrySetWhenRoomLayouts( UITextEntry* textEntry, const GenesisWhenRoom** rooms, const GenesisWhenRoom** orig_rooms )
{
	char* buffer = NULL;
	int it;

	for( it = 0; it != eaSize( &rooms ); ++it ) {
		estrConcatf( &buffer, "%s:%s ", rooms[it]->layoutName, rooms[it]->roomName );
	}
	if( estrLength( &buffer ) > 0 ) {
		estrSetSize( &buffer, estrLength( &buffer ) - 1 );
	}

	ui_TextEntrySetText( textEntry, buffer );
	estrDestroy( &buffer );

	// check for difference
	if( eaSize( &rooms ) != eaSize( &orig_rooms )) {
		ui_SetChanged( UI_WIDGET(textEntry), true );
	} else {
		bool changed = false;
		for( it = 0; it != eaSize( &rooms ); ++it ) {
			assert( orig_rooms );
			if(   stricmp_safe(rooms[it]->roomName, orig_rooms[it]->roomName) != 0
				  || stricmp_safe(rooms[it]->layoutName, orig_rooms[it]->layoutName) != 0) {
				changed = true;
				break;
			}
		}

		ui_SetChanged( UI_WIDGET(textEntry), changed);
	}
}

static void GMDStructFreeString(char *pcStructString)
{
	StructFreeString(pcStructString);
}

static const char* GMDGetActiveLayoutName(MapDescEditDoc *pDoc)
{
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		return pDoc->pEditingSolSys->name;
	case GenesisMapType_Exterior:
		return pDoc->pEditingExterior->name;
	case GenesisMapType_Interior:
		return pDoc->pEditingInterior->name;
	default:
		assert(false);
	}
	return NULL;
}

static void GMDSetActiveLayoutName(MapDescEditDoc *pDoc, const char *pcNewName)
{
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		if(pDoc->pEditingSolSys->name)
			StructFreeString(pDoc->pEditingSolSys->name);
		pDoc->pEditingSolSys->name = StructAllocString(pcNewName);
		break;
	case GenesisMapType_Exterior:
		if(pDoc->pEditingExterior->name)
			StructFreeString(pDoc->pEditingExterior->name);
		pDoc->pEditingExterior->name = StructAllocString(pcNewName);
		break;
	case GenesisMapType_Interior:
		if(pDoc->pEditingInterior->name)
			StructFreeString(pDoc->pEditingInterior->name);
		pDoc->pEditingInterior->name = StructAllocString(pcNewName);
		break;
	default:
		assert(false);
	}
}

static void GMDUpdateSelectedLayout(MapDescEditDoc *pDoc)
{
	if(!pDoc->pMapDesc)
		return;

	pDoc->pEditingSolSys = NULL;
	pDoc->pEditingInterior = NULL;
	pDoc->pEditingExterior = NULL;
	pDoc->pOrigEditingSolSys = NULL;
	pDoc->pOrigEditingInterior = NULL;
	pDoc->pOrigEditingExterior = NULL;

	if(	pDoc->EditingMapType == GenesisMapType_None )
	{
		pDoc->iEditingLayoutIdx = 0;
		if(eaSize(&pDoc->pMapDesc->solar_system_layouts) > 0)
			pDoc->EditingMapType = GenesisMapType_SolarSystem;
		else if(eaSize(&pDoc->pMapDesc->interior_layouts) > 0)
			pDoc->EditingMapType = GenesisMapType_Interior;
		else if(pDoc->pMapDesc->exterior_layout)
			pDoc->EditingMapType = GenesisMapType_Exterior;
		else
			assert(false);
	}
	if(pDoc->pTypeCombo)
		ui_ComboBoxSetSelectedEnum(pDoc->pTypeCombo, pDoc->EditingMapType);
	if(pDoc->pLayoutCombo)
		ui_ComboBoxSetSelected(pDoc->pLayoutCombo, pDoc->iEditingLayoutIdx);

	if(pDoc->pcEditingLayoutName)
		StructFreeString(pDoc->pcEditingLayoutName);

	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		assert(pDoc->iEditingLayoutIdx >= 0 && pDoc->iEditingLayoutIdx < eaSize(&pDoc->pMapDesc->solar_system_layouts));
		pDoc->pEditingSolSys = pDoc->pMapDesc->solar_system_layouts[pDoc->iEditingLayoutIdx];
		if(pDoc->pOrigMapDesc && eaSize(&pDoc->pOrigMapDesc->solar_system_layouts) > pDoc->iEditingLayoutIdx)
			pDoc->pOrigEditingSolSys = pDoc->pOrigMapDesc->solar_system_layouts[pDoc->iEditingLayoutIdx];
		pDoc->pcEditingLayoutName = StructAllocString(pDoc->pEditingSolSys->name);
		break;
	case GenesisMapType_Exterior:
		pDoc->pEditingExterior = pDoc->pMapDesc->exterior_layout;
		if(pDoc->pOrigMapDesc)
			pDoc->pOrigEditingExterior = pDoc->pOrigMapDesc->exterior_layout;
		pDoc->pcEditingLayoutName = StructAllocString(pDoc->pEditingExterior->name);
		break;
	case GenesisMapType_Interior:
		assert(pDoc->iEditingLayoutIdx >= 0 && pDoc->iEditingLayoutIdx < eaSize(&pDoc->pMapDesc->interior_layouts));
		pDoc->pEditingInterior = pDoc->pMapDesc->interior_layouts[pDoc->iEditingLayoutIdx];
		if(pDoc->pOrigMapDesc && eaSize(&pDoc->pOrigMapDesc->interior_layouts) > pDoc->iEditingLayoutIdx)
			pDoc->pOrigEditingInterior = pDoc->pOrigMapDesc->interior_layouts[pDoc->iEditingLayoutIdx];
		pDoc->pcEditingLayoutName = StructAllocString(pDoc->pEditingInterior->name);
		break;
	default:
		assert(false);
		break;
	}
}

static void GMDMapDescUndoCB(MapDescEditDoc *pDoc, GMDUndoData *pData)
{
	// Put the undo mapdesc into the editor
	StructDestroy(parse_GenesisMapDescription, pDoc->pMapDesc);
	pDoc->pMapDesc = StructClone(parse_GenesisMapDescription, pData->pPreMapDesc);
	pDoc->EditingMapType = GenesisMapType_None;
	GMDUpdateSelectedLayout(pDoc);
	if (pDoc->pNextUndoMapDesc) {
		StructDestroy(parse_GenesisMapDescription, pDoc->pNextUndoMapDesc);
	}
	pDoc->pNextUndoMapDesc= StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);

	// Update the UI
	GMDMapDescChanged(pDoc, false);
}


static void GMDMapDescRedoCB(MapDescEditDoc *pDoc, GMDUndoData *pData)
{
	// Put the undo mapdesc into the editor
	StructDestroy(parse_GenesisMapDescription, pDoc->pMapDesc);
	pDoc->pMapDesc = StructClone(parse_GenesisMapDescription, pData->pPostMapDesc);
	pDoc->EditingMapType = GenesisMapType_None;
	GMDUpdateSelectedLayout(pDoc);
	if (pDoc->pNextUndoMapDesc) {
		StructDestroy(parse_GenesisMapDescription, pDoc->pNextUndoMapDesc);
	}
	pDoc->pNextUndoMapDesc= StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);

	// Update the UI
	GMDMapDescChanged(pDoc, false);
}


static void GMDMapDescUndoFreeCB(MapDescEditDoc *pDoc, GMDUndoData *pData)
{
	// Free the memory
	StructDestroy(parse_GenesisMapDescription, pData->pPreMapDesc);
	StructDestroy(parse_GenesisMapDescription, pData->pPostMapDesc);
	free(pData);
}

static void GMDFreeLayoutKitInfo(GMDLayoutDetailKitInfo *pKitInfo, bool free_widgets)
{
	if(free_widgets) {
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailSpecLabel);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailTagsLabel);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutVaryPerRoomLabel);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailNameLabel);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailDensityLabel);
	}

	MEFieldSafeDestroy(&pKitInfo->pLayoutDetailSpecField);
	MEFieldSafeDestroy(&pKitInfo->pLayoutDetailTagsField);
	MEFieldSafeDestroy(&pKitInfo->pLayoutVaryPerRoomField);
	MEFieldSafeDestroy(&pKitInfo->pLayoutDetailNameField);
	MEFieldSafeDestroy(&pKitInfo->pLayoutDetailDensityField);
}

static void GMDFreeLayoutInfoGroup(MapDescEditDoc *pDoc, GMDLayoutInfoGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pLayoutExtNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutExtTemplateNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutSideTrailMinField);
	MEFieldSafeDestroy(&pGroup->pLayoutSideTrailMaxField);
	MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeSpecField);
	MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeTagsField);
	MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutEcosystemSpecField);
	MEFieldSafeDestroy(&pGroup->pLayoutEcosystemTagsField);
	MEFieldSafeDestroy(&pGroup->pLayoutEcosystemNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMinXField);
	MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMinZField);
	MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMaxXField);
	MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMaxZField);
	MEFieldSafeDestroy(&pGroup->pLayoutBufferField);
	MEFieldSafeDestroy(&pGroup->pLayoutColorShiftField);
	MEFieldSafeDestroy(&pGroup->pLayoutExtVertDirField);
	MEFieldSafeDestroy(&pGroup->pLayoutExtShapeField);
	MEFieldSafeDestroy(&pGroup->pLayoutMaxRoadAngleField);
	MEFieldSafeDestroy(&pGroup->pLayoutExtDetailNoSharingField);
	GMDFreeLayoutKitInfo(&pGroup->ExtDetailKit1, false);
	GMDFreeLayoutKitInfo(&pGroup->ExtDetailKit2, false);
	MEFieldSafeDestroy(&pGroup->pLayoutIntNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutIntTemplateNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutRoomKitSpecField);
	MEFieldSafeDestroy(&pGroup->pLayoutRoomKitTagsField);
	MEFieldSafeDestroy(&pGroup->pLayoutRoomKitNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutLightKitSpecField);
	MEFieldSafeDestroy(&pGroup->pLayoutLightKitTagsField);
	MEFieldSafeDestroy(&pGroup->pLayoutLightKitNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutIntVertDirField);
	MEFieldSafeDestroy(&pGroup->pLayoutIntDetailNoSharingField);
	GMDFreeLayoutKitInfo(&pGroup->IntDetailKit1, false);
	GMDFreeLayoutKitInfo(&pGroup->IntDetailKit2, false);
	MEFieldSafeDestroy(&pGroup->pLayoutSolarSystemEnvTagsField);
	MEFieldSafeDestroy(&pGroup->pLayoutSolarSystemNameField);
	MEFieldSafeDestroy(&pGroup->pLayoutIntLayoutInfoSpecifierField);
	MEFieldSafeDestroy(&pGroup->pLayoutExtLayoutInfoSpecifierField);
	MEFieldSafeDestroy(&pGroup->pLayoutEncJitterTypeField);
	MEFieldSafeDestroy(&pGroup->pLayoutEncJitterPosField);
	MEFieldSafeDestroy(&pGroup->pLayoutEncJitterRotField);
	MEFieldSafeDestroy(&pGroup->pLayoutBackdropSpecField);
	MEFieldSafeDestroy(&pGroup->pLayoutBackdropTagsField);
	MEFieldSafeDestroy(&pGroup->pLayoutBackdropNameField);

	ui_ExpanderGroupRemoveExpander(pDoc->pLayoutExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreeShoeboxGroup(MapDescEditDoc *pDoc, GMDLayoutShoeboxGroup *pGroup)
{
	int j;

	for(j=eaSize(&pGroup->eaDetailRefGroups)-1; j>=0; --j) {
		GMDFreeObjectRefGroup(pGroup->eaDetailRefGroups[j]);
	}
	eaDestroy(&pGroup->eaDetailRefGroups);
	for(j=eaSize(&pGroup->eaDetailTagGroups)-1; j>=0; --j) {
		GMDFreeObjectTagGroup(pGroup->eaDetailTagGroups[j]);
	}
	eaDestroy(&pGroup->eaDetailTagGroups);

	ui_ExpanderGroupRemoveExpander(pDoc->pLayoutExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}

static void GMDFreeRoomKitInfo(GMDRoomDetailKitInfo *pKitInfo)
{
	MEFieldSafeDestroy(&pKitInfo->pDetailSpecField);
	MEFieldSafeDestroy(&pKitInfo->pDetailTagsField);
	MEFieldSafeDestroy(&pKitInfo->pDetailNameField);
	MEFieldSafeDestroy(&pKitInfo->pDetailCustomDensityField);
	MEFieldSafeDestroy(&pKitInfo->pDetailDensityField);
}

static void GMDFreeRoomGroup(GMDRoomGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pRoomSpecField);
	MEFieldSafeDestroy(&pGroup->pRoomTagsField);
	MEFieldSafeDestroy(&pGroup->pRoomNameField);
	MEFieldSafeDestroy(&pGroup->pOffMapField);
	GMDFreeRoomKitInfo(&pGroup->DetailKit1);
	GMDFreeRoomKitInfo(&pGroup->DetailKit2);

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pLayoutExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreePathGroup(GMDPathGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pPathSpecField);
	MEFieldSafeDestroy(&pGroup->pPathTagsField);
	MEFieldSafeDestroy(&pGroup->pPathNameField);
	GMDFreeRoomKitInfo(&pGroup->DetailKit1);
	GMDFreeRoomKitInfo(&pGroup->DetailKit2);
	MEFieldSafeDestroy(&pGroup->pMinLengthField);
	MEFieldSafeDestroy(&pGroup->pMaxLengthField);
	MEFieldSafeDestroy(&pGroup->pStartField);
	MEFieldSafeDestroy(&pGroup->pEndField);

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pLayoutExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}

static void GMDFreeObjectRefGroup(GMDObjectRefGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pObjectLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pObjectEntry);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinCountLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxCountLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinDistLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxDistLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinHorizLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxHorizLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinVertLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxVertLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDetachedLabel);

	MEFieldSafeDestroy(&pGroup->pMinCountField);
	MEFieldSafeDestroy(&pGroup->pMaxCountField);
	MEFieldSafeDestroy(&pGroup->pMinDistField);
	MEFieldSafeDestroy(&pGroup->pMaxDistField);
	MEFieldSafeDestroy(&pGroup->pMinHorizField);
	MEFieldSafeDestroy(&pGroup->pMaxHorizField);
	MEFieldSafeDestroy(&pGroup->pMinVertField);
	MEFieldSafeDestroy(&pGroup->pMaxVertField);
	MEFieldSafeDestroy(&pGroup->pDetachedField);

	free(pGroup);
}


static void GMDFreeObjectTagGroup(GMDObjectTagGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pTagLabel);
	MEFieldSafeDestroy(&pGroup->pTagField);
	ui_WidgetQueueFreeAndNull(&pGroup->pTagErrorPane);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinCountLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxCountLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinDistLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxDistLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinHorizLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxHorizLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMinVertLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pMaxVertLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pDetachedLabel);

	MEFieldSafeDestroy(&pGroup->pMinCountField);
	MEFieldSafeDestroy(&pGroup->pMaxCountField);
	MEFieldSafeDestroy(&pGroup->pMinDistField);
	MEFieldSafeDestroy(&pGroup->pMaxDistField);
	MEFieldSafeDestroy(&pGroup->pMinHorizField);
	MEFieldSafeDestroy(&pGroup->pMaxHorizField);
	MEFieldSafeDestroy(&pGroup->pMinVertField);
	MEFieldSafeDestroy(&pGroup->pMaxVertField);
	MEFieldSafeDestroy(&pGroup->pDetachedField);

	free(pGroup);
}


static void GMDFreePointGroup(GMDPointGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pRadiusField);
	MEFieldSafeDestroy(&pGroup->pMinClusterDistField);
	MEFieldSafeDestroy(&pGroup->pMaxClusterDistField);
	MEFieldSafeDestroy(&pGroup->pDistFromPrevField);
	MEFieldSafeDestroy(&pGroup->pFacingField);
	MEFieldSafeDestroy(&pGroup->pFacingOffsetField);

	free(pGroup);
}


static void GMDFreePointListGroup(GMDPointListGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pStartField);
	MEFieldSafeDestroy(&pGroup->pEndField);
	MEFieldSafeDestroy(&pGroup->pOrbitField);
	MEFieldSafeDestroy(&pGroup->pDistField);
	MEFieldSafeDestroy(&pGroup->pEquiField);
	MEFieldSafeDestroy(&pGroup->pFollowField);
	MEFieldSafeDestroy(&pGroup->pListTypeField);
	MEFieldSafeDestroy(&pGroup->pMinRadiusField);
	MEFieldSafeDestroy(&pGroup->pMaxRadiusField);
	MEFieldSafeDestroy(&pGroup->pMinTiltField);
	MEFieldSafeDestroy(&pGroup->pMaxTiltField);
	MEFieldSafeDestroy(&pGroup->pMinYawField);
	MEFieldSafeDestroy(&pGroup->pMaxYawField);
	MEFieldSafeDestroy(&pGroup->pMinHorizField);
	MEFieldSafeDestroy(&pGroup->pMaxHorizField);
	MEFieldSafeDestroy(&pGroup->pMinVertField);
	MEFieldSafeDestroy(&pGroup->pMaxVertField);

	eaDestroyEx(&pGroup->eaOrbitRefGroups, GMDFreeObjectRefGroup);
	eaDestroyEx(&pGroup->eaOrbitTagGroups, GMDFreeObjectTagGroup);
	eaDestroyEx(&pGroup->eaCurveRefGroups, GMDFreeObjectRefGroup);
	eaDestroyEx(&pGroup->eaCurveTagGroups, GMDFreeObjectTagGroup);
	eaDestroyEx(&pGroup->eaPointGroups, GMDFreePointGroup);

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pLayoutExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreeChallengeStartGroup(MapDescEditDoc *pDoc, GMDChallengeStartGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pEntryFromMapField);
	MEFieldSafeDestroy(&pGroup->pEntryFromInteractableField);
	MEFieldSafeDestroy(&pGroup->pStartTransitionOverrideField);
	MEFieldSafeDestroy(&pGroup->pHasDoorField);
	MEFieldSafeDestroy(&pGroup->pExitTransitionOverrideField);
	MEFieldSafeDestroy(&pGroup->pExitFromField);
	MEFieldSafeDestroy(&pGroup->pExitUsePetCostumeField);
	MEFieldSafeDestroy(&pGroup->pExitCostumeField);
	MEFieldSafeDestroy(&pGroup->pExitPetCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinueField);
	MEFieldSafeDestroy(&pGroup->pContinueFromField);
	MEFieldSafeDestroy(&pGroup->pContinueChallengeField);
	MEFieldSafeDestroy(&pGroup->pContinueMapField);
	MEFieldSafeDestroy(&pGroup->pContinueTransitionOverrideField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptUsePetCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptPetCostumeField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptButtonTextField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptCategoryField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptPriorityField);
	MEFieldSafeDestroy(&pGroup->pContinuePromptTitleTextField);
	eaDestroyEx(&pGroup->eaContinuePromptBodyTextField, MEFieldDestroy);

	ui_ExpanderGroupRemoveExpander(pDoc->pChallengeExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}

static void GMDFreeWhenGroup(GMDWhenGroup** pGroup)
{
	MEFieldSafeDestroy(&(*pGroup)->pTypeField);
	ui_WidgetQueueFreeAndNull(&(*pGroup)->pRoomsText);
	MEFieldSafeDestroy(&(*pGroup)->pChallengeNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pObjectiveNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pPromptNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pContactNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pCritterDefNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pItemDefNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pChallengeNumToCompleteField);
	MEFieldSafeDestroy(&(*pGroup)->pCritterDefNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pCritterGroupNamesField);
	MEFieldSafeDestroy(&(*pGroup)->pCritterNumToKillField);
	MEFieldSafeDestroy(&(*pGroup)->pItemCountField);

	free(*pGroup);
	*pGroup = NULL;
}


static void GMDFreeChallengeGroup(GMDChallengeGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pRoomField);
	MEFieldSafeDestroy(&pGroup->pChallengeTypeField);
	MEFieldSafeDestroy(&pGroup->pChallengeSpecField);
	MEFieldSafeDestroy(&pGroup->pChallengeTagsField);
	MEFieldSafeDestroy(&pGroup->pChallengeHeterogenousField);
	MEFieldSafeDestroy(&pGroup->pChallengeNameField);
	MEFieldSafeDestroy(&pGroup->pCountField);
	MEFieldSafeDestroy(&pGroup->pNumSpawnField);
	MEFieldSafeDestroy(&pGroup->pPlacementField);
	MEFieldSafeDestroy(&pGroup->pPlacementPrefabLocationField);
	MEFieldSafeDestroy(&pGroup->pFacingField);
	MEFieldSafeDestroy(&pGroup->pRotationIncrementField);
	MEFieldSafeDestroy(&pGroup->pExcludeDistField);
	MEFieldSafeDestroy(&pGroup->pChallengeRefField);

	MEFieldSafeDestroy(&pGroup->pSpacePatrolTypeField);
	MEFieldSafeDestroy(&pGroup->pSpacePatRoomRefField);
	MEFieldSafeDestroy(&pGroup->pSpacePatChallengeRefField);
	MEFieldSafeDestroy(&pGroup->pPatrolTypeField);
	MEFieldSafeDestroy(&pGroup->pPatOtherRoomField);
	MEFieldSafeDestroy(&pGroup->pPatPlacementField);
	MEFieldSafeDestroy(&pGroup->pPatChallengeRefField);

	MEFieldSafeDestroy(&pGroup->pClickieInteractionDefField);
	MEFieldSafeDestroy(&pGroup->pClickieInteractTextField);
	MEFieldSafeDestroy(&pGroup->pClickieSuccessTextField);
	MEFieldSafeDestroy(&pGroup->pClickieFailureTextField);
	MEFieldSafeDestroy(&pGroup->pClickieInteractAnimField);

	GMDFreeWhenGroup(&pGroup->pWhenGroup);

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pChallengeExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	eaDestroy( &pGroup->eaPrefabLocations );
	free(pGroup);
}


static void GMDFreePromptActionGroup(GMDPromptActionGroup *pGroup)
{
	ui_WidgetQueueFreeAndNull(&pGroup->pGroupLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pTextLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pNextLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActionsLabel);
	ui_WidgetQueueFreeAndNull(&pGroup->pActionButton);
	ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);

	MEFieldSafeDestroy(&pGroup->pTextField);
	MEFieldSafeDestroy(&pGroup->pGrantMissionField);
	MEFieldSafeDestroy(&pGroup->pDismissActionField);
	MEFieldSafeDestroy(&pGroup->pNextField);
	
	free(pGroup);
}


static void GMDFreePromptGroup(GMDPromptGroup *pGroup)
{
	int i;

	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pDialogFlagsField);
	MEFieldSafeDestroy(&pGroup->pCostumeTypeField);
	MEFieldSafeDestroy(&pGroup->pCostumeSpecifiedField);
	MEFieldSafeDestroy(&pGroup->pCostumePetField);
	MEFieldSafeDestroy(&pGroup->pCostumeCritterGroupTypeField);
	MEFieldSafeDestroy(&pGroup->pCostumeCritterGroupSpecifiedField);
	MEFieldSafeDestroy(&pGroup->pCostumeCritterGroupMapVarField);
	MEFieldSafeDestroy(&pGroup->pCostumeCritterGroupIDField);
	MEFieldSafeDestroy(&pGroup->pHeadshotStyleField);
	MEFieldSafeDestroy(&pGroup->pTitleTextField);
	eaDestroyEx(&pGroup->eaBodyTextField, MEFieldDestroy);
	eaDestroyEx(&pGroup->eaBodyTextAddRemoveButtons, ui_WidgetQueueFree);
	MEFieldSafeDestroy(&pGroup->pPhraseField);
	MEFieldSafeDestroy(&pGroup->pOptionalField);
	MEFieldSafeDestroy(&pGroup->pOptionalButtonTextField);
	MEFieldSafeDestroy(&pGroup->pOptionalCategoryField);
	MEFieldSafeDestroy(&pGroup->pOptionalPriorityField);
	MEFieldSafeDestroy(&pGroup->pOptionalHideOnCompleteField);
	MEFieldSafeDestroy(&pGroup->pOptionalHideOnCompletePromptField);
	MEFieldSafeDestroy(&pGroup->pOptionalAutoExecuteField);

	GMDFreeWhenGroup(&pGroup->pShowWhenGroup);
	
	// Clean up sub-groups
	for(i=eaSize(&pGroup->eaPromptActions)-1; i>=0; --i) {
		GMDFreePromptActionGroup(pGroup->eaPromptActions[i]);
	}
	eaDestroy(&pGroup->eaPromptActions);

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pChallengeExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreePortalGroup(GMDPortalGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pTypeField);
	MEFieldSafeDestroy(&pGroup->pUseTypeField);
	MEFieldSafeDestroy(&pGroup->pStartRoomField);
	MEFieldSafeDestroy(&pGroup->pStartDoorField);
	MEFieldSafeDestroy(&pGroup->pWarpToStartTextField);
	MEFieldSafeDestroy(&pGroup->pEndZmapField);
	MEFieldSafeDestroy(&pGroup->pEndRoomField);
	MEFieldSafeDestroy(&pGroup->pEndDoorField);
	MEFieldSafeDestroy(&pGroup->pWarpToEndTextField);
	GMDFreeWhenGroup(&pGroup->pWhenGroup);
	eaDestroyEx( &pGroup->eaEndVariablesGroup, GEFreeVariableDefGroup );

	ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pChallengeExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreeMissionInfoGroup(MapDescEditDoc *pDoc, GMDMissionInfoGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pDisplayNameField);
	MEFieldSafeDestroy(&pGroup->pShortTextField);
	MEFieldSafeDestroy(&pGroup->pDescriptionTextField);
	MEFieldSafeDestroy(&pGroup->pSummaryTextField);
	MEFieldSafeDestroy(&pGroup->pCategoryField);
	MEFieldSafeDestroy(&pGroup->pShareableField);
	MEFieldSafeDestroy(&pGroup->pRewardField);
	MEFieldSafeDestroy(&pGroup->pRewardScaleField);
	MEFieldSafeDestroy(&pGroup->pGenerationTypeField);
	MEFieldSafeDestroy(&pGroup->pOpenMissionNameField);
	MEFieldSafeDestroy(&pGroup->pOpenMissionShortTextField);
	MEFieldSafeDestroy(&pGroup->pDropRewardTableField);
	MEFieldSafeDestroy(&pGroup->pDropChallengeNamesField);
	GEFreeMissionLevelDefGroupSafe(&pGroup->pLevelDefGroup);

	ui_ExpanderGroupRemoveExpander(pDoc->pObjectiveExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreeMissionStartGroup(MapDescEditDoc *pDoc, GMDMissionStartGroup *pGroup)
{
	MEFieldSafeDestroy(&pGroup->pGrantField);
	MEFieldSafeDestroy(&pGroup->pGrantContactOfferField);
	MEFieldSafeDestroy(&pGroup->pGrantContactInProgressField);
	MEFieldSafeDestroy(&pGroup->pTurnInField);
	MEFieldSafeDestroy(&pGroup->pTurnInContactCompletedField);
	MEFieldSafeDestroy(&pGroup->pTurnInContactMissionReturnField);
	MEFieldSafeDestroy(&pGroup->pFailTimeoutSecondsField);
	MEFieldSafeDestroy(&pGroup->pCanRepeatField);
	MEFieldSafeDestroy(&pGroup->pRepeatCooldownHoursField);
	MEFieldSafeDestroy(&pGroup->pRepeatCooldownHoursFromStartField);
	MEFieldSafeDestroy(&pGroup->pRepeatRepeatCooldownCountField);
	MEFieldSafeDestroy(&pGroup->pRepeatCooldownBlockTimeField);
	MEFieldSafeDestroy(&pGroup->pRequiresMissionsField);

	ui_ExpanderGroupRemoveExpander(pDoc->pObjectiveExpanderGroup, pGroup->pExpander);
	ui_WidgetQueueFreeAndNull(&pGroup->pExpander);

	free(pGroup);
}


static void GMDFreeObjectiveGroup(GMDObjectiveGroup *pGroup)
{
	int i;

	MEFieldSafeDestroy(&pGroup->pNameField);
	MEFieldSafeDestroy(&pGroup->pOptionalField);
	MEFieldSafeDestroy(&pGroup->pShortTextField);
	MEFieldSafeDestroy(&pGroup->pLongTextField);
	MEFieldSafeDestroy(&pGroup->pTimeoutField);
	MEFieldSafeDestroy(&pGroup->pShowWaypointsField);

	GMDFreeWhenGroup(&pGroup->pWhenGroup);

	// Clean up child groups
	for(i=eaSize(&pGroup->eaSubGroups)-1; i>=0; --i) {
		GMDFreeObjectiveGroup(pGroup->eaSubGroups[i]);
	}
	eaDestroy(&pGroup->eaSubGroups);

	if (pGroup->iIndent == 0) {
		ui_ExpanderGroupRemoveExpander(pGroup->pDoc->pObjectiveExpanderGroup, pGroup->pExpander);
		ui_WidgetQueueFreeAndNull(&pGroup->pExpander);
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pTitleLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pShortTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLongTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pTimeoutLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRemoveButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pUpButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pDownButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pAddChildButton);
	}

	free(pGroup);
}

static void GMDRefreshTagLists(void)
{
	ResourceSearchRequest request = {0};
	ResourceSearchResult *pResult;
	int i;

	eaClear(&gGeoTypeTags);
	eaClear(&gEcosystemTags);
	eaClear(&gInteriorKitTags);
	eaClear(&gRoomTags);
	eaClear(&gPathTags);
	eaClear(&gBackdropTags);
	eaClear(&gDetailTags);
	eaClear(&gObjectLibTags);
	eaClear(&gSolarSystemEnvTags);
	eaClear(&gChallengeTags);
	eaClear(&gChallengeNames);
	eaClear(&gPhraseNames);

	tagGetCombinationsForDictionary(GENESIS_GEOTYPE_DICTIONARY, &gGeoTypeTags);
	tagGetCombinationsForDictionary(GENESIS_ECOTYPE_DICTIONARY, &gEcosystemTags);
	tagGetCombinationsForDictionary(GENESIS_INTERIORS_DICTIONARY, &gInteriorKitTags);
	tagGetCombinationsForDictionary(GENESIS_ROOM_DEF_DICTIONARY, &gRoomTags);
	tagGetCombinationsForDictionary(GENESIS_PATH_DEF_DICTIONARY, &gPathTags);
	tagGetCombinationsForDictionary(GENESIS_BACKDROP_FILE_DICTIONARY, &gBackdropTags);
	tagGetCombinationsForDictionary(OBJECT_LIBRARY_DICT, &gObjectLibTags);
	tagGetCombinationsForDictionary(GENESIS_DETAIL_DICTIONARY, &gDetailTags);

	// Pull out object library information
	request.eSearchMode = SEARCH_MODE_TAG_SEARCH;
	request.pcSearchDetails = "genesischallenge";
	request.pcType = OBJECT_LIBRARY_DICT;
	pResult = handleResourceSearchRequest(&request);
	for(i=eaSize(&pResult->eaRows)-1; i>=0; --i) {
		GroupDef *group;
		char *pcName = pResult->eaRows[i]->pcName;

		group = objectLibraryGetGroupDefByName(pcName, false);
		if (group) {
			eaPush(&gChallengeNames, group->name_str);
			tagAddCombinationsFromString(&gChallengeTags, group->tags);
		}
	}
	
	// Add in complement tags to the peaTags list 
	{
		const char** complementTags = NULL;
		for( i = 0; i != eaSize(&gChallengeTags); ++i ) {
			char buffer[256];
			sprintf(buffer, "!%s", gChallengeTags[i]);
			eaPush(&complementTags, allocAddString(buffer));
		}

		eaPushEArray(&gChallengeTags, (char***)&complementTags);
		eaDestroy(&complementTags);
	}

	// Add in the solar system env tags - which is an intersection of
	// Backdrops and ObjLib tags
	for( i = 0; i != eaSize( &gBackdropTags ); ++i ) {
		if( eaFindString( &gObjectLibTags, gBackdropTags[ i ]) >= 0 ) {
			eaPush( &gSolarSystemEnvTags, gBackdropTags[ i ]);
		}
	}

	// Add in phrase names
	DefineFillAllKeysAndValues( ContactAudioPhrasesEnum, &gPhraseNames, NULL );

	resGetUniqueScopes(g_MapDescDictionary, &geaScopes);
	GERefreshMapNamesList();
}


static void GMDIndexChangedCB(void *unused)
{
	gIndexChanged = false;
	GMDRefreshTagLists();
}


static void GMDContentDictChanged(enumResourceEventType eType, const char *pDictName, const char *pcName, Referent pReferent, void *pUserData)
{
	if ((eType == RESEVENT_INDEX_MODIFIED) && !gIndexChanged) {
		gIndexChanged = true;
		emQueueFunctionCall(GMDIndexChangedCB, NULL);
	}
}

static void GMDCheckNameList(void ***peaCurrentList, void ***peaNewList, ParseTable* pti)
{
	bool bChanged = false;
	int i;

	if (eaSize(peaCurrentList) == eaSize(peaNewList)) {
		for(i=eaSize(peaCurrentList)-1; i>=0; --i) {
			if (!pti) {
				char*** strCurrentList = (char***)peaCurrentList;
				char*** strNewList = (char***)peaNewList;
				if (stricmp((*strCurrentList)[i], (*strNewList)[i]) != 0) {
					bChanged = true;
					break;
				}
			} else {
				if (StructCompare(pti, (*peaCurrentList)[i], (*peaNewList)[i], 0, 0, 0) != 0) {
					bChanged = true;
					break;
				}
			}
		}
	} else {
		bChanged = true;
	}
	if (bChanged) {
		if (!pti) {
			eaDestroyEx(peaCurrentList, NULL);
		} else {
			eaDestroyStructVoid(peaCurrentList, pti);
		}
		*peaCurrentList = *peaNewList;
	} else {
		if (!pti) {
			eaDestroyEx(peaNewList, NULL);
		} else {
			eaDestroyStructVoid(peaNewList, pti);
		}
	}
}

static void GMDAddObjectiveName(char*** peaObjectiveNames, GenesisMissionObjective* objective)
{
	int i;
	
	eaPush(peaObjectiveNames, strdup(objective->pcName));
	for(i = 0; i != eaSize(&objective->eaChildren); ++i)
	{
		GMDAddObjectiveName(peaObjectiveNames, objective->eaChildren[i]);
	}
}

static void GMDRefreshData(MapDescEditDoc *pDoc)
{
	char **eaTempRoomNames = NULL;
	GMDRoomLayoutPair **eaTempAllRooms = NULL;
	char **eaTempRoomAndOrbitNames = NULL;
	char **eaTempRoomAndPathAndOrbitNames = NULL;
	char **eaTempChallengeLocNames = NULL;
	char **eaTempChallengeNames = NULL;
	char **eaTempPromptNames = NULL;
	char **eaTempObjectiveNames = NULL;
	char **eaTempLayoutNames = NULL;
	char **eaTempMissionNames = NULL;
	char **eaTempShoeboxNames = NULL;
	int i,j,k;

	// Build active room and path lists
	if ((pDoc->EditingMapType == GenesisMapType_Exterior) && pDoc->pMapDesc->exterior_layout) {
		for(i=eaSize(&pDoc->pMapDesc->exterior_layout->rooms)-1; i>=0; --i) {
			eaPush(&eaTempRoomNames, strdup(pDoc->pMapDesc->exterior_layout->rooms[i]->name));
			eaPush(&eaTempRoomAndOrbitNames, strdup(pDoc->pMapDesc->exterior_layout->rooms[i]->name));
			eaPush(&eaTempRoomAndPathAndOrbitNames, strdup(pDoc->pMapDesc->exterior_layout->rooms[i]->name));
			eaPush(&eaTempChallengeLocNames, strdup(pDoc->pMapDesc->exterior_layout->rooms[i]->name));
		}
		for(i=eaSize(&pDoc->pMapDesc->exterior_layout->paths)-1; i>=0; --i) {
			eaPush(&eaTempRoomAndPathAndOrbitNames, strdup(pDoc->pMapDesc->exterior_layout->paths[i]->name));
			eaPush(&eaTempChallengeLocNames, strdup(pDoc->pMapDesc->exterior_layout->paths[i]->name));
		}
		eaPush(&eaTempChallengeLocNames, strdup(GENESIS_SIDE_TRAIL_NAME));
	} else if ((pDoc->EditingMapType == GenesisMapType_Interior) && pDoc->pEditingInterior) {
		for(i=eaSize(&pDoc->pEditingInterior->rooms)-1; i>=0; --i) {
			eaPush(&eaTempRoomNames, strdup(pDoc->pEditingInterior->rooms[i]->name));
			eaPush(&eaTempRoomAndOrbitNames, strdup(pDoc->pEditingInterior->rooms[i]->name));
			eaPush(&eaTempRoomAndPathAndOrbitNames, strdup(pDoc->pEditingInterior->rooms[i]->name));
			eaPush(&eaTempChallengeLocNames, strdup(pDoc->pEditingInterior->rooms[i]->name));
		}
		for(i=eaSize(&pDoc->pEditingInterior->paths)-1; i>=0; --i) {
			eaPush(&eaTempRoomAndPathAndOrbitNames, strdup(pDoc->pEditingInterior->paths[i]->name));
			eaPush(&eaTempChallengeLocNames, strdup(pDoc->pEditingInterior->paths[i]->name));
		}
	} else if ((pDoc->EditingMapType == GenesisMapType_SolarSystem) && pDoc->pEditingSolSys) {
		GenesisShoeboxLayout *pShoebox = &pDoc->pEditingSolSys->shoebox;
		for(j=eaSize(&pShoebox->point_lists)-1; j>=0; --j) {
			ShoeboxPointList *pList = pShoebox->point_lists[j];
			for(k=eaSize(&pList->points)-1; k>=0; --k) {
				eaPush(&eaTempRoomNames, strdup(pList->points[k]->name));
				eaPush(&eaTempRoomAndOrbitNames, strdup(pList->points[k]->name));
				eaPush(&eaTempRoomAndPathAndOrbitNames, strdup(pList->points[k]->name));
				eaPush(&eaTempChallengeLocNames, strdup(pList->points[k]->name));
			}
			if(pList->list_type == SBLT_Orbit && pList->name && pList->name[0]) {
				eaPush(&eaTempRoomAndOrbitNames, strdup(pList->name));
				eaPush(&eaTempRoomAndPathAndOrbitNames, strdup(pList->name));
			}
		}
	}

	// Build ALL room lists
	if( pDoc->pMapDesc->exterior_layout ) {
		for(i=eaSize(&pDoc->pMapDesc->exterior_layout->rooms)-1; i>=0; --i) {
			GMDRoomLayoutPair* pair = StructCreate( parse_GMDRoomLayoutPair );
			pair->layout = StructAllocString( pDoc->pMapDesc->exterior_layout->name );
			pair->room = StructAllocString( pDoc->pMapDesc->exterior_layout->rooms[i]->name );
			eaPush(&eaTempAllRooms, pair);
		}
	}
	for(i=eaSize(&pDoc->pMapDesc->interior_layouts)-1; i>=0; --i) {
		GenesisInteriorLayout* interior_layout = pDoc->pMapDesc->interior_layouts[i];
		for(j=eaSize(&interior_layout->rooms)-1; j>=0; --j) {
			GMDRoomLayoutPair* pair = StructCreate( parse_GMDRoomLayoutPair );
			pair->layout = StructAllocString( interior_layout->name );
			pair->room = StructAllocString( interior_layout->rooms[j]->name );
			eaPush(&eaTempAllRooms, pair);
		}
	}
	for(i=eaSize(&pDoc->pMapDesc->solar_system_layouts)-1; i>=0; --i) {
		GenesisSolSysLayout* solsys_layout = pDoc->pMapDesc->solar_system_layouts[i];
		for(j=eaSize(&solsys_layout->shoebox.point_lists)-1; j>=0; --j) {
			ShoeboxPointList *pList = solsys_layout->shoebox.point_lists[j];
			for(k=eaSize(&pList->points)-1; k>=0; --k) {
				GMDRoomLayoutPair* pair = StructCreate( parse_GMDRoomLayoutPair );
				pair->layout = StructAllocString( solsys_layout->name );
				pair->room = StructAllocString( pList->points[k]->name );
				eaPush(&eaTempAllRooms, pair);
			}

			if(pList->list_type == SBLT_Orbit && pList->name && pList->name[0]) {
				GMDRoomLayoutPair* pair = StructCreate( parse_GMDRoomLayoutPair );
				pair->layout = StructAllocString( solsys_layout->name );
				pair->room = StructAllocString( pList->name );
				eaPush(&eaTempAllRooms, pair);
			}
		}
	}

	if (pDoc->iCurrentMission < eaSize(&pDoc->pMapDesc->missions)) {
		assert(pDoc->pMapDesc->missions);

		// Build challenge lists
		for(i=eaSize(&pDoc->pMapDesc->missions[pDoc->iCurrentMission]->eaChallenges)-1; i>=0; --i) {
			eaPush(&eaTempChallengeNames, strdup(pDoc->pMapDesc->missions[pDoc->iCurrentMission]->eaChallenges[i]->pcName));
		}

		// Build objective lists
		for(i=eaSize(&pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaObjectives)-1; i>=0; --i) {
			GMDAddObjectiveName(&eaTempObjectiveNames, pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaObjectives[i]);
		}

		// Build prompt lists
		for(i=eaSize(&pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaPrompts)-1; i>=0; --i) {
			eaPush(&eaTempPromptNames, strdup(pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaPrompts[i]->pcName));
		}
		if(genesisMissionReturnIsAutogenerated(&pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc, eaSize(&pDoc->pMapDesc->solar_system_layouts))) {
			eaPush(&eaTempPromptNames, strdup("MissionReturn"));
		}
		if(genesisMissionContinueIsAutogenerated(&pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc, eaSize(&pDoc->pMapDesc->solar_system_layouts), pDoc->pMapDesc->exterior_layout != NULL)) {
			eaPush(&eaTempPromptNames, strdup("MissionContinue"));
		}
	}

	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		for ( i=0; i < eaSize(&pDoc->pMapDesc->solar_system_layouts); i++ )
			eaPush(&eaTempLayoutNames, strdup(pDoc->pMapDesc->solar_system_layouts[i]->name));
		break;
	case GenesisMapType_Exterior:
		eaPush(&eaTempLayoutNames, strdup(pDoc->pMapDesc->exterior_layout->name));
		break;
	case GenesisMapType_Interior:
		for ( i=0; i < eaSize(&pDoc->pMapDesc->interior_layouts); i++ )
			eaPush(&eaTempLayoutNames, strdup(pDoc->pMapDesc->interior_layouts[i]->name));
		break;
	}

	for(i=eaSize(&pDoc->pMapDesc->shared_challenges)-1; i>= 0; --i) {
		eaPush(&eaTempChallengeNames, strdup(pDoc->pMapDesc->shared_challenges[i]->pcName));
	}

	for(i=0; i<eaSize(&pDoc->pMapDesc->missions); ++i) {
		eaPush(&eaTempMissionNames, strdupf( "#%d. %s", i, pDoc->pMapDesc->missions[i]->zoneDesc.pcName));
	}

	for(i=eaSize(&pDoc->pMapDesc->solar_system_layouts)-1; i>=0; --i) {
		eaPush(&eaTempShoeboxNames, strdup(pDoc->pMapDesc->solar_system_layouts[i]->name));
	}

	// Sort the lists
	eaQSort(eaTempRoomNames, GMDStringCompare);
	eaQSort(eaTempRoomAndOrbitNames, GMDStringCompare);
	eaQSort(eaTempRoomAndPathAndOrbitNames, GMDStringCompare);
	eaQSort(eaTempChallengeNames, GMDStringCompare);
	eaQSort(eaTempPromptNames, GMDStringCompare);
	eaQSort(eaTempObjectiveNames, GMDStringCompare);
	eaQSort(eaTempShoeboxNames, GMDStringCompare);
	eaQSort(eaTempAllRooms, GMDRoomLayoutPairCompare);

	// Check if names changed
	GMDCheckNameList(&pDoc->eaRoomNames, &eaTempRoomNames, NULL);
	GMDCheckNameList(&pDoc->eaRoomAndOrbitNames, &eaTempRoomAndOrbitNames, NULL);
	GMDCheckNameList(&pDoc->eaRoomAndPathAndOrbitNames, &eaTempRoomAndPathAndOrbitNames, NULL);
	GMDCheckNameList(&pDoc->eaChallengeLocNames, &eaTempChallengeLocNames, NULL);
	GMDCheckNameList(&pDoc->eaChallengeNames, &eaTempChallengeNames, NULL);
	GMDCheckNameList(&pDoc->eaPromptNames, &eaTempPromptNames, NULL);
	GMDCheckNameList(&pDoc->eaObjectiveNames, &eaTempObjectiveNames, NULL);
	GMDCheckNameList(&pDoc->eaLayoutNames, &eaTempLayoutNames, NULL);
	GMDCheckNameList(&pDoc->eaMissionNames, &eaTempMissionNames, NULL);
	GMDCheckNameList(&pDoc->eaShoeboxNames, &eaTempShoeboxNames, NULL);
	GMDCheckNameList(&pDoc->eaAllRooms, &eaTempAllRooms, parse_GMDRoomLayoutPair);
}

static void GMDEnsureLayout(MapDescEditDoc *pDoc)
{
	if(	eaSize(&pDoc->pMapDesc->solar_system_layouts) == 0 &&
		eaSize(&pDoc->pMapDesc->interior_layouts) == 0 &&
		!pDoc->pMapDesc->exterior_layout)
	{
		GenesisSolSysLayout *layout = StructCreate(parse_GenesisSolSysLayout);
		layout->name = genesisMakeNewLayoutName(pDoc->pMapDesc, pDoc->EditingMapType);
		eaPush(&pDoc->pMapDesc->solar_system_layouts, layout);
	}
}

static void GMDEnsureMission(MapDescEditDoc *pDoc, GenesisMissionDescription **ppMission, GenesisMissionDescription **ppOrigMission)
{
	char buf[128];

	// Make sure a mission is present
	while(eaSize(&pDoc->pMapDesc->missions) <= pDoc->iCurrentMission) {
		GenesisMissionDescription *pNewMission = StructCreate(parse_GenesisMissionDescription);
		sprintf(buf, "Mission_%d", pDoc->iCurrentMission+1);
		pNewMission->zoneDesc.pcName = StructAllocString(buf);
		pNewMission->zoneDesc.levelDef.missionLevel = 1;
		eaPush(&pDoc->pMapDesc->missions, pNewMission);
	}
	assert(pDoc->pMapDesc->missions);

	// Get the correct mission to refresh with
	if (ppMission)
		*ppMission = pDoc->pMapDesc->missions[pDoc->iCurrentMission];

	if (ppOrigMission) {
		if (pDoc->pOrigMapDesc && (eaSize(&pDoc->pOrigMapDesc->missions) > pDoc->iCurrentMission)) {
			assert(pDoc->pOrigMapDesc->missions);
			*ppOrigMission = pDoc->pOrigMapDesc->missions[pDoc->iCurrentMission];
		} else {
			*ppOrigMission = NULL;
		}
	}
}

static void GMDSetCurrentMission(UIComboBox* pCombo, MapDescEditDoc *pDoc)
{
	pDoc->iCurrentMission = ui_ComboBoxGetSelected( pCombo );
	
	// Refresh the UI
	GMDMapDescChanged(pDoc, false);
}

static void GMDAddMission(void* ignored, MapDescEditDoc *pDoc)
{
	pDoc->iCurrentMission = eaSize(&pDoc->pMapDesc->missions);
	GMDEnsureMission(pDoc, NULL, NULL);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDDeleteCurrentMission(void* ignored, MapDescEditDoc *pDoc)
{
	GenesisMissionDescription* currentMission = pDoc->pMapDesc->missions[ pDoc->iCurrentMission ];
	eaRemove(&pDoc->pMapDesc->missions, pDoc->iCurrentMission );
	StructDestroy( parse_GenesisMissionDescription, currentMission );
	
	pDoc->iCurrentMission = MAX( 0, MIN( eaSize( &pDoc->pMapDesc->missions ) - 1, pDoc->iCurrentMission ));
	GMDEnsureMission(pDoc, NULL, NULL);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDAddLayoutByType(MapDescEditDoc *pDoc, GenesisMapType map_type)
{
	switch(map_type)
	{
	case GenesisMapType_SolarSystem:
		eaPush(&pDoc->pMapDesc->solar_system_layouts, StructCreate(parse_GenesisSolSysLayout));
		pDoc->pMapDesc->solar_system_layouts[eaSize(&pDoc->pMapDesc->solar_system_layouts)-1]->name = genesisMakeNewLayoutName(pDoc->pMapDesc, pDoc->EditingMapType);
		break;
	case GenesisMapType_Exterior:
		pDoc->pMapDesc->exterior_layout = StructCreate(parse_GenesisExteriorLayout);
		pDoc->pMapDesc->exterior_layout->name = genesisMakeNewLayoutName(pDoc->pMapDesc, pDoc->EditingMapType);
		break;
	case GenesisMapType_Interior:
		eaPush(&pDoc->pMapDesc->interior_layouts, StructCreate(parse_GenesisInteriorLayout));
		pDoc->pMapDesc->interior_layouts[eaSize(&pDoc->pMapDesc->interior_layouts)-1]->name = genesisMakeNewLayoutName(pDoc->pMapDesc, pDoc->EditingMapType);
		break;
	default:
		assert(false);
		break;
	}
}

static void GMDSetCurrentLayout(UIComboBox* pCombo, MapDescEditDoc *pDoc)
{
	pDoc->iEditingLayoutIdx = ui_ComboBoxGetSelected( pCombo );

	// Refresh the UI
	GMDMapDescChanged(pDoc, false);
}

static void GMDAddLayout(void* ignored, MapDescEditDoc *pDoc)
{
	if(pDoc->EditingMapType == GenesisMapType_Exterior)
	{
		Alertf("Exterior maps only support one layout.");
		return;
	}

	GMDAddLayoutByType(pDoc, pDoc->EditingMapType);

	// Refresh the UI
	GMDMapDescChanged(pDoc, false);
}

static void GMDDeleteCurrentLayout(void* ignored, MapDescEditDoc *pDoc)
{
	int i, j;
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		if(eaSize(&pDoc->pMapDesc->solar_system_layouts) <= 1)
		{
			Alertf("You must have at least one layout.");
			return;
		}
		StructDestroySafe(parse_GenesisSolSysLayout, &pDoc->pEditingSolSys);
		eaRemove(&pDoc->pMapDesc->solar_system_layouts, pDoc->iEditingLayoutIdx);
		break;
	case GenesisMapType_Exterior:
		Alertf("You must have at least one layout.");
		return;		
	case GenesisMapType_Interior:
		if(eaSize(&pDoc->pMapDesc->interior_layouts) <= 1)
		{
			Alertf("You must have at least one layout.");
			return;
		}
		StructDestroySafe(parse_GenesisInteriorLayout, &pDoc->pEditingInterior);
		eaRemove(&pDoc->pMapDesc->interior_layouts, pDoc->iEditingLayoutIdx);
		break;
	default:
		assert(false);
	}
	pDoc->iEditingLayoutIdx = MAX(pDoc->iEditingLayoutIdx-1, 0);

	for ( i=0; i < eaSize(&pDoc->pMapDesc->missions); i++ ) {
		GenesisMissionDescription *pMission = pDoc->pMapDesc->missions[i];
		for ( j=eaSize(&pMission->eaChallenges)-1; j >= 0 ; j-- ) {
			GenesisMissionChallenge *pChallenge = pMission->eaChallenges[j];
			if(stricmp_safe(pChallenge->pcLayoutName, pDoc->pcEditingLayoutName)==0) {
				eaRemove(&pMission->eaChallenges, j);
				StructDestroy(parse_GenesisMissionChallenge, pChallenge);
			}
		}
		for ( j=eaSize(&pMission->zoneDesc.eaPrompts)-1; j >= 0 ; j-- ) {
			GenesisMissionPrompt *pPrompt = pMission->zoneDesc.eaPrompts[j];
			if(stricmp_safe(pPrompt->pcLayoutName, pDoc->pcEditingLayoutName)==0) {
				eaRemove(&pMission->zoneDesc.eaPrompts, j);
				StructDestroy(parse_GenesisMissionPrompt, pPrompt);
			}
		}
		for ( j=eaSize(&pMission->zoneDesc.eaPortals)-1; j >= 0 ; j-- ) {
			GenesisMissionPortal *pPortal = pMission->zoneDesc.eaPortals[j];
			if(pPortal->eType == GenesisMissionPortal_BetweenLayouts)
				continue;
			if(stricmp_safe(pPortal->pcStartLayout, pDoc->pcEditingLayoutName)==0) {
				eaRemove(&pMission->zoneDesc.eaPortals, j);
				StructDestroy(parse_GenesisMissionPortal, pPortal);
			}
		}
	}
	for ( j=eaSize(&pDoc->pMapDesc->shared_challenges)-1; j >= 0 ; j-- ) {
		GenesisMissionChallenge *pChallenge = pDoc->pMapDesc->shared_challenges[j];
		if(stricmp_safe(pChallenge->pcLayoutName, pDoc->pcEditingLayoutName)==0) {
			eaRemove(&pDoc->pMapDesc->shared_challenges, j);
			StructDestroy(parse_GenesisMissionChallenge, pChallenge);
		}
	}

	// Refresh the UI
	GMDMapDescChanged(pDoc, false);	
}
static void GMDActiveLayoutReseed(void* ignored, MapDescEditDoc *pDoc)
{
	// Perform the operation
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		pDoc->pEditingSolSys->common_data.layout_seed = randomU32();
		break;
	case GenesisMapType_Exterior:
		pDoc->pEditingExterior->common_data.layout_seed = randomU32();
		break;
	case GenesisMapType_Interior:
		pDoc->pEditingInterior->common_data.layout_seed = randomU32();
		break;
	default:
		assert(false);
	}

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDActiveLayoutClone(void* ignored, MapDescEditDoc *pDoc)
{
	GenesisSolSysLayout *pSolSysLayout = NULL;
	GenesisInteriorLayout *pInteriorLayout = NULL;
	char *prev_name=NULL;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	if(pDoc->EditingMapType == GenesisMapType_Exterior)
	{
		Alertf("Exterior maps only support one layout.");
		return;
	}

	GMDAddLayoutByType(pDoc, pDoc->EditingMapType);

	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		pSolSysLayout = pDoc->pMapDesc->solar_system_layouts[eaSize(&pDoc->pMapDesc->solar_system_layouts)-1];
		prev_name = pSolSysLayout->name;
		pSolSysLayout->name = NULL;
		StructCopyAll(parse_GenesisSolSysLayout, pDoc->pEditingSolSys, pSolSysLayout);
		pSolSysLayout->name = prev_name;
		break;
	case GenesisMapType_Interior:
		pInteriorLayout = pDoc->pMapDesc->interior_layouts[eaSize(&pDoc->pMapDesc->interior_layouts)-1];
		prev_name = pInteriorLayout->name;
		pInteriorLayout->name = NULL;
		StructCopyAll(parse_GenesisInteriorLayout, pDoc->pEditingInterior, pInteriorLayout);
		pInteriorLayout->name = prev_name;
		break;
	default:
		assert(false);
	}

	// Refresh the UI
	GMDMapDescChanged( pDoc, true);
}

static void GMDActiveLayoutCut(void* ignored, MapDescEditDoc *pDoc)
{
	GenesisMissionDescription* pMission = pDoc->pMapDesc->missions[ pDoc->iCurrentMission ];

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		if(eaSize(&pDoc->pMapDesc->solar_system_layouts) <= 1)
		{
			Alertf("You must have at least one layout.");
			return;
		}
		StructCopyAll( parse_GenesisSolSysLayout, pDoc->pEditingSolSys, &g_GMDClipboardSolSys );
		g_GMDClipboardLayoutType = pDoc->EditingMapType;
		break;
	case GenesisMapType_Exterior:
		Alertf("You must have at least one layout.");
		return;		
	case GenesisMapType_Interior:
		if(eaSize(&pDoc->pMapDesc->interior_layouts) <= 1)
		{
			Alertf("You must have at least one layout.");
			return;
		}
		StructCopyAll( parse_GenesisInteriorLayout, pDoc->pEditingInterior, &g_GMDClipboardInterior );
		g_GMDClipboardLayoutType = pDoc->EditingMapType;
		break;
	default:
		assert(false);
	}

	GMDDeleteCurrentLayout(NULL, pDoc);
}

static void GMDActiveLayoutCopy(void* ignored, MapDescEditDoc *pDoc)
{
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		StructCopyAll( parse_GenesisSolSysLayout, pDoc->pEditingSolSys, &g_GMDClipboardSolSys );
		break;
	case GenesisMapType_Exterior:
		StructCopyAll( parse_GenesisExteriorLayout, pDoc->pEditingExterior, &g_GMDClipboardExterior );
		break;		
	case GenesisMapType_Interior:
		StructCopyAll( parse_GenesisInteriorLayout, pDoc->pEditingInterior, &g_GMDClipboardInterior );
		break;
	default:
		assert(false);
	}
	g_GMDClipboardLayoutType = pDoc->EditingMapType;
}

static void GMDActiveLayoutPaste(void* ignored, MapDescEditDoc *pDoc)
{
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	if(g_GMDClipboardLayoutType != pDoc->EditingMapType) {
		Alertf("Layout type on clipboard is not the same as editing layout type");
		return;
	}
		
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		StructCopyAll( parse_GenesisSolSysLayout, &g_GMDClipboardSolSys, pDoc->pEditingSolSys );
		break;
	case GenesisMapType_Exterior:
		StructCopyAll( parse_GenesisExteriorLayout, &g_GMDClipboardExterior, pDoc->pEditingExterior );
		break;		
	case GenesisMapType_Interior:
		StructCopyAll( parse_GenesisInteriorLayout, &g_GMDClipboardInterior, pDoc->pEditingInterior );
		break;
	default:
		assert(false);
	}

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

//---------------------------------------------------------------------------------------------------
// UI Logic
//---------------------------------------------------------------------------------------------------

static void GMDAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, MapDescEditDoc *pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, GMDFieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, GMDFieldPreChangeCB, pDoc);
}


static UIExpander *GMDCreateExpander(UIExpanderGroup *pExGroup, const char *pcName, int index)
{
	UIExpander *pExpander = ui_ExpanderCreate(pcName, 0);
	ui_WidgetSkin(UI_WIDGET(pExpander), gBoldExpanderSkin);
	ui_ExpanderGroupInsertExpander(pExGroup, pExpander, index);
	ui_ExpanderSetOpened(pExpander, 1);

	return pExpander;
}


// This is called whenever any mapdesc data changes to do cleanup
static void GMDMapDescChanged(MapDescEditDoc *pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		GMDUpdateDisplay(pDoc);

		if (bUndoable && !pDoc->bEmbeddedMode) {
			GMDUndoData *pData = calloc(1, sizeof(GMDUndoData));
			pData->pPreMapDesc = pDoc->pNextUndoMapDesc;
			pData->pPostMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, GMDMapDescUndoCB, GMDMapDescRedoCB, GMDMapDescUndoFreeCB, pData);
			pDoc->pNextUndoMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
		}
	}
}


// This is called by MEField prior to allowing an edit
static bool GMDFieldPreChangeCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc)
{
	// Make sure the resource is checked out of Gimme
	return pDoc->bEmbeddedMode || emDocIsEditable(&pDoc->emDoc, true);
}


// This is called when an MEField is changed
static void GMDFieldChangedCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc)
{
	GMDMapDescChanged(pDoc, bFinished);
}


static void GMDCancelEmbedded(UIButton *pButton, MapDescEditDoc *pDoc)
{
	EditorPrefStoreWindowPosition(MAPDESC_EDITOR, "Window Position", "Main", pDoc->pMainWindow);
	ui_WindowHide(pDoc->pMainWindow);
	if (pDoc->callbackFunc) {
		(*pDoc->callbackFunc)(pDoc, NULL, false, false);
	}
	GMDCloseMapDesc(pDoc);
	gEmbeddedDoc = NULL;
}


static void GMDSaveEmbedded(UIButton *pButton, MapDescEditDoc *pDoc)
{
	EditorPrefStoreWindowPosition(MAPDESC_EDITOR, "Window Position", "Main", pDoc->pMainWindow);

	// Perform the save/reseed action
	GMDMapDescPreSaveFixup(pDoc->pMapDesc);

	if (pDoc->callbackFunc) {
		(*pDoc->callbackFunc)(pDoc, pDoc->pMapDesc, false, false);
	}

	// Make the original now be the current copy
	StructDestroySafe(parse_GenesisMapDescription, &pDoc->pOrigMapDesc);
	pDoc->pOrigMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
	GMDUpdateSelectedLayout(pDoc);

	// Call on to do regular updates
	GMDMapDescChanged(pDoc, false);
}


static void GMDReseedEmbedded(UIButton *pButton, MapDescEditDoc *pDoc)
{
	if (pDoc->callbackFunc) {
		(*pDoc->callbackFunc)(pDoc, pDoc->pMapDesc, true, true);
	}
}

static void GMDReseedDetailEmbedded(UIButton *pButton, MapDescEditDoc *pDoc)
{
	if (pDoc->callbackFunc) {
		(*pDoc->callbackFunc)(pDoc, pDoc->pMapDesc, false, true);
	}
}

static bool GMDWindowCloseCB(UIWindow *pWindow, MapDescEditDoc *pDoc)
{
	GMDCancelEmbedded(NULL, pDoc);

	return true;
}


static void GMDSetScopeCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// Update the filename appropriately
		resFixFilename(g_MapDescDictionary, pDoc->pMapDesc->name, pDoc->pMapDesc);
	}

	// Call on to do regular updates
	GMDFieldChangedCB(pField, bFinished, pDoc);
}

typedef struct GMDUIMakeMapsWindow
{
	UIWindow *win;
	UITextEntry *pMapCountEntry;
	UITextEntry *pMapStartNumEntry;
	UITextEntry *pGenerateDisplayNamesEntry;
	UITextEntry *pNameSpacePrefix;
	int count;
	int start_num;
	GenesisMapDescription *map_desc;
} GMDUIMakeMapsWindow;

static void GMDOpenMakeMapsBrowserCancel(GMDUIMakeMapsWindow *ui)
{
	SAFE_FREE(ui);
}

static const char* GMDGenerateGenesisMapName( StashTable zoneMapDisplayNames, const char* name_list )
{
	NameTemplateListRef displayNameList = { 0 };
	const char* name = NULL;
	SET_HANDLE_FROM_STRING("NameTemplateList", name_list, displayNameList.hNameTemplateList);

	{
		int it;
		for( it = 0; it != 10; ++it ) {
			name = namegen_GenerateName( &displayNameList, NULL, NULL );
			if ((!name) || (!*name)) continue;
			if( !stashFindElement( zoneMapDisplayNames, name, NULL )) {
				break;
			}
		}

		if( it == 10 ) {
			Alertf( "Name Generation failed!\n"
					"\n"
					"Tried to generate a genesis map name 10 times in a row and "
					"it collided with another map name each time.  You need to "
					"add more variety to the GenesisMap name list." );
			name = NULL;
		}
	}

	StructDeInit( parse_NameTemplateListRef, &displayNameList );
	if(name)
		stashAddInt( zoneMapDisplayNames, name, 1, false );

	return name;
}

static void GMDOpenMakeMapsBrowserOk(const char *pcInDir, const char *pcInName, GMDUIMakeMapsWindow *ui)
{
	const char* generateDisplayNameList = EditorPrefGetString(MAPDESC_EDITOR, "MakeMaps", "GenerateDisplayNames", "");
	const char* nameSpacePrefix = EditorPrefGetString(MAPDESC_EDITOR, "MakeMaps", "NameSpacePrefix", "");
	int i;
	EditorPrefStoreString(MAPDESC_EDITOR, "MakeMaps", "StartDir", pcInDir);
	EditorPrefStoreString(MAPDESC_EDITOR, "MakeMaps", "StartText", pcInName);

	if( ui->count > 0 ) {
		char** mapNames = NULL;
		char** mapRoots = NULL;
		StashTable zoneMapDisplayNames = stashTableCreateWithStringKeys( 4, StashDeepCopyKeys );
		bool usingNameSpaces = false;
		char nameSpacePrefixFull[MAX_PATH];

		filelog_printf("GenesisMakeMaps.log", "Making %d Maps:", ui->count);

		if(nameSpacePrefix && nameSpacePrefix[0]) {
			usingNameSpaces = true;
			sprintf(nameSpacePrefixFull, "dyn_%s", nameSpacePrefix);
		}

		// fill out the display names table
		{
			RefDictIterator it;
			ZoneMapInfo* zminfo;
			worldGetZoneMapIterator( &it );
			while( zminfo = worldGetNextZoneMap( &it )) {
				Message* displayName = zmapInfoGetDisplayNameMessagePtr(zminfo);
				if( displayName ) {
					stashAddInt( zoneMapDisplayNames, displayName->pcDefaultString, 1, true );
				}
			}
		}

		// Build a list of directories that are going to be generated
		// in so that they all can be checked out first.
		for( i=0; i < ui->count; i++ )
		{
			char pcNameNoExt[64];
			char pcMapRoot[ MAX_PATH ];
			char pcMapName[ 256 ];

			getFileNameNoExt(pcNameNoExt, pcInName);
			sprintf(pcMapName, "%s_%03d", pcNameNoExt, i + ui->start_num);
			sprintf(pcMapRoot, "%s/%s", pcInDir, pcMapName);

			eaPush(&mapNames, strdup(pcMapName));
			eaPush(&mapRoots, strdup(pcMapRoot));
		}

		if( GMDQueryIfNotGenesisDirs( mapRoots, mapNames )) {
			GimmeErrorValue err;

			err = gimmeDLLDoOperationsDirs( mapRoots, GIMME_CHECKOUT, 0 );
			if( err != GIMME_NO_ERROR && err != GIMME_ERROR_NOT_IN_DB && err != GIMME_ERROR_NO_DLL ) {
				Alertf( "Error checking out all files (see console for details)." );
				gfxStatusPrintf( "Check out FAILED" );
			} else {
				GenesisRuntimeStatus* genStatus = StructCreate( parse_GenesisRuntimeStatus );
				
				for( i=0; i < ui->count; i++ )
				{
					char pcFileName[MAX_PATH];
					char pcMapName[MAX_PATH];
					bool failed=false;
					GenesisRuntimeStatus *map_status;
					U32 seed = rand();
					
					if(usingNameSpaces) {
						char pcNameSpace[MAX_PATH];
						ResourceNameSpace *nameSpaceFile = StructCreate(parse_ResourceNameSpace);
						sprintf( pcNameSpace, "%s_%03d", nameSpacePrefixFull, i + ui->start_num);
						sprintf( pcFileName, NAMESPACE_PATH"%s/%s.namespace", pcNameSpace, pcNameSpace);
						nameSpaceFile->pName = StructAllocString(pcNameSpace);
						ParserWriteTextFile(pcFileName, parse_ResourceNameSpace, nameSpaceFile, 0, 0);

						sprintf( pcFileName, NAMESPACE_PATH"%s/%s/%s.zone", pcNameSpace, mapRoots[i], mapNames[i]);
						sprintf( pcMapName, "%s:%s", pcNameSpace, mapNames[i]);
					} else {
						sprintf( pcFileName, "%s/%s.zone", mapRoots[i], mapNames[i]);
						strcpy( pcMapName, mapNames[i]);
					}
					filelog_printf("GenesisMakeMaps.log", "%s", pcMapName);
					map_status = genesisCreateExternalMap(PARTITION_CLIENT, ui->map_desc, pcFileName, pcMapName,
														  (generateDisplayNameList && generateDisplayNameList[0] ? GMDGenerateGenesisMapName( zoneMapDisplayNames, generateDisplayNameList ) : NULL),
														  seed, seed, false, false);
					eaPushEArray( &genStatus->stages, &map_status->stages );
					eaClear( &map_status->stages );					
					StructDestroy(parse_GenesisRuntimeStatus, map_status);
				}

				wleGenesisDisplayErrorDialog(genStatus);
				StructDestroy( parse_GenesisRuntimeStatus, genStatus );
			}
		}

		stashTableDestroy( zoneMapDisplayNames );
		eaDestroyEx( &mapRoots, NULL );
		eaDestroyEx( &mapNames, NULL );
	}
	GMDOpenMakeMapsBrowserCancel(ui);
}

static void GMDOpenMakeMapsWindowOk(UIButton *button, GMDUIMakeMapsWindow *ui)
{
	ui->count = atoi(ui_TextEntryGetText(ui->pMapCountEntry));
	ui->start_num = atoi(ui_TextEntryGetText(ui->pMapStartNumEntry));
	if(ui->count > 0)
	{
		UIWindow *browser;
		const char *start_dir = EditorPrefGetString(MAPDESC_EDITOR, "MakeMaps", "StartDir", "maps");
		const char *start_text = EditorPrefGetString(MAPDESC_EDITOR, "MakeMaps", "StartText", "");
		EditorPrefStoreInt(MAPDESC_EDITOR, "MakeMaps", "MakeCount", ui->count);
		EditorPrefStoreString(MAPDESC_EDITOR, "MakeMaps", "GenerateDisplayNames", ui_TextEntryGetText(ui->pGenerateDisplayNamesEntry));
		EditorPrefStoreString(MAPDESC_EDITOR, "MakeMaps", "NameSpacePrefix", ui_TextEntryGetText(ui->pNameSpacePrefix));
		browser = ui_FileBrowserCreate("Make Maps", "Save", UIBrowseNew, UIBrowseFiles, true,
									   "maps", start_dir, start_text, "zone", GMDOpenMakeMapsBrowserCancel, ui, GMDOpenMakeMapsBrowserOk, ui);
		if(browser)
		{
			elUICenterWindow(browser);
			ui_WindowShow(browser);
		}
		elUIWindowClose(NULL, ui->win);
	}
}

static bool GMDOpenMakeMapsWindowCancel(UIButton *button, GMDUIMakeMapsWindow *ui)
{
	elUIWindowClose(NULL, ui->win);
	free(ui);
	return true;
}

static void GMDMakeMaps(UIButton *pMakeMapsButton, MapDescEditDoc *pDoc)
{
	GMDUIMakeMapsWindow *ui;
	UIWindow *win;
	UILabel *label;
	UITextEntry *entry;
	int y = 0;
	char buf[25];

	if(!pDoc->emDoc.saved)
	{
		Alertf("You must save before making maps.");
		return;
	}

	ui = calloc(1, sizeof(*ui));

	win = ui_WindowCreate("Make Maps", 100, 100, 300, 90);
	ui->win = win;
	ui->map_desc = pDoc->pMapDesc;
	ui->count = EditorPrefGetInt(MAPDESC_EDITOR, "MakeMaps", "MakeCount", 1);

	label = ui_LabelCreate("Number to Make:", 5, y);
	ui_WindowAddChild(win, label);
	sprintf(buf, "%d", ui->count);
	entry = ui_TextEntryCreate(buf, 125, y);
	ui_TextEntrySetIntegerOnly(entry);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	ui->pMapCountEntry = entry;
	ui_WindowAddChild(win, entry);

	y += STANDARD_ROW_HEIGHT;

	label = ui_LabelCreate("Starting Number:", 5, y);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate("1", 125, y);
	ui_TextEntrySetIntegerOnly(entry);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	ui->pMapStartNumEntry = entry;
	ui_WindowAddChild(win, entry);

	y += STANDARD_ROW_HEIGHT;

	label = ui_LabelCreate( "Display Name List:", 5, y);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreateWithGlobalDictionaryCombo(EditorPrefGetString(MAPDESC_EDITOR, "MakeMaps", "GenerateDisplayNames", ""),
														125, y, "NameTemplateList", "ResourceName",
														true, true, false, true);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	ui_WindowAddChild( win, entry );
	ui->pGenerateDisplayNamesEntry = entry;
	y += STANDARD_ROW_HEIGHT;

	label = ui_LabelCreate( "Name Space Prefix: Dyn_", 5, y);
	ui_WindowAddChild(win, label);
	entry = ui_TextEntryCreate(EditorPrefGetString(MAPDESC_EDITOR, "MakeMaps", "NameSpacePrefix", ""), 155, y);
	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
	ui_WindowAddChild( win, entry );
	ui->pNameSpacePrefix = entry;
	y += STANDARD_ROW_HEIGHT;

	elUIAddCancelOkButtons(win, GMDOpenMakeMapsWindowCancel, ui, GMDOpenMakeMapsWindowOk, ui);
	ui_WindowSetCloseCallback(win, GMDOpenMakeMapsWindowCancel, ui);
	y += STANDARD_ROW_HEIGHT * 2;

	ui_WidgetSetHeight(UI_WIDGET(win), y);
	elUICenterWindow(win);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
}

static void GMDSetNameCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc)
{
	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pMapDesc->name);

	// Make sure the browser picks up the new mapdesc name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pMapDesc->name);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pMapDesc->name);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	GMDSetScopeCB(pField, bFinished, pDoc);
}


static void GMDGameActionChangeCB(UIGameActionEditButton *pButton, GMDPromptActionGroup *pGroup)
{
	GenesisMissionPromptAction *pAction = (*pGroup->peaActions)[pGroup->index];

	if (gameactionblock_Compare(&pAction->actionBlock, pButton->pActionBlock)) {
		// No change, so do nothing
		return;
	}

	StructCopyAll(parse_WorldGameActionBlock, pButton->pActionBlock, &pAction->actionBlock);

	GMDMapDescChanged(pGroup->pGroup->pDoc, true);
}


static void GMDObjectRefChanged(UITextEntry *pEntry, GMDObjectRefGroup *pGroup)
{
	SSLibObj *pObj = (*pGroup->peaObjects)[pGroup->index];

	StructFreeString(pObj->obj.name_str);
	pObj->obj.name_str = StructAllocString(ui_TextEntryGetText(pEntry));

	// Update the map
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDRoomUniquifyName(GenesisLayoutRoom** eaRooms, GenesisLayoutRoom* pRoom)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pRoom->name);
	i = (int)strlen(bufSansNumbers) - 1;
	while( i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pRoom->name );
		}
		
		for(i=eaSize(&eaRooms)-1; i>=0; --i) {
			if (eaRooms[i] != pRoom && stricmp(buf, eaRooms[i]->name) == 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pRoom->name, buf );
}

static void GMDAddRoom(UIButton *pButton, MapDescEditDoc *pDoc)
{
	GenesisLayoutRoom ***peaRooms = NULL;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	if (pDoc->EditingMapType == GenesisMapType_Exterior) {
		peaRooms = &pDoc->pMapDesc->exterior_layout->rooms;
	} else if (pDoc->EditingMapType == GenesisMapType_Interior) {
		peaRooms = &pDoc->pEditingInterior->rooms;
	}

	if (peaRooms) {
		GenesisLayoutRoom *pRoom = StructCreate(parse_GenesisLayoutRoom);

		// Pick a unique name
		pRoom->name = StructAllocString("Room_1");
		GMDRoomUniquifyName(*peaRooms, pRoom);

		// Add the room
		eaPush(peaRooms, pRoom);

		// Refresh the UI
		GMDMapDescChanged(pDoc, true);
	}
}


static void GMDRemoveRoom(void *ignored, GMDRoomGroup *pGroup)
{
	GenesisLayoutRoom *pRoom;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the room
	pRoom = (*pGroup->peaRooms)[pGroup->index];
	StructDestroy(parse_GenesisLayoutRoom, pRoom);
	eaRemove(pGroup->peaRooms, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpRoom(void *ignored, GMDRoomGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the room
	eaSwap(pGroup->peaRooms, pGroup->index, pGroup->index - 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownRoom(void *ignored, GMDRoomGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the room
	eaSwap(pGroup->peaRooms, pGroup->index, pGroup->index + 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDPathUniquifyName(GenesisLayoutPath** eaPaths, GenesisLayoutPath* pPath)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pPath->name);
	i = (int)strlen(bufSansNumbers) - 1;
	while(i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pPath->name );
		}
		
		for(i=eaSize(&eaPaths)-1; i>=0; --i) {
			if (eaPaths[i] != pPath && stricmp(buf, eaPaths[i]->name) == 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pPath->name, buf );
}


static void GMDAddPath(UIButton *pButton, MapDescEditDoc *pDoc)
{
	GenesisLayoutPath ***peaPaths = NULL;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	if (pDoc->EditingMapType == GenesisMapType_Exterior) {
		peaPaths = &pDoc->pMapDesc->exterior_layout->paths;
	} else if (pDoc->EditingMapType == GenesisMapType_Interior) {
		peaPaths = &pDoc->pEditingInterior->paths;
	}

	if (peaPaths) {
		GenesisLayoutPath *pPath = StructCreate(parse_GenesisLayoutPath);

		// Pick a unique name
		pPath->name = StructAllocString("Path_1");
		GMDPathUniquifyName(*peaPaths, pPath);

		// Add the path
		eaPush(peaPaths, pPath);

		// Refresh the UI
		GMDMapDescChanged(pDoc, true);
	}
}


static void GMDRemovePath(UIButton *pButton, GMDPathGroup *pGroup)
{
	GenesisLayoutPath *pPath;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the path
	pPath = (*pGroup->peaPaths)[pGroup->index];
	StructDestroy(parse_GenesisLayoutPath, pPath);
	eaRemove(pGroup->peaPaths, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpPath(UIButton *pButton, GMDPathGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the path
	eaSwap(pGroup->peaPaths, pGroup->index, pGroup->index - 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownPath(UIButton *pButton, GMDPathGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the path
	eaSwap(pGroup->peaPaths, pGroup->index, pGroup->index + 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddDetailObjectRef(UIButton *pButton, GMDLayoutShoeboxGroup *pGroup)
{
	SSLibObj *pObj;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pObj = StructCreate(parse_SSLibObj);
	assert(pObj);

	eaPush(&pGroup->pDoc->pEditingSolSys->shoebox.detail_objects, pObj);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddOrbitObjectRef(UIButton *pButton, GMDPointListGroup *pGroup)
{
	SSLibObj *pObj;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pObj = StructCreate(parse_SSLibObj);
	assert(pObj);

	if (!(*pGroup->peaPointLists)[pGroup->index]->orbit_object) {
		(*pGroup->peaPointLists)[pGroup->index]->orbit_object = StructCreate(parse_SSObjSet);
	}

	eaPush(&(*pGroup->peaPointLists)[pGroup->index]->orbit_object->group_refs, pObj);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddCurveObjectRef(UIButton *pButton, GMDPointListGroup *pGroup)
{
	SSLibObj *pObj;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pObj = StructCreate(parse_SSLibObj);
	assert(pObj);

	eaPush(&(*pGroup->peaPointLists)[pGroup->index]->curve_objects, pObj);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDRemoveObjectRef(UIButton *pButton, GMDObjectRefGroup *pGroup)
{
	SSLibObj *pObj;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the object
	pObj = (*pGroup->peaObjects)[pGroup->index];
	StructDestroy(parse_SSLibObj, pObj);
	eaRemove(pGroup->peaObjects, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddDetailObjectTag(UIButton *pButton, GMDLayoutShoeboxGroup *pGroup)
{
	SSTagObj *pTags;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pTags = StructCreate(parse_SSTagObj);
	assert(pTags);

	eaPush(&pGroup->pDoc->pEditingSolSys->shoebox.detail_objects_tags, pTags);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddOrbitObjectTag(UIButton *pButton, GMDPointListGroup *pGroup)
{
	SSTagObj *pTags;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	if (!(*pGroup->peaPointLists)[pGroup->index]->orbit_object) {
		(*pGroup->peaPointLists)[pGroup->index]->orbit_object = StructCreate(parse_SSObjSet);
	}

	pTags = StructCreate(parse_SSTagObj);
	assert(pTags);

	eaPush(&(*pGroup->peaPointLists)[pGroup->index]->orbit_object->object_tags, pTags);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddCurveObjectTag(UIButton *pButton, GMDPointListGroup *pGroup)
{
	SSTagObj *pTags;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pTags = StructCreate(parse_SSTagObj);
	assert(pTags);

	eaPush(&(*pGroup->peaPointLists)[pGroup->index]->curve_objects_tags, pTags);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDRemoveObjectTag(UIButton *pButton, GMDObjectTagGroup *pGroup)
{
	SSTagObj *pTag;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the object
	pTag = (*pGroup->peaTags)[pGroup->index];
	StructDestroy(parse_SSTagObj, pTag);
	eaRemove(pGroup->peaTags, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDPointUniquifyName(ShoeboxPointList **eaLists, ShoeboxPoint *pPoint)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i, j;

	strcpy(bufSansNumbers, pPoint->name);
	i = (int)strlen(bufSansNumbers) - 1;
	while(i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pPoint->name );
		}
		
		for(i=eaSize(&eaLists)-1; i>=0; --i) {
			ShoeboxPointList *pList = eaLists[i];
			for(j=eaSize(&pList->points)-1; j>=0; --j) {
				if (pList->points[j] != pPoint && stricmp(buf, pList->points[j]->name) == 0) {
					break;
				}
			}
			if (j >= 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pPoint->name, buf );
}

static void GMDAddPoint(UIButton *pButton, GMDPointListGroup *pGroup)
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	ShoeboxPointList ***peaLists = &pDoc->pEditingSolSys->shoebox.point_lists;
	ShoeboxPoint *pPoint;
	int iNum = 1;
	char buf[128];
	int iList = pGroup->index;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	pPoint = StructCreate(parse_ShoeboxPoint);

	// Pick a unique name
	sprintf(buf, "List_%d_Room_1", iList+1);
	pPoint->name = StructAllocString(buf);
	GMDPointUniquifyName(*peaLists, pPoint);

	// Add the point
	eaPush(&(*peaLists)[iList]->points, pPoint);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}


static void GMDRemovePointList(UIButton *pButton, GMDPointListGroup *pGroup)
{
	ShoeboxPointList *pList = (*pGroup->peaPointLists)[pGroup->index];
	
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the list
	StructDestroy(parse_ShoeboxPointList, pList);
	eaRemove(pGroup->peaPointLists, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDRemovePoint(UIButton *pButton, GMDPointGroup *pGroup)
{
	ShoeboxPoint *pPoint = (*pGroup->peaPoints)[pGroup->index];
		
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the point
	StructDestroy(parse_ShoeboxPoint, pPoint);
	eaRemove(pGroup->peaPoints, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpPointList(UIButton *pButton, GMDPointListGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	eaSwap(pGroup->peaPointLists, pGroup->index, pGroup->index - 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpPoint(UIButton *pButton, GMDPointGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the point
	eaSwap(pGroup->peaPoints, pGroup->index, pGroup->index - 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownPointList(UIButton *pButton, GMDPointListGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the list
	eaSwap(pGroup->peaPointLists, pGroup->index, pGroup->index + 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownPoint(UIButton *pButton, GMDPointGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the point
	assert(*pGroup->peaPoints);
	eaSwap(pGroup->peaPoints, pGroup->index, pGroup->index + 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDAddPointList(UIButton *pButton, MapDescEditDoc *pDoc)
{
	ShoeboxPointList *pPointList;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	pPointList = StructCreate(parse_ShoeboxPointList);

	// Add the point list
	eaPush(&pDoc->pEditingSolSys->shoebox.point_lists, pPointList);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDChallengeUniquifyName(GenesisMissionChallenge** eaChallenges, GenesisMissionChallenge* pChallenge)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pChallenge->pcName);
	i = (int)strlen(bufSansNumbers) - 1;
	while( i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pChallenge->pcName );
		}
		
		for(i=eaSize(&eaChallenges)-1; i>=0; --i) {
			if (eaChallenges[i] != pChallenge && stricmp(buf, eaChallenges[i]->pcName) == 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pChallenge->pcName, buf );
}


static void GMDAddChallenge(UIButton *pButton, MapDescEditDoc *pDoc)
{
	GenesisMissionChallenge ***peaChallenges;
	GenesisMissionChallenge *pChallenge;
	int iNum = 1;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	peaChallenges = &pDoc->pMapDesc->missions[pDoc->iCurrentMission]->eaChallenges;

	pChallenge = StructCreate(parse_GenesisMissionChallenge);
	pChallenge->iCount = 1;
	pChallenge->eType = GenesisChallenge_Encounter2;

	// Pick a unique name
	pChallenge->pcName = StructAllocString("Challenge_1");
	pChallenge->pcLayoutName = StructAllocString(GMDGetActiveLayoutName(pDoc));
	GMDChallengeUniquifyName( *peaChallenges, pChallenge );

	// Add the challenge
	eaPush(peaChallenges, pChallenge);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}


static void GMDRemoveChallenge(UIButton *pButton, GMDChallengeGroup *pGroup)
{
	GenesisMissionChallenge *pChallenge;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the challenge
	pChallenge = (*pGroup->peaChallenges)[pGroup->index];
	StructDestroy(parse_GenesisMissionChallenge, pChallenge);
	eaRemove(pGroup->peaChallenges, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpChallenge(UIButton *pButton, GMDChallengeGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the challenge
	{
		int index = pGroup->index;
		if( GMDGetNextChallenge(*pGroup->peaChallenges, pGroup->pDoc->pcEditingLayoutName, &pGroup->index, true)) {
			eaSwap(pGroup->peaChallenges, pGroup->index, index);
		}
	}
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownChallenge(UIButton *pButton, GMDChallengeGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the challenge
	{
		int index = pGroup->index;
		if( GMDGetNextChallenge(*pGroup->peaChallenges, pGroup->pDoc->pcEditingLayoutName, &pGroup->index, false)) {
			eaSwap(pGroup->peaChallenges, pGroup->index, index);
		}
	}
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDAddPromptAction(UIButton *pButton, GMDPromptGroup *pGroup)
{
	GenesisMissionPromptAction *pAction;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	pAction = StructCreate(parse_GenesisMissionPromptAction);

	// Add the action
	eaPush(&(*pGroup->peaPrompts)[pGroup->index]->sPrimaryBlock.eaActions, pAction);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDRemovePromptAction(UIButton *pButton, GMDPromptActionGroup *pGroup)
{
	GenesisMissionPromptAction *pAction;

	if (!pGroup->pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the action
	pAction = (*pGroup->peaActions)[pGroup->index];
	StructDestroy(parse_GenesisMissionPromptAction, pAction);
	eaRemove(pGroup->peaActions, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pGroup->pDoc, true);
}


static void GMDPromptUniquifyName(GenesisMissionPrompt** eaPrompts, GenesisMissionPrompt* pPrompt)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pPrompt->pcName);
	i = (int)strlen(bufSansNumbers) - 1;
	while( i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pPrompt->pcName );
		}
		
		for(i=eaSize(&eaPrompts)-1; i>=0; --i) {
			if (eaPrompts[i] != pPrompt && stricmp(buf, eaPrompts[i]->pcName) == 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pPrompt->pcName, buf );
}

static void GMDAddPrompt(UIButton *pButton, MapDescEditDoc *pDoc)
{
	GenesisMissionPrompt ***peaPrompts;
	GenesisMissionPrompt *pPrompt;
	int iNum = 1;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	peaPrompts = &pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaPrompts;

	pPrompt = StructCreate(parse_GenesisMissionPrompt);
	pPrompt->pcLayoutName = StructAllocString(GMDGetActiveLayoutName(pDoc));

	// Pick a unique name
	pPrompt->pcName = StructAllocString("Prompt_1");
	GMDPromptUniquifyName(*peaPrompts, pPrompt);

	// Add the prompt
	eaPush(peaPrompts, pPrompt);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}


static void GMDRemovePrompt(UIButton *pButton, GMDPromptGroup *pGroup)
{
	GenesisMissionPrompt *pPrompt;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the prompt
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];
	StructDestroy(parse_GenesisMissionPrompt, pPrompt);
	eaRemove(pGroup->peaPrompts, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpPrompt(UIButton *pButton, GMDPromptGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the prompt
	{
		int index = pGroup->index;
		if( GMDGetNextPrompt(*pGroup->peaPrompts, pGroup->pDoc->pcEditingLayoutName, &pGroup->index, true)) {
			eaSwap(pGroup->peaPrompts, pGroup->index, index);
		}
	}

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownPrompt(UIButton *pButton, GMDPromptGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the prompt
	{
		int index = pGroup->index;
		if( GMDGetNextPrompt(*pGroup->peaPrompts, pGroup->pDoc->pcEditingLayoutName, &pGroup->index, false)) {
			eaSwap(pGroup->peaPrompts, pGroup->index, index);
		}
	}

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDPortalUniquifyName(GenesisMissionPortal** eaPortals, GenesisMissionPortal* pPortal)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pPortal->pcName);
	i = (int)strlen(bufSansNumbers) - 1;
	while( i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pPortal->pcName );
		}
		
		for(i=eaSize(&eaPortals)-1; i>=0; --i) {
			if (eaPortals[i] != pPortal && stricmp(buf, eaPortals[i]->pcName) == 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pPortal->pcName, buf );
}

static void GMDAddPortal(UIButton *pButton, MapDescEditDoc *pDoc)
{
	GenesisMissionPortal ***peaPortals;
	GenesisMissionPortal *pPortal;
	int iNum = 1;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	peaPortals = &pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaPortals;

	pPortal = StructCreate(parse_GenesisMissionPortal);
	pPortal->pcName = StructAllocString("Portal_1");
	pPortal->pcStartLayout = StructAllocString(GMDGetActiveLayoutName(pDoc));
	if(pDoc->EditingMapType == GenesisMapType_Interior)
		pPortal->eUseType = GenesisMissionPortal_Door;
	GMDPortalUniquifyName(*peaPortals, pPortal);

	// Add the Portal
	eaPush(peaPortals, pPortal);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDRemovePortal(UIButton *pButton, GMDPortalGroup *pGroup)
{
	GenesisMissionPortal *pPortal;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the portal
	pPortal = (*pGroup->peaPortals)[pGroup->index];
	StructDestroy(parse_GenesisMissionPortal, pPortal);
	eaRemove(pGroup->peaPortals, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpPortal(UIButton *pButton, GMDPortalGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the portal
	{
		int index = pGroup->index;
		if( GMDGetNextPortal(*pGroup->peaPortals, pGroup->pDoc->pcEditingLayoutName, &pGroup->index, true)) {
			eaSwap(pGroup->peaPortals, pGroup->index, index);
		}
	}

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownPortal(UIButton *pButton, GMDPortalGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the portal
	{
		int index = pGroup->index;
		if( GMDGetNextPortal(*pGroup->peaPortals, pGroup->pDoc->pcEditingLayoutName, &pGroup->index, false)) {
			eaSwap(pGroup->peaPortals, pGroup->index, index);
		}
	}

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static bool GMDIsObjectiveNameUnique(const char *pcName, GenesisMissionObjective ***peaObjectives)
{
	int i;

	for(i=eaSize(peaObjectives)-1; i>=0; --i) {
		if (stricmp(pcName, (*peaObjectives)[i]->pcName) == 0) {
			return false;
		}
		if (eaSize(&(*peaObjectives)[i]->eaChildren) && !GMDIsObjectiveNameUnique(pcName, &(*peaObjectives)[i]->eaChildren)) {
			return false;
		}
	}
	return true;
}

static void GMDObjectiveUniquifyName(GenesisMissionObjective** eaObjectives, GenesisMissionObjective* pObjective)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pObjective->pcName);
	i = (int)strlen(bufSansNumbers) - 1;
	while( i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pObjective->pcName );
		}

		if(GMDIsObjectiveNameUnique(buf, &eaObjectives)) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pObjective->pcName, buf );
}

static void GMDAddObjective(UIButton *pButton, MapDescEditDoc *pDoc)
{
	GenesisMissionObjective ***peaObjectives;
	GenesisMissionObjective *pObjective;
	int iNum = 1;
	char buf[128];

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	peaObjectives = &pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.eaObjectives;

	pObjective = StructCreate(parse_GenesisMissionObjective);
	pObjective->succeedWhen.type = GenesisWhen_ChallengeComplete;

	// Pick a unique name
	while(true) {
		sprintf(buf, "Objective_%d", iNum);
		if (GMDIsObjectiveNameUnique(buf, peaObjectives)) {
			break;
		}
		++iNum;
	}
	pObjective->pcName = StructAllocString(buf);

	// Add the challenge
	eaPush(peaObjectives, pObjective);

	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}


static void GMDAddSubObjective(UIButton *pButton, GMDObjectiveGroup *pGroup)
{
	GenesisMissionObjective ***peaObjectives;
	GenesisMissionObjective *pObjective;
	int iNum = 1;
	char buf[128];

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	peaObjectives = &(*pGroup->peaObjectives)[pGroup->index]->eaChildren;

	pObjective = StructCreate(parse_GenesisMissionObjective);
	pObjective->succeedWhen.type = GenesisWhen_ChallengeComplete;

	// Pick a unique name
	while(true) {
		sprintf(buf, "Objective_%d", iNum);
		if (GMDIsObjectiveNameUnique(buf, &pGroup->pDoc->pMapDesc->missions[pGroup->pDoc->iCurrentMission]->zoneDesc.eaObjectives)) {
			break;
		}
		++iNum;
	}
	pObjective->pcName = StructAllocString(buf);

	// Add the challenge
	eaPush(peaObjectives, pObjective);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDRemoveObjective(UIButton *pButton, GMDObjectiveGroup *pGroup)
{
	GenesisMissionObjective *pObjective;

	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Remove the objective
	pObjective = (*pGroup->peaObjectives)[pGroup->index];
	StructDestroy(parse_GenesisMissionObjective, pObjective);
	eaRemove(pGroup->peaObjectives, pGroup->index);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDUpObjective(void *ignored, GMDObjectiveGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the challenge
	eaSwap(pGroup->peaObjectives, pGroup->index, pGroup->index - 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static void GMDDownObjective(void *ignored, GMDObjectiveGroup *pGroup)
{
	if (!pGroup->pDoc->bEmbeddedMode && !emDocIsEditable(&pGroup->pDoc->emDoc, true)) {
		return;
	}

	// Move the objective
	eaSwap(pGroup->peaObjectives, pGroup->index, pGroup->index + 1);

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDExpanderDrawAdvancedTint( UIExpander* expander, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( expander );
	UIStyleFont *font;
	F32 headerHeight;

	if (!UI_GET_SKIN(expander))
		font = GET_REF(g_ui_State.font);
	else if (!ui_IsActive(UI_WIDGET(expander)))
		font = GET_REF(UI_GET_SKIN(expander)->hNormal);
	else
		font = GET_REF(UI_GET_SKIN(expander)->hNormal);

	headerHeight = (ui_StyleFontLineHeight(font, scale) + UI_STEP_SC / 2);

	if(!expander->widget.childrenInactive)
	{
		display_sprite( white_tex_atlas, x, y+headerHeight, z, w / white_tex_atlas->width, scale / white_tex_atlas->height, 0x000000FF);
		display_sprite( white_tex_atlas, x, y+headerHeight, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0x00000022 );
	}
	ui_ExpanderDraw( expander, UI_PARENT_VALUES );
}

static void GMDPaneDrawError( UIPane* pane, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( pane );
	
	display_sprite( white_tex_atlas, x, y, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0xFF000030 );
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}

static void GMDRefreshErrorPane(UIPane** ppPane, F32 y, F32 height, bool inError, UIExpander* pExpander)
{
	if( !*ppPane ) {
		*ppPane = ui_PaneCreate(0, y, 1.0, height, UIUnitPercentage, UIUnitFixed, 0);
		ui_ExpanderAddChild( pExpander, *ppPane );
	}
	
	ui_WidgetSetPosition( UI_WIDGET(*ppPane), 0, y );
	ui_WidgetSetDimensionsEx( UI_WIDGET(*ppPane), 1, height, UIUnitPercentage,
							  (height <= 1 ? UIUnitPercentage : UIUnitFixed) );
	(*ppPane)->invisible = true;
	(*ppPane)->widget.drawF = (inError ? GMDPaneDrawError : ui_PaneDraw);
}

static UILabel *GMDRefreshLabel(UILabel *pLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
{
	if (!pLabel) {
		pLabel = ui_LabelCreate(pcText, x, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
		ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
		ui_LabelEnableTooltips(pLabel);
		ui_ExpanderAddChild(pExpander, pLabel);
	} else {
		ui_LabelSetText(pLabel, pcText);
		ui_WidgetSetTooltipString(UI_WIDGET(pLabel), pcTooltip);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), x, y, xPercent, 0, UITopLeft);
	}
	return pLabel;
}

static UISeparator* GMDRefreshSeparator(UISeparator *pSeparator, F32 y, UIExpander *pExpander)
{
	if (!pSeparator) {
		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_ExpanderAddChild(pExpander, pSeparator);
	}

	ui_WidgetSetPosition(UI_WIDGET(pSeparator), 0, y);

	return pSeparator;
}

static void GMDRefreshTagsSpecifier(UILabel** ppLabel, MEField** ppField, UIPane** ppPane,
									void* pOrig, void* pNew, ParseTable* pTable, char* pcField,
									char* labelText, char* dictionary, char*** peaTags, char** append_tags,
									F32 x, F32 y, UIExpander* pExpander, MapDescEditDoc* pDoc )
{
	ResourceSearchResult* results = NULL;

	if( !labelText ) {
		labelText = "Tags";
	}

	if( dictionary ) {
		int fieldColumn;
		char*** pEArray = NULL;
	
		if( !ParserFindColumn( pTable, pcField, &fieldColumn )) {
			return;
		}
		pEArray = (char***)TokenStoreGetEArray(pTable, fieldColumn, pNew, NULL);
		results = genesisDictionaryItemsFromTagList( dictionary, *pEArray, append_tags, false );
	}
	

	{
		char textBuffer[ 256 ];
		char tooltipBuffer[ 1024 ];
		if( results ) {
			if( eaSize( &results->eaRows ) <= 0 ) {
				sprintf( textBuffer, "%s *ERROR*", labelText );
				strcpy( tooltipBuffer, "This combination of tags is not valid." );
			} else {
				int i;
				sprintf( textBuffer, "%s (%d)", labelText, eaSize( &results->eaRows ));

				strcpy( tooltipBuffer, results->eaRows[0]->pcName );
				for( i = 1; i < MIN( 20, eaSize( &results->eaRows )); ++i ) {
					strcatf( tooltipBuffer, ", %s", results->eaRows[i]->pcName );
				}
				if( i < eaSize( &results->eaRows )) {
					strcat( tooltipBuffer, ", ..." );
				}
			}
		} else {
			strcpy( textBuffer, labelText );
			strcpy( tooltipBuffer, "Specify tags here." );
		}
		
		*ppLabel = GMDRefreshLabel(*ppLabel, textBuffer, tooltipBuffer, x, 0, y, pExpander );
	}
	(*ppLabel)->widget.priority = 10;

	if( !*ppField ) {
		*ppField = MEFieldCreate(kMEFieldType_TextEntry, pOrig, pNew, pTable, pcField, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
								 NULL, NULL, NULL, false, NULL, peaTags, NULL, NULL, -1, 0, 0, 0, ", ");
		MEExpanderAddFieldToParent(*ppField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
								   GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc);
	} else {
		ui_WidgetSetPosition((*ppField)->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(*ppField, pOrig, pNew);
	}
	(*ppField)->pUIText->cb->bDontSortList = true;
	(*ppField)->pUIText->widget.priority = 10;

	if( results ) {
		GMDRefreshErrorPane( ppPane, y - 2, STANDARD_ROW_HEIGHT, eaSize( &results->eaRows ) <= 0, pExpander );
	} else {
		GMDRefreshErrorPane( ppPane, y - 2, STANDARD_ROW_HEIGHT, false, pExpander );
	}
	StructDestroy( parse_ResourceSearchResult, results );
}

static int GMDRefreshAddRemoveButtons(UIButton*** peaButtons, int numButtons, int y,
									  UIWidget* pParent, UIActivationFunc addFunc, UIActivationFunc removeFunc, void* data)
{
	int i;
	
	for( i = eaSize( peaButtons ) - 1; i >= numButtons; --i ) {
		assert( *peaButtons );
		ui_WidgetQueueFreeAndNull( &(*peaButtons)[ i ]);
	}
	eaSetSize( peaButtons, numButtons );

	for( i = 0; i != numButtons; ++i ) {
		UIButton** pButton = &(*peaButtons)[i];

		if( !*pButton ) {
			*pButton = ui_ButtonCreate( "", 0, 0, NULL, NULL );
			ui_WidgetAddChild( pParent, UI_WIDGET(*pButton) );
		}

		ui_WidgetSetPositionEx( UI_WIDGET(*pButton), 5, y, 0, 0, UITopRight );
		ui_WidgetSetWidth( UI_WIDGET(*pButton), 16 );

		if( i == 0 ) {
			ui_ButtonSetText( *pButton, "+" );
			ui_ButtonSetTooltip( *pButton, "Add another page to the prompt." );
			ui_ButtonSetCallback( *pButton, addFunc, data );
		} else {
			ui_ButtonSetText( *pButton, "X" );
			ui_ButtonSetTooltip( *pButton, "Remove this page from the prompt." );
			ui_ButtonSetCallback( *pButton, removeFunc, data );
		}

		y += STANDARD_ROW_HEIGHT;
	}

	return y;
}

static int GMDRefreshEArrayFieldSimple(
		MEField*** peaField, MEFieldType fieldType, void* pOld, void* pNew,
		int x, int y, F32 w, int pad, UIWidget* pParent, MapDescEditDoc* pDoc,
		ParseTable* pTable, const char* pcField)
{
	int fieldColumn;

	assert( peaField );
	if( ParserFindColumn( pTable, pcField, &fieldColumn )) {
		void*** pArray = TokenStoreGetEArray(pTable, fieldColumn, pNew, NULL);
		int numElem = eaSize( pArray );
		int i;
	
		for( i = eaSize( peaField ) - 1; i >= numElem; --i ) {
			assert( *peaField );
			MEFieldSafeDestroy( &(*peaField)[i] );
		}
		eaSetSize( peaField, numElem );
			
		for( i = 0; i != numElem; ++i ) {
			MEField** ppField = &(*peaField)[i];
					
			if (!*ppField) {
				*ppField = MEFieldCreate(fieldType, pOld, pNew, pTable, pcField, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
										 NULL, NULL, NULL, NULL, false , NULL, NULL, NULL, NULL, i, 0, 0, 0, NULL);
				GMDAddFieldToParent(*ppField, pParent, x, y, 0, w,
									(w <= 1.0 ? UIUnitPercentage : UIUnitFixed),
									pad, pDoc);
			} else {
				ui_WidgetSetPosition((*ppField)->pUIWidget, x, y);
				MEFieldSetAndRefreshFromData(*ppField, pOld, pNew);
			}
			
			y += STANDARD_ROW_HEIGHT;
		}
	}

	return y;
}

static UISMFView *GMDRefreshSMFView(UISMFView *pSMFView, const char *pcText, const char* pcTooltip, F32 x, F32 xPercent, F32 y, UIExpander *pExpander)
{
	if (!pSMFView) {
		pSMFView = ui_SMFViewCreate( 0, 0, 1, 1 );
		ui_WidgetSetWidthEx(UI_WIDGET(pSMFView), 1, UIUnitPercentage);
		ui_ExpanderAddChild(pExpander, pSMFView);
	}

	ui_WidgetSetPositionEx(UI_WIDGET(pSMFView), x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetTooltipString(UI_WIDGET(pSMFView), pcTooltip);
	ui_SMFViewSetText(pSMFView, pcText, NULL);
	return pSMFView;
}

static void GMDRefreshButton(UIExpander *pExpander, F32 x, F32 y, F32 w, const char* pcText, UIButton **ppButton, UIActivationFunc pFunc, void* pData)
{
	if (!*ppButton) {
		*ppButton = ui_ButtonCreate(pcText, X_OFFSET_BASE+x, y, pFunc, pData);
		ui_WidgetSetWidth(UI_WIDGET(*ppButton), w);
		ui_ExpanderAddChild(pExpander, *ppButton);
	} else {
		ui_ButtonSetText(*ppButton, pcText);
		ui_ButtonSetCallback(*ppButton, pFunc, pData);
		ui_WidgetSetWidth(UI_WIDGET(*ppButton), w);
		ui_WidgetSetPosition(UI_WIDGET(*ppButton), X_OFFSET_BASE+x, y);
	}
}

static void GMDRefreshButtonSet(UIExpander *pExpander, F32 x, F32 y, bool bUp, bool bDown, const char *pcText, UIButton **ppRemoveButton, UIActivationFunc pRemoveFunc, UIButton **ppUpButton, UIActivationFunc pUpFunc, UIButton **ppDownButton, UIActivationFunc pDownFunc, void *pGroup)
{
	// Update remove button
	GMDRefreshButton(pExpander, x, y, 100, pcText, ppRemoveButton, pRemoveFunc, pGroup);

	// Update up button
	GMDRefreshButton(pExpander, x+110, y, 60, "Up", ppUpButton, pUpFunc, pGroup);
	ui_SetActive(UI_WIDGET(*ppUpButton), bUp);

	// Update down button
	GMDRefreshButton(pExpander, x+180, y, 60, "Down", ppDownButton, pDownFunc, pGroup);
	ui_SetActive(UI_WIDGET(*ppDownButton), bDown);
}

static int GMDRefreshDetailKitInfo(MapDescEditDoc *pDoc, GMDLayoutDetailKitInfo *pKitInfo, GenesisDetailKitLayout *pOrigKit, GenesisDetailKitLayout *pNewKit, const char *name, int y)
{
	UIExpander *pExpander = pDoc->pLayoutInfoGroup->pExpander;

	// Update detail specifier
	pKitInfo->pLayoutDetailSpecLabel = GMDRefreshLabel(pKitInfo->pLayoutDetailSpecLabel, name, "Specifies how the Detail Kit will be chosen.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pKitInfo->pLayoutDetailSpecField) {
		pKitInfo->pLayoutDetailSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigKit, pNewKit, parse_GenesisDetailKitLayout, "DetailSpecifier", GenesisTagOrNameEnum);
		GMDAddFieldToParent(pKitInfo->pLayoutDetailSpecField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pKitInfo->pLayoutDetailSpecField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pKitInfo->pLayoutDetailSpecField, pOrigKit, pNewKit);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pNewKit->detail_kit_specifier == GenesisTagOrName_RandomByTag) {
		// Update detail tags
		GMDRefreshTagsSpecifier(&pKitInfo->pLayoutDetailTagsLabel, &pKitInfo->pLayoutDetailTagsField, &pKitInfo->pLayoutDetailTagsErrorPane,
								pOrigKit, pNewKit, parse_GenesisDetailKitLayout, "DetailTags2",
								NULL, GENESIS_DETAIL_DICTIONARY, &gDetailTags, NULL,
								X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc);
		y += STANDARD_ROW_HEIGHT;

		pKitInfo->pLayoutVaryPerRoomLabel = GMDRefreshLabel(pKitInfo->pLayoutVaryPerRoomLabel, "Vary Per Room", "The Detail Kit will be randomly chose per room rather than once for the whole map.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
		if (!pKitInfo->pLayoutVaryPerRoomField) {
			pKitInfo->pLayoutVaryPerRoomField = MEFieldCreateSimple(kMEFieldType_Check, pOrigKit, pNewKit, parse_GenesisDetailKitLayout, "VaryPerRoom");
			GMDAddFieldToParent(pKitInfo->pLayoutVaryPerRoomField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pKitInfo->pLayoutVaryPerRoomField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pKitInfo->pLayoutVaryPerRoomField, pOrigKit, pNewKit);
		}

		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailTagsLabel);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutVaryPerRoomLabel);
		MEFieldSafeDestroy(&pKitInfo->pLayoutDetailTagsField);
		MEFieldSafeDestroy(&pKitInfo->pLayoutVaryPerRoomField);
	}

	if (pNewKit->detail_kit_specifier == GenesisTagOrName_SpecificByName) {
		// Update detail name
		pKitInfo->pLayoutDetailNameLabel = GMDRefreshLabel(pKitInfo->pLayoutDetailNameLabel, "Name", "The Detail Kit will be the one specified.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
		if (!pKitInfo->pLayoutDetailNameField) {
			pKitInfo->pLayoutDetailNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigKit, pNewKit, parse_GenesisDetailKitLayout, "DetailKit", GENESIS_DETAIL_DICTIONARY, "ResourceName");
			GMDAddFieldToParent(pKitInfo->pLayoutDetailNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pKitInfo->pLayoutDetailNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pKitInfo->pLayoutDetailNameField, pOrigKit, pNewKit);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pKitInfo->pLayoutDetailNameLabel);
		MEFieldSafeDestroy(&pKitInfo->pLayoutDetailNameField);
	}

	// Update density specifier
	pKitInfo->pLayoutDetailDensityLabel = GMDRefreshLabel(pKitInfo->pLayoutDetailDensityLabel, "Density(%)", "Specifies how full of detail pieces rooms should be.  This is a percentage of the value specified in the detail kit.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
	if (!pKitInfo->pLayoutDetailDensityField) {
		pKitInfo->pLayoutDetailDensityField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigKit, pNewKit, parse_GenesisDetailKitLayout, "DetailDensity");
		GMDAddFieldToParent(pKitInfo->pLayoutDetailDensityField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		pKitInfo->pLayoutDetailDensityField->pUISliderText->pSlider->max = 100;
		pKitInfo->pLayoutDetailDensityField->pUISliderText->pSlider->step = 1;
		MEFieldSetAndRefreshFromData(pKitInfo->pLayoutDetailDensityField, pOrigKit, pNewKit);
	} else {
		ui_WidgetSetPosition(pKitInfo->pLayoutDetailDensityField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pKitInfo->pLayoutDetailDensityField, pOrigKit, pNewKit);
	}

	y += STANDARD_ROW_HEIGHT;

	return y;
}

static void GMDUpdateLayoutNameChanged(char **ppcUpdateName, const char *pcOldName, const char *pcNewName)
{
	if(stricmp_safe(*ppcUpdateName, pcOldName) == 0)
	{
		StructFreeString(*ppcUpdateName);
		*ppcUpdateName = StructAllocString(pcNewName);
	}
}

static void* GMDGetActiveLayout(MapDescEditDoc *pDoc)
{
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_SolarSystem:
		return pDoc->pEditingSolSys;
	case GenesisMapType_Exterior:
		return pDoc->pEditingExterior;
	case GenesisMapType_Interior:
		return pDoc->pEditingInterior;
	}
	assert(false);
	return NULL;
}

static void GMDLayoutNameChangedCB(MEField *pField, bool bFinished, MapDescEditDoc *pDoc)
{
	int i, j;
	const char *pcOldName;
	const char *pcNewName;

	if(!bFinished)
		return;

	pcNewName = GMDGetActiveLayoutName(pDoc);
	pcOldName = pDoc->pcEditingLayoutName;

	if(stricmp_safe(pcNewName, pcOldName) == 0)
	{
		GMDFieldChangedCB(pField, bFinished, pDoc);
		return;
	}
	if(pcNewName == NULL || pcNewName[0] == '\0')
	{
		Alertf("Layout name cannot be blank.");
		GMDSetActiveLayoutName(pDoc, pcOldName);
		GMDFieldChangedCB(pField, bFinished, pDoc);
		return;
	}
	if(genesisLayoutNameExists(pDoc->pMapDesc, pcNewName, GMDGetActiveLayout(pDoc)))
	{
		Alertf("Duplicate name found, layout name must be unique.");
		GMDSetActiveLayoutName(pDoc, pcOldName);
		GMDFieldChangedCB(pField, bFinished, pDoc);
		return;
	}

	for ( i=0; i < eaSize(&pDoc->pMapDesc->missions); i++ )
	{
		GenesisMissionDescription *pMission = pDoc->pMapDesc->missions[i];
		for ( j=0; j < eaSize(&pMission->eaChallenges); j++ )
		{
			GenesisMissionChallenge *pChallenge = pMission->eaChallenges[j];
			GMDUpdateLayoutNameChanged(&pChallenge->pcLayoutName, pcOldName, pcNewName);
		}
		for ( j=0; j < eaSize(&pMission->zoneDesc.eaPortals); j++ )
		{
			GenesisMissionPortal *pPortal = pMission->zoneDesc.eaPortals[j];
			GMDUpdateLayoutNameChanged(&pPortal->pcStartLayout, pcOldName, pcNewName);
			GMDUpdateLayoutNameChanged(&pPortal->pcEndLayout, pcOldName, pcNewName);
		}
		for ( j=0; j < eaSize(&pMission->zoneDesc.eaPrompts); j++ )
		{
			GenesisMissionPrompt *pPrompt = pMission->zoneDesc.eaPrompts[j];
			GMDUpdateLayoutNameChanged(&pPrompt->pcLayoutName, pcOldName, pcNewName);
		}
	}
	for ( j=0; j < eaSize(&pDoc->pMapDesc->shared_challenges); j++ )
	{
		GenesisMissionChallenge *pChallenge = pDoc->pMapDesc->shared_challenges[j];
		GMDUpdateLayoutNameChanged(&pChallenge->pcLayoutName, pcOldName, pcNewName);
	}

	if(pDoc->pcEditingLayoutName)
		StructFreeString(pDoc->pcEditingLayoutName);
	pDoc->pcEditingLayoutName = StructAllocString(pcNewName);

	GMDFieldChangedCB(pField, bFinished, pDoc);
}

static void GMDRefreshLayoutInfo(MapDescEditDoc *pDoc)
{
	UIExpander *pExpander = pDoc->pLayoutInfoGroup->pExpander;
	GMDLayoutInfoGroup *pGroup = pDoc->pLayoutInfoGroup;
	GenesisDetailKitLayout *pOrigKitDetail1;
	GenesisDetailKitLayout *pOrigKitDetail2;
	F32 y = 0;

	// Update name
	pGroup->pLayoutName = GMDRefreshLabel(pGroup->pLayoutName, "Layout Name", "The name of the layout.", X_OFFSET_BASE, 0, y, pExpander);
	switch(pDoc->EditingMapType)
	{
	case GenesisMapType_Exterior:
		if (!pGroup->pLayoutExtNameField) {
			pGroup->pLayoutExtNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigEditingExterior, pDoc->pEditingExterior, parse_GenesisExteriorLayout, "Name");
			GMDAddFieldToParent(pGroup->pLayoutExtNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			MEFieldSetChangeCallback(pGroup->pLayoutExtNameField, GMDLayoutNameChangedCB, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutExtNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutExtNameField, pDoc->pOrigEditingExterior, pDoc->pEditingExterior);
		}
		MEFieldSafeDestroy(&pGroup->pLayoutIntNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutSolarSystemNameField);
		break;
	case GenesisMapType_Interior:
		if (!pGroup->pLayoutIntNameField) {
			pGroup->pLayoutIntNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "Name");
			GMDAddFieldToParent(pGroup->pLayoutIntNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			MEFieldSetChangeCallback(pGroup->pLayoutIntNameField, GMDLayoutNameChangedCB, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutIntNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutIntNameField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
		}
		MEFieldSafeDestroy(&pGroup->pLayoutExtNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutSolarSystemNameField);
		break;
	case GenesisMapType_SolarSystem:
		if (!pGroup->pLayoutSolarSystemNameField) {
			pGroup->pLayoutSolarSystemNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigEditingSolSys, pDoc->pEditingSolSys, parse_GenesisSolSysLayout, "Name");
			GMDAddFieldToParent(pGroup->pLayoutSolarSystemNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			MEFieldSetChangeCallback(pGroup->pLayoutSolarSystemNameField, GMDLayoutNameChangedCB, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutSolarSystemNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutSolarSystemNameField, pDoc->pOrigEditingSolSys, pDoc->pEditingSolSys);
		}
		MEFieldSafeDestroy(&pGroup->pLayoutIntNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutExtNameField);
		break;
	}
	y += STANDARD_ROW_HEIGHT;


	if (pDoc->EditingMapType == GenesisMapType_Exterior ) {

			// Update Layout Edit Type
			pGroup->pLayoutExtLayoutInfoSpecifierLabel = GMDRefreshLabel(pGroup->pLayoutExtLayoutInfoSpecifierLabel, "Layout Info", "Specifies if Layout info is defined my template or customized.", X_OFFSET_BASE, 0, y, pExpander);
			if (!pGroup->pLayoutExtLayoutInfoSpecifierField) {
				pGroup->pLayoutExtLayoutInfoSpecifierField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigEditingExterior, pDoc->pEditingExterior, parse_GenesisExteriorLayout, "LayoutInfoSpecifier", GenesisTemplateOrCustomEnum);
				GMDAddFieldToParent(pGroup->pLayoutExtLayoutInfoSpecifierField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutExtLayoutInfoSpecifierField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutExtLayoutInfoSpecifierField, pDoc->pOrigEditingExterior, pDoc->pEditingExterior);
			}

			y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutExtLayoutInfoSpecifierLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutExtLayoutInfoSpecifierField);
	}

	if (pDoc->EditingMapType == GenesisMapType_Interior) {

		// Update Layout Edit Type
		pGroup->pLayoutIntLayoutInfoSpecifierLabel = GMDRefreshLabel(pGroup->pLayoutIntLayoutInfoSpecifierLabel, "Layout Info", "Specifies if Layout info is defined my template or customized.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutIntLayoutInfoSpecifierField) {
			pGroup->pLayoutIntLayoutInfoSpecifierField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "LayoutInfoSpecifier", GenesisTemplateOrCustomEnum);
			GMDAddFieldToParent(pGroup->pLayoutIntLayoutInfoSpecifierField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutIntLayoutInfoSpecifierField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutIntLayoutInfoSpecifierField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
		}

		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutIntLayoutInfoSpecifierLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutIntLayoutInfoSpecifierField);
	}

	// Update Interior Layout Template
	if (pDoc->EditingMapType == GenesisMapType_Interior &&
		pDoc->pEditingInterior->layout_info_specifier == GenesisTemplateOrCustom_Template) {

		MEExpanderRefreshLabel(&pGroup->pLayoutIntTemplateNameLabel, "Template", "Layout template to be used.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pExpander));
		MEExpanderRefreshGlobalDictionaryField(&pGroup->pLayoutIntTemplateNameField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, 
			parse_GenesisInteriorLayout, "InteriorLayoutInfoTemplate", GENESIS_INT_LAYOUT_TEMP_FILE_DICTIONARY,
			UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutIntTemplateNameLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutIntTemplateNameField);
	}

	// Update Exterior Layout Template
	if (pDoc->EditingMapType == GenesisMapType_Exterior &&
		pDoc->pEditingExterior->layout_info_specifier == GenesisTemplateOrCustom_Template) {

		MEExpanderRefreshLabel(&pGroup->pLayoutExtTemplateNameLabel, "Template", "Layout template to be used.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pExpander));
		MEExpanderRefreshGlobalDictionaryField(&pGroup->pLayoutExtTemplateNameField, pDoc->pOrigEditingExterior, pDoc->pEditingExterior, 
			parse_GenesisExteriorLayout, "ExteriorLayoutInfoTemplate", GENESIS_EXT_LAYOUT_TEMP_FILE_DICTIONARY,
			UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutExtTemplateNameLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutExtTemplateNameField);
	}

	if(	pDoc->EditingMapType == GenesisMapType_SolarSystem ||
		(pDoc->EditingMapType == GenesisMapType_Exterior && pDoc->pEditingExterior->layout_info_specifier == GenesisTemplateOrCustom_Custom) ||
		(pDoc->EditingMapType == GenesisMapType_Interior && pDoc->pEditingInterior->layout_info_specifier == GenesisTemplateOrCustom_Custom))
	{
		GenesisLayoutCommonData *pCommonData = NULL;
		GenesisLayoutCommonData *pOrigCommonData = NULL;

		if(pDoc->EditingMapType == GenesisMapType_SolarSystem) {
			pCommonData = &pDoc->pEditingSolSys->common_data;
			if(pDoc->pOrigEditingSolSys)
				pOrigCommonData = &pDoc->pOrigEditingSolSys->common_data;
		}
		if(pDoc->EditingMapType == GenesisMapType_Interior) {
			pCommonData = &pDoc->pEditingInterior->common_data;
			if(pDoc->pOrigEditingInterior)
				pOrigCommonData = &pDoc->pOrigEditingInterior->common_data;
		}
		if(pDoc->EditingMapType == GenesisMapType_Exterior) {
			pCommonData = &pDoc->pEditingExterior->common_data;
			if(pDoc->pOrigEditingExterior)
				pOrigCommonData = &pDoc->pOrigEditingExterior->common_data;
		}
		assert(pCommonData);

		// All map types have backdrop data
		if(pDoc->EditingMapType != GenesisMapType_SolarSystem)
			y += STANDARD_ROW_HEIGHT/2;

		// Update backdrop spec
		pGroup->pLayoutBackdropSpecLabel = GMDRefreshLabel(pGroup->pLayoutBackdropSpecLabel, "Backdrop", "Determines the way the backdrop is specified.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pLayoutBackdropSpecField) {
			pGroup->pLayoutBackdropSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, (pOrigCommonData ? &pOrigCommonData->backdrop_info : NULL), &pCommonData->backdrop_info, parse_GenesisMapDescBackdrop, "BackdropSpecifier", GenesisTagOrNameEnum);
			GMDAddFieldToParent(pGroup->pLayoutBackdropSpecField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutBackdropSpecField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutBackdropSpecField, (pOrigCommonData ? &pOrigCommonData->backdrop_info : NULL), &pCommonData->backdrop_info);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pCommonData->backdrop_info.backdrop_specifier == GenesisTagOrName_RandomByTag) {
			// Update backdrop
			GMDRefreshTagsSpecifier( &pGroup->pLayoutBackdropTagsLabel, &pGroup->pLayoutBackdropTagsField, &pGroup->pLayoutBackdropTagsErrorPane,
				(pOrigCommonData ? &pOrigCommonData->backdrop_info : NULL), &pCommonData->backdrop_info, parse_GenesisMapDescBackdrop, "BackdropTags2",
				NULL, GENESIS_BACKDROP_FILE_DICTIONARY, &gBackdropTags, SAFE_MEMBER( pDoc->pEditingSolSys, environment_tags ),
				X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc );
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropTagsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropTagsErrorPane);
			MEFieldSafeDestroy(&pGroup->pLayoutBackdropTagsField);
		}

		if (pCommonData->backdrop_info.backdrop_specifier == GenesisTagOrName_SpecificByName) {		
			// Update backdrop
			pGroup->pLayoutBackdropNameLabel = GMDRefreshLabel(pGroup->pLayoutBackdropNameLabel, "Name", "The backdrop will be the chosen name.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pLayoutBackdropNameField) {
				pGroup->pLayoutBackdropNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, (pOrigCommonData ? &pOrigCommonData->backdrop_info : NULL), &pCommonData->backdrop_info, parse_GenesisMapDescBackdrop, "Backdrop", GENESIS_BACKDROP_FILE_DICTIONARY, "ResourceName");
				GMDAddFieldToParent(pGroup->pLayoutBackdropNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutBackdropNameField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutBackdropNameField, (pOrigCommonData ? &pOrigCommonData->backdrop_info : NULL), &pCommonData->backdrop_info);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropNameLabel);
			MEFieldSafeDestroy(&pGroup->pLayoutBackdropNameField);
		}
	} else {
		MEFieldSafeDestroy(&pGroup->pLayoutBackdropSpecField);
		MEFieldSafeDestroy(&pGroup->pLayoutBackdropNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutBackdropTagsField);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropSpecLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBackdropTagsErrorPane);
	}

	if (pDoc->EditingMapType == GenesisMapType_Exterior) {
		ui_WidgetSetTextString(UI_WIDGET(pExpander), "Exterior Layout Info");

		// Create struct as required
		if (!pDoc->pMapDesc->exterior_layout) {
			pDoc->pMapDesc->exterior_layout = StructCreate(parse_GenesisExteriorLayout);
			pDoc->pMapDesc->exterior_layout->play_max[0] = DEFAULT_EXTERIOR_PLAYFIELD_SIZE;
			pDoc->pMapDesc->exterior_layout->play_max[1] = DEFAULT_EXTERIOR_PLAYFIELD_SIZE;
			pDoc->pMapDesc->exterior_layout->play_buffer = DEFAULT_EXTERIOR_PLAYFIELD_BUFFER;
		}
	}

	if (pDoc->EditingMapType == GenesisMapType_Exterior &&
		pDoc->pEditingExterior->layout_info_specifier == GenesisTemplateOrCustom_Custom) {

		// Update ecosystem specifier
		pGroup->pLayoutEcosystemSpecLabel = GMDRefreshLabel(pGroup->pLayoutEcosystemSpecLabel, "Ecosystem", "Specifies how the Ecosystem will be chosen.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutEcosystemSpecField) {
			pGroup->pLayoutEcosystemSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "EcosystemSpecifier", GenesisTagOrNameEnum);
			GMDAddFieldToParent(pGroup->pLayoutEcosystemSpecField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutEcosystemSpecField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutEcosystemSpecField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pDoc->pMapDesc->exterior_layout->info.ecosystem_specifier == GenesisTagOrName_RandomByTag) {
			// Update ecosystem tags
			GMDRefreshTagsSpecifier(&pGroup->pLayoutEcosystemTagsLabel, &pGroup->pLayoutEcosystemTagsField, &pGroup->pLayoutEcosystemTagsErrorPane,
				SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "EcosystemTags2",
				NULL, GENESIS_ECOTYPE_DICTIONARY, &gEcosystemTags, NULL,
				X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemTagsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemTagsErrorPane);
			MEFieldSafeDestroy(&pGroup->pLayoutEcosystemTagsField);
		}

		if (pDoc->pMapDesc->exterior_layout->info.ecosystem_specifier == GenesisTagOrName_SpecificByName) {
			// Update ecosystem name
			pGroup->pLayoutEcosystemNameLabel = GMDRefreshLabel(pGroup->pLayoutEcosystemNameLabel, "Name", "The Ecosystem will be the one specified here.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pLayoutEcosystemNameField) {
				pGroup->pLayoutEcosystemNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "Ecosystem", GENESIS_ECOTYPE_DICTIONARY, "ResourceName");
				GMDAddFieldToParent(pGroup->pLayoutEcosystemNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutEcosystemNameField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutEcosystemNameField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemNameLabel);
			MEFieldSafeDestroy(&pGroup->pLayoutEcosystemNameField);
		}

		// Update geo type specifier
		pGroup->pLayoutGeoTypeSpecLabel = GMDRefreshLabel(pGroup->pLayoutGeoTypeSpecLabel, "Geo Type", "Determines how the GeoType is chosen.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutGeoTypeSpecField) {
			pGroup->pLayoutGeoTypeSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "GeotypeSpecifier", GenesisTagOrNameEnum);
			GMDAddFieldToParent(pGroup->pLayoutGeoTypeSpecField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutGeoTypeSpecField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutGeoTypeSpecField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pDoc->pMapDesc->exterior_layout->info.geotype_specifier == GenesisTagOrName_RandomByTag) {
			// Update geo type tags
			GMDRefreshTagsSpecifier(&pGroup->pLayoutGeoTypeTagsLabel, &pGroup->pLayoutGeoTypeTagsField, &pGroup->pLayoutGeoTypeTagsErrorPane,
				SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "GeotypeTags2",
				NULL, GENESIS_GEOTYPE_DICTIONARY, &gGeoTypeTags, NULL,
				X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeTagsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeTagsErrorPane);
			MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeTagsField);
		}

		if (pDoc->pMapDesc->exterior_layout->info.geotype_specifier == GenesisTagOrName_SpecificByName) {
			// Update geo type name
			pGroup->pLayoutGeoTypeNameLabel = GMDRefreshLabel(pGroup->pLayoutGeoTypeNameLabel, "Name", "The GeoType will be the one specified by name.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pLayoutGeoTypeNameField) {
				pGroup->pLayoutGeoTypeNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "Geotype", GENESIS_GEOTYPE_DICTIONARY, "ResourceName");
				GMDAddFieldToParent(pGroup->pLayoutGeoTypeNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutGeoTypeNameField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutGeoTypeNameField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeNameLabel);
			MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeNameField);
		}

		// Update Color Shift
		pGroup->pLayoutColorShiftLabel = GMDRefreshLabel(pGroup->pLayoutColorShiftLabel, "Color Shift", "The color shift to apply.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutColorShiftField) {
			pGroup->pLayoutColorShiftField = MEFieldCreateSimple(kMEFieldType_SliderText, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "ColorShift");
			GMDAddFieldToParent(pGroup->pLayoutColorShiftField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 130, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutColorShiftField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutColorShiftField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Detail Kits
		if(pDoc->pOrigMapDesc && pDoc->pOrigMapDesc->exterior_layout)
		{
			pOrigKitDetail1 = &pDoc->pOrigMapDesc->exterior_layout->detail_kit_1;
			pOrigKitDetail2 = &pDoc->pOrigMapDesc->exterior_layout->detail_kit_2;
		}
		else
		{
			pOrigKitDetail1 = NULL;
			pOrigKitDetail2 = NULL;
		}
		y = GMDRefreshDetailKitInfo(pDoc, &pGroup->ExtDetailKit1, pOrigKitDetail1, &pDoc->pMapDesc->exterior_layout->detail_kit_1, "Detail Kit 1", y);
		y = GMDRefreshDetailKitInfo(pDoc, &pGroup->ExtDetailKit2, pOrigKitDetail2, &pDoc->pMapDesc->exterior_layout->detail_kit_2, "Detail Kit 2", y);

	} else {
		GMDFreeLayoutKitInfo(&pGroup->ExtDetailKit1, true);
		GMDFreeLayoutKitInfo(&pGroup->ExtDetailKit2, true);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeSpecLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutGeoTypeNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemSpecLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEcosystemNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutColorShiftLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeSpecField);
		MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeTagsField);
		MEFieldSafeDestroy(&pGroup->pLayoutGeoTypeNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutEcosystemSpecField);
		MEFieldSafeDestroy(&pGroup->pLayoutEcosystemTagsField);
		MEFieldSafeDestroy(&pGroup->pLayoutEcosystemNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutColorShiftField);
	}

	if (pDoc->EditingMapType == GenesisMapType_Exterior) {
		
		y += STANDARD_ROW_HEIGHT;

		// Detail sharing
		pGroup->pLayoutExtDetailNoSharingLabel = GMDRefreshLabel( pGroup->pLayoutExtDetailNoSharingLabel, "No Detail Sharing", "The detail placements will not be shared, between missions.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pLayoutExtDetailNoSharingField) {
			pGroup->pLayoutExtDetailNoSharingField = MEFieldCreateSimple(kMEFieldType_Check, (pDoc->pOrigEditingExterior ? &pDoc->pOrigEditingExterior->common_data : NULL), &pDoc->pEditingExterior->common_data, parse_GenesisLayoutCommonData, "NoSharingDetail");
			GMDAddFieldToParent(pGroup->pLayoutExtDetailNoSharingField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutExtDetailNoSharingField, (pDoc->pOrigEditingExterior ? &pDoc->pOrigEditingExterior->common_data : NULL), &pDoc->pEditingExterior->common_data);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutExtDetailNoSharingField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutExtDetailNoSharingField, (pDoc->pOrigEditingExterior ? &pDoc->pOrigEditingExterior->common_data : NULL), &pDoc->pEditingExterior->common_data);
		}

		y += STANDARD_ROW_HEIGHT;

		pGroup->pLayoutSideTrailLengthLabel = GMDRefreshLabel(pGroup->pLayoutSideTrailLengthLabel, "Side Trail:", "The min and max distance of side trail paths.", X_OFFSET_BASE, 0, y, pExpander);
		pGroup->pLayoutSideTrailLengthLabel2 = GMDRefreshLabel(pGroup->pLayoutSideTrailLengthLabel2, "x20ft", NULL, X_OFFSET_CONTROL+150, 0, y, pExpander);

		// Update Min Dist
		pGroup->pLayoutSideTrailMinLabel = GMDRefreshLabel(pGroup->pLayoutSideTrailMinLabel, "Min", NULL, X_OFFSET_CONTROL-30, 0, y, pExpander);
		if (!pGroup->pLayoutSideTrailMinField) {
			pGroup->pLayoutSideTrailMinField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "MinSideTrailLength");
			GMDAddFieldToParent(pGroup->pLayoutSideTrailMinField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutSideTrailMinField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutSideTrailMinField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		// Update Max Length
		pGroup->pLayoutSideTrailMaxLabel = GMDRefreshLabel(pGroup->pLayoutSideTrailMaxLabel, "Max", NULL, X_OFFSET_CONTROL+60, 0, y, pExpander);
		if (!pGroup->pLayoutSideTrailMaxField) {
			pGroup->pLayoutSideTrailMaxField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "MaxSideTrailLength");
			GMDAddFieldToParent(pGroup->pLayoutSideTrailMaxField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutSideTrailMaxField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutSideTrailMaxField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update Play Area Min
		pGroup->pLayoutPlayAreaMinXLabel = GMDRefreshLabel(pGroup->pLayoutPlayAreaMinXLabel, "Start: X", "The x position that the terrain starts.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutPlayAreaMinXField) {
			pGroup->pLayoutPlayAreaMinXField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "PlayAreaMin");
			GMDAddFieldToParent(pGroup->pLayoutPlayAreaMinXField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutPlayAreaMinXField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutPlayAreaMinXField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		pGroup->pLayoutPlayAreaMinZLabel = GMDRefreshLabel(pGroup->pLayoutPlayAreaMinZLabel, "Z", "The z position that the terrain starts.", X_OFFSET_CONTROL+60, 0, y, pExpander);
		if (!pGroup->pLayoutPlayAreaMinZField) {
			pGroup->pLayoutPlayAreaMinZField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "PlayAreaMin");
			pGroup->pLayoutPlayAreaMinZField->arrayIndex = 1;
			GMDAddFieldToParent(pGroup->pLayoutPlayAreaMinZField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutPlayAreaMinZField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutPlayAreaMinZField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update Play Area Max
		pGroup->pLayoutPlayAreaMaxXLabel = GMDRefreshLabel(pGroup->pLayoutPlayAreaMaxXLabel, "End: X", "The x position that the terrain ends.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutPlayAreaMaxXField) {
			pGroup->pLayoutPlayAreaMaxXField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "PlayAreaMax");
			GMDAddFieldToParent(pGroup->pLayoutPlayAreaMaxXField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutPlayAreaMaxXField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutPlayAreaMaxXField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		pGroup->pLayoutPlayAreaMaxZLabel = GMDRefreshLabel(pGroup->pLayoutPlayAreaMaxZLabel, "Z", "The z position that the terrain ends.", X_OFFSET_CONTROL+60, 0, y, pExpander);
		if (!pGroup->pLayoutPlayAreaMaxZField) {
			pGroup->pLayoutPlayAreaMaxZField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "PlayAreaMax");
			pGroup->pLayoutPlayAreaMaxZField->arrayIndex = 1;
			GMDAddFieldToParent(pGroup->pLayoutPlayAreaMaxZField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutPlayAreaMaxZField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutPlayAreaMaxZField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update Buffer Distance
		pGroup->pLayoutBufferLabel = GMDRefreshLabel(pGroup->pLayoutBufferLabel, "Buffer Dist", "The buffer size around the map.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutBufferField) {
			pGroup->pLayoutBufferField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "PlayAreaBuffer");
			GMDAddFieldToParent(pGroup->pLayoutBufferField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutBufferField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutBufferField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update list Vertical
		pGroup->pLayoutExtVertDirLabel = GMDRefreshLabel(pGroup->pLayoutExtVertDirLabel, "Vertical Direction", "Should the map go up hill or down hill.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutExtVertDirField) {
			pGroup->pLayoutExtVertDirField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "VertDir", GenesisVertDirEnum);
			GMDAddFieldToParent(pGroup->pLayoutExtVertDirField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutExtVertDirField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutExtVertDirField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update list Shape
		pGroup->pLayoutExtShapeLabel = GMDRefreshLabel(pGroup->pLayoutExtShapeLabel, "Shape", "The basic shape of the layout.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutExtShapeField) {
			pGroup->pLayoutExtShapeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "Shape", GenesisExteriorShapeEnum);
			GMDAddFieldToParent(pGroup->pLayoutExtShapeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutExtShapeField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutExtShapeField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update Max Road Angle
		pGroup->pLayoutMaxRoadAngleLabel = GMDRefreshLabel(pGroup->pLayoutMaxRoadAngleLabel, "Max Road Angle", "Maximum angle the roads can be at.  0 defaults to what is in the geotype.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutMaxRoadAngleField) {
			pGroup->pLayoutMaxRoadAngleField = MEFieldCreateSimple(kMEFieldType_SliderText, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout, parse_GenesisExteriorLayout, "MaxRoadAngle");
			GMDAddFieldToParent(pGroup->pLayoutMaxRoadAngleField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 130, UIUnitFixed, 0, pDoc);
			pGroup->pLayoutMaxRoadAngleField->pUISliderText->pSlider->max = 89;
			pGroup->pLayoutMaxRoadAngleField->pUISliderText->pSlider->step = 0.01;
			MEFieldSetAndRefreshFromData(pGroup->pLayoutMaxRoadAngleField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutMaxRoadAngleField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutMaxRoadAngleField, SAFE_MEMBER(pDoc->pOrigMapDesc, exterior_layout), pDoc->pMapDesc->exterior_layout);
		}

		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutSideTrailLengthLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutSideTrailLengthLabel2);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutSideTrailMinLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutSideTrailMaxLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutPlayAreaMinXLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutPlayAreaMinZLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutPlayAreaMaxXLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutPlayAreaMaxZLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutBufferLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutExtVertDirLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutExtShapeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutMaxRoadAngleLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutExtDetailNoSharingLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutSideTrailMinField);
		MEFieldSafeDestroy(&pGroup->pLayoutSideTrailMaxField);
		MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMinXField);
		MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMinZField);
		MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMaxXField);
		MEFieldSafeDestroy(&pGroup->pLayoutPlayAreaMaxZField);
		MEFieldSafeDestroy(&pGroup->pLayoutBufferField);
		MEFieldSafeDestroy(&pGroup->pLayoutExtVertDirField);
		MEFieldSafeDestroy(&pGroup->pLayoutExtShapeField);
		MEFieldSafeDestroy(&pGroup->pLayoutMaxRoadAngleField);
		MEFieldSafeDestroy(&pGroup->pLayoutExtDetailNoSharingField);
	}

	if (pDoc->EditingMapType == GenesisMapType_Interior) {
		ui_WidgetSetTextString(UI_WIDGET(pExpander), "Interior Layout Info");
	}

	if (pDoc->EditingMapType == GenesisMapType_Interior && 
		pDoc->pEditingInterior->layout_info_specifier == GenesisTemplateOrCustom_Custom) {

		// Update interior kit specifier
		pGroup->pLayoutRoomKitSpecLabel = GMDRefreshLabel(pGroup->pLayoutRoomKitSpecLabel, "Interior Kit", "Specifies how the Interior Kit will be chosen.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutRoomKitSpecField) {
			pGroup->pLayoutRoomKitSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "InteriorKitSpecifier", GenesisTagOrNameEnum);
			GMDAddFieldToParent(pGroup->pLayoutRoomKitSpecField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutRoomKitSpecField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutRoomKitSpecField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pDoc->pEditingInterior->info.room_kit_specifier == GenesisTagOrName_RandomByTag) {
			// Update interior kit tags
			GMDRefreshTagsSpecifier(&pGroup->pLayoutRoomKitTagsLabel, &pGroup->pLayoutRoomKitTagsField, &pGroup->pLayoutRoomKitTagsErrorPane,
				pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "InteriorKitTags2",
				NULL, GENESIS_INTERIORS_DICTIONARY, &gInteriorKitTags, NULL,
				X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitTagsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitTagsErrorPane);
			MEFieldSafeDestroy(&pGroup->pLayoutRoomKitTagsField);
		}

		if (pDoc->pEditingInterior->info.room_kit_specifier == GenesisTagOrName_SpecificByName) {
			// Update room kit name
			pGroup->pLayoutRoomKitNameLabel = GMDRefreshLabel(pGroup->pLayoutRoomKitNameLabel, "Name", "The Interior Kit will be the one named.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pLayoutRoomKitNameField) {
				pGroup->pLayoutRoomKitNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "InteriorKit", GENESIS_INTERIORS_DICTIONARY, "ResourceName");
				GMDAddFieldToParent(pGroup->pLayoutRoomKitNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutRoomKitNameField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutRoomKitNameField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitNameLabel);
			MEFieldSafeDestroy(&pGroup->pLayoutRoomKitNameField);
		}

		// Update light kit specifier
		pGroup->pLayoutLightKitSpecLabel = GMDRefreshLabel(pGroup->pLayoutLightKitSpecLabel, "Light Kit", "Specifies how the Light Kit will be chosen.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutLightKitSpecField) {
			pGroup->pLayoutLightKitSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "LightKitSpecifier", GenesisTagOrNameEnum);
			GMDAddFieldToParent(pGroup->pLayoutLightKitSpecField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutLightKitSpecField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutLightKitSpecField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pDoc->pEditingInterior->info.light_kit_specifier == GenesisTagOrName_RandomByTag) {
			// Update light kit tags
			GMDRefreshTagsSpecifier(&pGroup->pLayoutLightKitTagsLabel, &pGroup->pLayoutLightKitTagsField, &pGroup->pLayoutLightKitTagsErrorPane,
				pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "LightKitTags2",
				NULL, GENESIS_INTERIORS_DICTIONARY, &gInteriorKitTags, NULL,
				X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc);
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitTagsLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitTagsErrorPane);
			MEFieldSafeDestroy(&pGroup->pLayoutLightKitTagsField);
		}

		if (pDoc->pEditingInterior->info.light_kit_specifier == GenesisTagOrName_SpecificByName) {
			// Update light kit name
			pGroup->pLayoutLightKitNameLabel = GMDRefreshLabel(pGroup->pLayoutLightKitNameLabel, "Name", "The Light Kit will be the one named.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pLayoutLightKitNameField) {
				pGroup->pLayoutLightKitNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "LightKit", GENESIS_INTERIORS_DICTIONARY, "ResourceName");
				GMDAddFieldToParent(pGroup->pLayoutLightKitNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutLightKitNameField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutLightKitNameField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitNameLabel);
			MEFieldSafeDestroy(&pGroup->pLayoutLightKitNameField);
		}

		// Detail Kits
		if(pDoc->pOrigMapDesc && pDoc->pOrigEditingInterior)
		{
			pOrigKitDetail1 = &pDoc->pOrigEditingInterior->detail_kit_1;
			pOrigKitDetail2 = &pDoc->pOrigEditingInterior->detail_kit_2;
		}
		else
		{
			pOrigKitDetail1 = NULL;
			pOrigKitDetail2 = NULL;
		}
		y = GMDRefreshDetailKitInfo(pDoc, &pGroup->IntDetailKit1, pOrigKitDetail1, &pDoc->pEditingInterior->detail_kit_1, "Detail Kit 1", y);
		y = GMDRefreshDetailKitInfo(pDoc, &pGroup->IntDetailKit2, pOrigKitDetail2, &pDoc->pEditingInterior->detail_kit_2, "Detail Kit 2", y);
	} else {
		GMDFreeLayoutKitInfo(&pGroup->IntDetailKit1, true);
		GMDFreeLayoutKitInfo(&pGroup->IntDetailKit2, true);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitSpecLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutRoomKitNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitSpecLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutLightKitNameLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutRoomKitSpecField);
		MEFieldSafeDestroy(&pGroup->pLayoutRoomKitTagsField);
		MEFieldSafeDestroy(&pGroup->pLayoutRoomKitNameField);
		MEFieldSafeDestroy(&pGroup->pLayoutLightKitSpecField);
		MEFieldSafeDestroy(&pGroup->pLayoutLightKitTagsField);
		MEFieldSafeDestroy(&pGroup->pLayoutLightKitNameField);
	}
		
	if (pDoc->EditingMapType == GenesisMapType_Interior) {

		y += STANDARD_ROW_HEIGHT;

		// Detail sharing
		pGroup->pLayoutIntDetailNoSharingLabel = GMDRefreshLabel( pGroup->pLayoutIntDetailNoSharingLabel, "No Detail Sharing", "The detail placements will not be shared, between missions.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pLayoutIntDetailNoSharingField) {
			pGroup->pLayoutIntDetailNoSharingField = MEFieldCreateSimple(kMEFieldType_Check, (pDoc->pOrigEditingInterior ? &pDoc->pOrigEditingInterior->common_data : NULL), &pDoc->pEditingInterior->common_data, parse_GenesisLayoutCommonData, "NoSharingDetail");
			GMDAddFieldToParent(pGroup->pLayoutIntDetailNoSharingField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutIntDetailNoSharingField, (pDoc->pOrigEditingInterior ? &pDoc->pOrigEditingInterior->common_data : NULL), &pDoc->pEditingInterior->common_data);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutIntDetailNoSharingField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutIntDetailNoSharingField, (pDoc->pOrigEditingInterior ? &pDoc->pOrigEditingInterior->common_data : NULL), &pDoc->pEditingInterior->common_data);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update list Vertical
		pGroup->pLayoutIntVertDirLabel = GMDRefreshLabel(pGroup->pLayoutIntVertDirLabel, "Vertical Direction", "Should the map go up hill or down hill.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutIntVertDirField) {
			pGroup->pLayoutIntVertDirField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigEditingInterior, pDoc->pEditingInterior, parse_GenesisInteriorLayout, "VertDir", GenesisVertDirEnum);
			GMDAddFieldToParent(pGroup->pLayoutIntVertDirField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutIntVertDirField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutIntVertDirField, pDoc->pOrigEditingInterior, pDoc->pEditingInterior);
		}

		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutIntDetailNoSharingLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutIntVertDirLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutIntDetailNoSharingField);
		MEFieldSafeDestroy(&pGroup->pLayoutIntVertDirField);
	}

	// Solar System Data
	if(pDoc->EditingMapType == GenesisMapType_SolarSystem)
	{
		ui_WidgetSetTextString(UI_WIDGET(pExpander), "Solar System Layout Info");

		GMDRefreshTagsSpecifier( &pGroup->pLayoutSolarSystemEnvTagsLabel, &pGroup->pLayoutSolarSystemEnvTagsField, &pGroup->pLayoutSolarSystemEnvTagsErrorPane,
								 pDoc->pOrigEditingSolSys, pDoc->pEditingSolSys, parse_GenesisSolSysLayout, "EnvironmentTags2",
								 "Environment Tags", NULL, &gSolarSystemEnvTags, NULL,
								 X_OFFSET_BASE, y, pExpander, pDoc );
		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutSolarSystemEnvTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutSolarSystemEnvTagsErrorPane);
		MEFieldSafeDestroy(&pGroup->pLayoutSolarSystemEnvTagsField);
	}

	if (pDoc->EditingMapType == GenesisMapType_Interior ||
		pDoc->EditingMapType == GenesisMapType_Exterior) {
		GenesisLayoutCommonData *pCommonData = NULL;
		GenesisLayoutCommonData *pOrigCommonData = NULL;

		if(pDoc->EditingMapType == GenesisMapType_Interior) {
			pCommonData = &pDoc->pEditingInterior->common_data;
			if(pDoc->pOrigEditingInterior)
				pOrigCommonData = &pDoc->pOrigEditingInterior->common_data;
		}
		if(pDoc->EditingMapType == GenesisMapType_Exterior) {
			pCommonData = &pDoc->pEditingExterior->common_data;
			if(pDoc->pOrigEditingExterior)
				pOrigCommonData = &pDoc->pOrigEditingExterior->common_data;
		}
		assert(pCommonData);

		// Update Jitter
		pGroup->pLayoutEncJitterTypeLabel = GMDRefreshLabel(pGroup->pLayoutEncJitterTypeLabel, "Actor Jitter", "Specifies jitter will be applied to encounters.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pLayoutEncJitterTypeField) {
			pGroup->pLayoutEncJitterTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, (pOrigCommonData ? &pOrigCommonData->jitter : NULL), &pCommonData->jitter, parse_GenesisEncounterJitter, "JitterType", GenesisEncounterJitterTypeEnum);
			GMDAddFieldToParent(pGroup->pLayoutEncJitterTypeField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pLayoutEncJitterTypeField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pLayoutEncJitterTypeField, (pOrigCommonData ? &pOrigCommonData->jitter : NULL), &pCommonData->jitter);
		}

		y += STANDARD_ROW_HEIGHT;

		if(pCommonData->jitter.jitter_type == GEJT_Custom) {

			// Update Jitter Position
			pGroup->pLayoutEncJitterPosLabel = GMDRefreshLabel(pGroup->pLayoutEncJitterPosLabel, "Jitter Position", "Distance in feet that the encounter can move from its original position.", X_OFFSET_BASE, 0, y, pExpander);
			if (!pGroup->pLayoutEncJitterPosField) {
				pGroup->pLayoutEncJitterPosField = MEFieldCreateSimple(kMEFieldType_TextEntry, (pOrigCommonData ? &pOrigCommonData->jitter : NULL), &pCommonData->jitter, parse_GenesisEncounterJitter, "EncPosJitter");
				GMDAddFieldToParent(pGroup->pLayoutEncJitterPosField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutEncJitterPosField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutEncJitterPosField, (pOrigCommonData ? &pOrigCommonData->jitter : NULL), &pCommonData->jitter);
			}

			y += STANDARD_ROW_HEIGHT;

			// Update Jitter Angle
			pGroup->pLayoutEncJitterRotLabel = GMDRefreshLabel(pGroup->pLayoutEncJitterRotLabel, "Jitter Rotation", "Angle in degrees that the encounter can rotate left or right from original rotation.", X_OFFSET_BASE, 0, y, pExpander);
			if (!pGroup->pLayoutEncJitterRotField) {
				pGroup->pLayoutEncJitterRotField = MEFieldCreateSimple(kMEFieldType_TextEntry, (pOrigCommonData ? &pOrigCommonData->jitter : NULL), &pCommonData->jitter, parse_GenesisEncounterJitter, "EncRotJitter");
				GMDAddFieldToParent(pGroup->pLayoutEncJitterRotField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pLayoutEncJitterRotField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pLayoutEncJitterRotField, (pOrigCommonData ? &pOrigCommonData->jitter : NULL), &pCommonData->jitter);
			}

			y += STANDARD_ROW_HEIGHT;

		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEncJitterPosLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEncJitterRotLabel);
			MEFieldSafeDestroy(&pGroup->pLayoutEncJitterPosField);
			MEFieldSafeDestroy(&pGroup->pLayoutEncJitterRotField);		
		}

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEncJitterTypeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEncJitterPosLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pLayoutEncJitterRotLabel);
		MEFieldSafeDestroy(&pGroup->pLayoutEncJitterTypeField);
		MEFieldSafeDestroy(&pGroup->pLayoutEncJitterPosField);
		MEFieldSafeDestroy(&pGroup->pLayoutEncJitterRotField);		
	}

	// Set the expander height
	ui_ExpanderSetHeight(pExpander, y);
}


static void GMDRefreshShoebox(MapDescEditDoc *pDoc)
{
	int i,j;
	UIExpander *pExpander;
	GMDLayoutShoeboxGroup *pGroup;
	GenesisShoeboxLayout *pShoebox, *pOrigShoebox=NULL;
	F32 y = 0;

	// Clear this expander if not a solar system
	if (pDoc->EditingMapType != GenesisMapType_SolarSystem) {
		if (pDoc->pLayoutShoeboxGroup) {
			GMDFreeShoeboxGroup(pDoc, pDoc->pLayoutShoeboxGroup);
			pDoc->pLayoutShoeboxGroup = NULL;
		}
		return;
	}

	// Create group if required
	if (!pDoc->pLayoutShoeboxGroup) {
		pDoc->pLayoutShoeboxGroup = calloc(1, sizeof(GMDLayoutShoeboxGroup));
		pDoc->pLayoutShoeboxGroup->pExpander = GMDCreateExpander(pDoc->pLayoutExpanderGroup, "Shoebox Info", 1);
		pDoc->pLayoutShoeboxGroup->pDoc = pDoc;
	}
	pGroup = pDoc->pLayoutShoeboxGroup;
	pExpander = pDoc->pLayoutShoeboxGroup->pExpander;
	pShoebox = &pDoc->pEditingSolSys->shoebox;
	pOrigShoebox = (pDoc->pOrigEditingSolSys ? &pDoc->pOrigEditingSolSys->shoebox : NULL);

	// Detail Objects
	pGroup->pDetailObjLabel = GMDRefreshLabel(pGroup->pDetailObjLabel, "Detail Objects", "Objects that are put some distance away from game play", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pAddDetailRefButton) {
		pGroup->pAddDetailRefButton = ui_ButtonCreate("Add Name", X_OFFSET_CONTROL, y, GMDAddDetailObjectRef, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddDetailRefButton), 80);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddDetailRefButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddDetailRefButton), X_OFFSET_CONTROL, y);
	}

	if (!pGroup->pAddDetailTagButton) {
		pGroup->pAddDetailTagButton = ui_ButtonCreate("Add Tags", X_OFFSET_CONTROL+90, y, GMDAddDetailObjectTag, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddDetailTagButton), 80);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddDetailTagButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddDetailTagButton), X_OFFSET_CONTROL+90, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update detail objects
	for(i=0; i<eaSize(&pShoebox->detail_objects); ++i) {
		GMDObjectRefGroup *pObjectGroup;
		SSLibObj *pObj, *pOrigObj = NULL;

		if (i >= eaSize(&pGroup->eaDetailRefGroups)) {
			pObjectGroup = calloc(1, sizeof(GMDObjectRefGroup));
			pObjectGroup->pDoc = pGroup->pDoc;
			pObjectGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaDetailRefGroups, pObjectGroup);
		} else {
			pObjectGroup = pGroup->eaDetailRefGroups[i];
		}

		pObj = pShoebox->detail_objects[i];
		if (pOrigShoebox && (i < eaSize(&pOrigShoebox->detail_objects))) {
			pOrigObj = pOrigShoebox->detail_objects[i];
		}

		y = GMDRefreshObjectRef(pDoc, pObjectGroup, y, i, &pShoebox->detail_objects, pObj, pOrigObj, false, true, true);
	}
	for(j=eaSize(&pGroup->eaDetailRefGroups)-1; j>=i; --j) {
		GMDFreeObjectRefGroup(pGroup->eaDetailRefGroups[j]);
		eaRemove(&pGroup->eaDetailRefGroups, j);
	}

	// Update detail tags
	for(i=0; i<eaSize(&pShoebox->detail_objects_tags); ++i) {
		GMDObjectTagGroup *pObjectGroup;
		SSTagObj *pTag, *pOrigTag = NULL;

		if (i >= eaSize(&pGroup->eaDetailTagGroups)) {
			pObjectGroup = calloc(1, sizeof(GMDObjectTagGroup));
			pObjectGroup->pDoc = pGroup->pDoc;
			pObjectGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaDetailTagGroups, pObjectGroup);
		} else {
			pObjectGroup = pGroup->eaDetailTagGroups[i];
		}

		pTag = pShoebox->detail_objects_tags[i];
		if (pOrigShoebox && (i < eaSize(&pOrigShoebox->detail_objects_tags))) {
			pOrigTag = pOrigShoebox->detail_objects_tags[i];
		}

		y = GMDRefreshObjectTag(pDoc, pObjectGroup, y, i, &pShoebox->detail_objects_tags, pTag, pOrigTag, false, true, true);
	}
	for(j=eaSize(&pGroup->eaDetailTagGroups)-1; j>=i; --j) {
		GMDFreeObjectTagGroup(pGroup->eaDetailTagGroups[j]);
		eaRemove(&pGroup->eaDetailTagGroups, j);
	}

	// Set the expander height
	ui_ExpanderSetHeight(pExpander, y);
}

static int GMDRefreshRoomDetailKitInfo(MapDescEditDoc *pDoc, UIExpander *pExpander, GMDRoomDetailKitInfo *pKitInfo, GenesisRoomDetailKitLayout *pOrigKit, GenesisRoomDetailKitLayout *pNewKit, const char *name, int y)
{
	// Update detail specifier
	pKitInfo->pDetailSpecLabel = GMDRefreshLabel(pKitInfo->pDetailSpecLabel, name, "Determines how the Detail Kit for this room is chosen.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pKitInfo->pDetailSpecField) {
		pKitInfo->pDetailSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigKit, pNewKit, parse_GenesisRoomDetailKitLayout, "DetailSpecifier", GenesisTagNameDefaultEnum);
		GMDAddFieldToParent(pKitInfo->pDetailSpecField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pKitInfo->pDetailSpecField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pKitInfo->pDetailSpecField, pOrigKit, pNewKit);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pNewKit->detail_specifier == GenesisTagNameDefault_RandomByTag) {
		// Update detail tags
		GMDRefreshTagsSpecifier(&pKitInfo->pDetailTagsLabel, &pKitInfo->pDetailTagsField, &pKitInfo->pDetailTagsErrorPane,
								pOrigKit, pNewKit, parse_GenesisRoomDetailKitLayout, "DetailTags2",
								NULL, GENESIS_DETAIL_DICTIONARY, &gDetailTags, NULL,
								X_OFFSET_BASE + X_OFFSET_INDENT, y, pExpander, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pKitInfo->pDetailTagsLabel);
		ui_WidgetQueueFreeAndNull(&pKitInfo->pDetailTagsErrorPane);
		MEFieldSafeDestroy(&pKitInfo->pDetailTagsField);
	}

	if (pNewKit->detail_specifier == GenesisTagNameDefault_SpecificByName) {
		// Update detail name
		pKitInfo->pDetailNameLabel = GMDRefreshLabel(pKitInfo->pDetailNameLabel, "Name", "The Detail Kit for this room will be the one named.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
		if (!pKitInfo->pDetailNameField) {
			pKitInfo->pDetailNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigKit, pNewKit, parse_GenesisRoomDetailKitLayout, "DetailKit", GENESIS_DETAIL_DICTIONARY, "ResourceName");
			GMDAddFieldToParent(pKitInfo->pDetailNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pKitInfo->pDetailNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pKitInfo->pDetailNameField, pOrigKit, pNewKit);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pKitInfo->pDetailNameLabel);
		MEFieldSafeDestroy(&pKitInfo->pDetailNameField);
	}

	// Update custom density specifier
	pKitInfo->pDetailCustomDensityLabel = GMDRefreshLabel(pKitInfo->pDetailCustomDensityLabel, "Custom Density", "Specifies if custom density applies to this room.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
	if (!pKitInfo->pDetailCustomDensityField) {
		pKitInfo->pDetailCustomDensityField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigKit, pNewKit, parse_GenesisRoomDetailKitLayout, "DetailDensityOverride");
		GMDAddFieldToParent(pKitInfo->pDetailCustomDensityField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pKitInfo->pDetailCustomDensityField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pKitInfo->pDetailCustomDensityField, pOrigKit, pNewKit);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pNewKit->detail_density_override) {
		// Update detail density specifier
		pKitInfo->pDetailDensityLabel = GMDRefreshLabel(pKitInfo->pDetailDensityLabel, "Density(%)", "Specifies how full of detail pieces this room should be.  This is a percentage of the value specified in the detail kit.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
		if (!pKitInfo->pDetailDensityField) {
			pKitInfo->pDetailDensityField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigKit, pNewKit, parse_GenesisRoomDetailKitLayout, "DetailDensity");
			GMDAddFieldToParent(pKitInfo->pDetailDensityField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			pKitInfo->pDetailDensityField->pUISliderText->pSlider->max = 100;
			pKitInfo->pDetailDensityField->pUISliderText->pSlider->step = 1;
			MEFieldSetAndRefreshFromData(pKitInfo->pDetailDensityField, pOrigKit, pNewKit);
		} else {
			ui_WidgetSetPosition(pKitInfo->pDetailDensityField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pKitInfo->pDetailDensityField, pOrigKit, pNewKit);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pKitInfo->pDetailDensityLabel);
		MEFieldSafeDestroy(&pKitInfo->pDetailDensityField);
	}

	return y;
}

static void GMDActiveRoomClone( void* ignored1, GMDRoomGroup* pGroup )
{
	GenesisLayoutRoom *pNewRoom;
	GenesisLayoutRoom *pRoom;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pRoom = (*pGroup->peaRooms)[pGroup->index];

	// Perform the operation
	pNewRoom = StructClone( parse_GenesisLayoutRoom, pRoom );
	GMDRoomUniquifyName( *pGroup->peaRooms, pNewRoom );
	eaInsert( pGroup->peaRooms, pNewRoom, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveRoomCut( void* ignored1, GMDRoomGroup* pGroup )
{
	GenesisLayoutRoom *pRoom;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pRoom = (*pGroup->peaRooms)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisLayoutRoom, pRoom, &g_GMDClipboardRoom );
	StructDestroy( parse_GenesisLayoutRoom, pRoom );
	eaRemove( pGroup->peaRooms, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveRoomCopy( void* ignored1, GMDRoomGroup* pGroup )
{
	GenesisLayoutRoom *pRoom;

	if( !pGroup ) {
		return;
	}
	pRoom = (*pGroup->peaRooms)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisLayoutRoom, pRoom, &g_GMDClipboardRoom );
	g_GMDClipboardRoom.detail_seed = 0;
}

static void GMDActiveRoomPaste( void* ignored1, GMDRoomGroup* pGroup )
{
	GenesisLayoutRoom *pRoom;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pRoom = (*pGroup->peaRooms)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_GenesisLayoutRoom, &g_GMDClipboardRoom, pRoom );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveRoomReseed( void* ignored1, GMDRoomGroup* pGroup )
{
	GenesisLayoutRoom *pRoom;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pRoom = (*pGroup->peaRooms)[pGroup->index];

	// Perform the operation
	pRoom->detail_seed = randomU32();

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDExpanderExpanded(UIExpander *pExpander, MapDescEditDoc *pDoc)
{
	GMDMapDescChanged(pDoc, false);
}

static int GMDRefreshRoomAdvancedOptions(MapDescEditDoc *pDoc, GMDRoomGroup *pGroup, UIExpander *pExpander, GenesisLayoutRoom *pOrigRoom, GenesisLayoutRoom *pRoom)
{
	int y = 0;
	GenesisRoomDetailKitLayout *pOrigKitDetail1=NULL;
	GenesisRoomDetailKitLayout *pOrigKitDetail2=NULL;

	// Detail Kits
	if(pOrigRoom)
	{
		pOrigKitDetail1 = &pOrigRoom->detail_kit_1;
		pOrigKitDetail2 = &pOrigRoom->detail_kit_2;
	}
	y = GMDRefreshRoomDetailKitInfo(pDoc, pExpander, &pGroup->DetailKit1, pOrigKitDetail1, &pRoom->detail_kit_1, "Detail Kit 1", y);
	y = GMDRefreshRoomDetailKitInfo(pDoc, pExpander, &pGroup->DetailKit2, pOrigKitDetail2, &pRoom->detail_kit_2, "Detail Kit 2", y);

	// Set the expander height
	ui_ExpanderSetHeight(pExpander, y);

	return y;
}

static void GMDRefreshRoom(MapDescEditDoc *pDoc, GMDRoomGroup *pGroup, int index, GenesisLayoutRoom ***peaRooms, GenesisLayoutRoom *pOrigRoom, GenesisLayoutRoom *pRoom)
{
	char buf[256];
	F32 y = 0, advanced_y;

	// Refresh the group
	pGroup->peaRooms = peaRooms;

	// Update expander
	sprintf(buf, "Room: %s", pRoom->name);
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);

	// Add popup menu
	if (!pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpRoom, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownRoom, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemoveRoom, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActiveRoomClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActiveRoomCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActiveRoomCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActiveRoomPaste, pGroup, NULL ),
				ui_MenuItemCreate("Reseed", UIMenuCallback, GMDActiveRoomReseed, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	}
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the room.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigRoom, pRoom, parse_GenesisLayoutRoom, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage,
							4, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigRoom, pRoom);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update room specifier
	pGroup->pRoomSpecLabel = GMDRefreshLabel(pGroup->pRoomSpecLabel, "Room", "Determines how the room is chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pRoomSpecField) {
		pGroup->pRoomSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigRoom, pRoom, parse_GenesisLayoutRoom, "RoomSpecifier", GenesisTagOrNameEnum);
		GMDAddFieldToParent(pGroup->pRoomSpecField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pRoomSpecField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRoomSpecField, pOrigRoom, pRoom);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pRoom->room_specifier == GenesisTagOrName_RandomByTag) {
		// Update room tags
		GMDRefreshTagsSpecifier(&pGroup->pRoomTagsLabel, &pGroup->pRoomTagsField, &pGroup->pRoomTagsErrorPane,
								pOrigRoom, pRoom, parse_GenesisLayoutRoom, "RoomTags2",
								NULL, GENESIS_ROOM_DEF_DICTIONARY, &gRoomTags, NULL,
								X_OFFSET_BASE + X_OFFSET_INDENT, y, pGroup->pExpander, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pRoomTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRoomTagsErrorPane);
		MEFieldSafeDestroy(&pGroup->pRoomTagsField);
	}

	if (pRoom->room_specifier == GenesisTagOrName_SpecificByName) {
		// Update room name
		pGroup->pRoomNameLabel = GMDRefreshLabel(pGroup->pRoomNameLabel, "Name", "The Room will be the one named.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pRoomNameField) {
			pGroup->pRoomNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigRoom, pRoom, parse_GenesisLayoutRoom, "RoomDef", GENESIS_ROOM_DEF_DICTIONARY, "ResourceName");
			GMDAddFieldToParent(pGroup->pRoomNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pRoomNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pRoomNameField, pOrigRoom, pRoom);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pRoomNameLabel);
		MEFieldSafeDestroy(&pGroup->pRoomNameField);
	}

	if(pDoc->EditingMapType == GenesisMapType_Exterior)
	{
		// Update off map check box
		pGroup->pOffMapLabel = GMDRefreshLabel(pGroup->pOffMapLabel, "Placed Off Map", "The clearing will be placed outside the playable area.  Details will be placed outside as well, but mission items will be placed in the path to this clearing.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pOffMapField) {
			pGroup->pOffMapField = MEFieldCreateSimple(kMEFieldType_Check, pOrigRoom, pRoom, parse_GenesisLayoutRoom, "OffMap");
			GMDAddFieldToParent(pGroup->pOffMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOffMapField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOffMapField, pOrigRoom, pRoom);
		}
		
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pOffMapLabel);
		MEFieldSafeDestroy(&pGroup->pOffMapField);
	}

	if(!pGroup->pAdvancedExpanderGroup) {
		pGroup->pAdvancedExpanderGroup = ui_ExpanderGroupCreate();
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAdvancedExpanderGroup);

		pGroup->pAdvancedExpander = GMDCreateExpander(pGroup->pAdvancedExpanderGroup, "Advanced Options", 0);
		pGroup->pAdvancedExpander->widget.drawF = GMDExpanderDrawAdvancedTint;
		ui_ExpanderSetExpandCallback(pGroup->pAdvancedExpander, GMDExpanderExpanded, pDoc);
		ui_ExpanderSetOpened(pGroup->pAdvancedExpander, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAdvancedExpanderGroup), X_OFFSET_BASE, y);
	advanced_y = GMDRefreshRoomAdvancedOptions(pDoc, pGroup, pGroup->pAdvancedExpander, pOrigRoom, pRoom);
	if(ui_ExpanderIsOpened(pGroup->pAdvancedExpander))
	{
		ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pAdvancedExpanderGroup), 1.0, STANDARD_ROW_HEIGHT-3+advanced_y, UIUnitPercentage, UIUnitFixed);
		y += advanced_y;
	}
	else
	{
		ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pAdvancedExpanderGroup), 1.0, STANDARD_ROW_HEIGHT-3, UIUnitPercentage, UIUnitFixed);	
	}

	y += STANDARD_ROW_HEIGHT;

	GMDRefreshButtonSet(pGroup->pExpander, 0, y, index > 0, index < eaSize(peaRooms)-1, "Delete Room", &pGroup->pRemoveButton, GMDRemoveRoom, &pGroup->pUpButton, GMDUpRoom, &pGroup->pDownButton, GMDDownRoom, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void GMDActivePathClone( void* ignored1, GMDPathGroup* pGroup )
{
	GenesisLayoutPath *pPath;
	GenesisLayoutPath *pNewPath;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPath = (*pGroup->peaPaths)[pGroup->index];

	// Perform the operation
	pNewPath = StructClone( parse_GenesisLayoutPath, pPath );
	GMDPathUniquifyName( *pGroup->peaPaths, pNewPath );
	eaInsert( pGroup->peaPaths, pNewPath, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePathCut( void* ignored1, GMDPathGroup* pGroup )
{
	GenesisLayoutPath *pPath;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPath = (*pGroup->peaPaths)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisLayoutPath, pPath, &g_GMDClipboardPath );
	StructDestroy( parse_GenesisLayoutPath, pPath );
	eaRemove( pGroup->peaPaths, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePathCopy( void* ignored1, GMDPathGroup* pGroup )
{
	GenesisLayoutPath *pPath;

	if( !pGroup ) {
		return;
	}
	pPath = (*pGroup->peaPaths)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisLayoutPath, pPath, &g_GMDClipboardPath );
	g_GMDClipboardPath.detail_seed = 0;
}

static void GMDActivePathPaste( void* ignored1, GMDPathGroup* pGroup )
{
	GenesisLayoutPath *pPath;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPath = (*pGroup->peaPaths)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_GenesisLayoutPath, &g_GMDClipboardPath, pPath );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePathReseed( void* ignored1, GMDPathGroup* pGroup )
{
	GenesisLayoutPath *pPath;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPath = (*pGroup->peaPaths)[pGroup->index];

	// Perform the operation
	pPath->detail_seed = randomU32();

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static int GMDRefreshPathAdvancedOptions(MapDescEditDoc *pDoc, GMDPathGroup *pGroup, UIExpander *pExpander, GenesisLayoutPath *pOrigPath, GenesisLayoutPath *pPath)
{
	int y = 0;
	GenesisRoomDetailKitLayout *pOrigKitDetail1=NULL;
	GenesisRoomDetailKitLayout *pOrigKitDetail2=NULL;

	// Detail Kits
	if(pOrigPath)
	{
		pOrigKitDetail1 = &pOrigPath->detail_kit_1;
		pOrigKitDetail2 = &pOrigPath->detail_kit_2;
	}
	y = GMDRefreshRoomDetailKitInfo(pDoc, pExpander, &pGroup->DetailKit1, pOrigKitDetail1, &pPath->detail_kit_1, "Detail Kit 1", y);
	y = GMDRefreshRoomDetailKitInfo(pDoc, pExpander, &pGroup->DetailKit2, pOrigKitDetail2, &pPath->detail_kit_2, "Detail Kit 2", y);

	pGroup->pLengthLabel = GMDRefreshLabel(pGroup->pLengthLabel, "Length:", "The min and max distance from game play.", X_OFFSET_BASE, 0, y, pExpander);

	// Update Min Dist
	pGroup->pMinLengthLabel = GMDRefreshLabel(pGroup->pMinLengthLabel, "Min", NULL, X_OFFSET_CONTROL-30, 0, y, pExpander);
	if (!pGroup->pMinLengthField) {
		pGroup->pMinLengthField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPath, pPath, parse_GenesisLayoutPath, "MinLength");
		GMDAddFieldToParent(pGroup->pMinLengthField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMinLengthField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMinLengthField, pOrigPath, pPath);
	}

	// Update Max Length
	pGroup->pMaxLengthLabel = GMDRefreshLabel(pGroup->pMaxLengthLabel, "Max", NULL, X_OFFSET_CONTROL+60, 0, y, pExpander);
	if (!pGroup->pMaxLengthField) {
		pGroup->pMaxLengthField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPath, pPath, parse_GenesisLayoutPath, "MaxLength");
		GMDAddFieldToParent(pGroup->pMaxLengthField, UI_WIDGET(pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMaxLengthField->pUIWidget, X_OFFSET_CONTROL+90, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxLengthField, pOrigPath, pPath);
	}

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pExpander, y);

	return y;
}

static void GMDRefreshPath(MapDescEditDoc *pDoc, GMDPathGroup *pGroup, int index, GenesisLayoutPath ***peaPaths, GenesisLayoutPath *pOrigPath, GenesisLayoutPath *pPath)
{
	char buf[256];
	F32 y = 0, advanced_y;

	// Refresh the group
	pGroup->peaPaths = peaPaths;

	// Update expander
	sprintf(buf, "Path: %s", pPath->name);
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);

	// Add popup menu
	if (!pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpPath, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownPath, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemovePath, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActivePathClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActivePathCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActivePathCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActivePathPaste, pGroup, NULL ),
				ui_MenuItemCreate("Reseed", UIMenuCallback, GMDActivePathReseed, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	}
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the path.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPath, pPath, parse_GenesisLayoutPath, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage,
							4, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigPath, pPath);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update start room
	pGroup->pStartLabel = GMDRefreshLabel(pGroup->pStartLabel, "Start Room", "The room the path starts at.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pStartField) {
		pGroup->pStartField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigPath, pPath, parse_GenesisLayoutPath, "StartRoom", NULL, &pDoc->eaRoomNames, NULL);
		GMDAddFieldToParent(pGroup->pStartField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pStartField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pStartField, pOrigPath, pPath);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update end room
	pGroup->pEndLabel = GMDRefreshLabel(pGroup->pEndLabel, "End Room", "The room the path ends at.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pEndField) {
		pGroup->pEndField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigPath, pPath, parse_GenesisLayoutPath, "EndRoom", NULL, &pDoc->eaRoomNames, NULL);
		GMDAddFieldToParent(pGroup->pEndField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pEndField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pEndField, pOrigPath, pPath);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update path specifier
	pGroup->pPathSpecLabel = GMDRefreshLabel(pGroup->pPathSpecLabel, "Path", "Determines how the Path is chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pPathSpecField) {
		pGroup->pPathSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigPath, pPath, parse_GenesisLayoutPath, "PathSpecifier", GenesisTagOrNameEnum);
		GMDAddFieldToParent(pGroup->pPathSpecField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pPathSpecField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pPathSpecField, pOrigPath, pPath);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pPath->path_specifier == GenesisTagOrName_RandomByTag) {
		// Update path tags
		GMDRefreshTagsSpecifier(&pGroup->pPathTagsLabel, &pGroup->pPathTagsField, &pGroup->pPathTagsErrorPane,
								pOrigPath, pPath, parse_GenesisLayoutPath, "PathTags2",
								NULL, GENESIS_PATH_DEF_DICTIONARY, &gPathTags, NULL,
								X_OFFSET_BASE + X_OFFSET_INDENT, y, pGroup->pExpander, pDoc);
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pPathTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPathTagsErrorPane);
		MEFieldSafeDestroy(&pGroup->pPathTagsField);
	}

	if (pPath->path_specifier == GenesisTagOrName_SpecificByName) {
		// Update path name
		pGroup->pPathNameLabel = GMDRefreshLabel(pGroup->pPathNameLabel, "Name", "The Path will be the one named.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pPathNameField) {
			pGroup->pPathNameField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigPath, pPath, parse_GenesisLayoutPath, "PathDef", GENESIS_PATH_DEF_DICTIONARY, "ResourceName");
			GMDAddFieldToParent(pGroup->pPathNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pPathNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pPathNameField, pOrigPath, pPath);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pPathNameLabel);
		MEFieldSafeDestroy(&pGroup->pPathNameField);
	}

	if(!pGroup->pAdvancedExpanderGroup) {
		pGroup->pAdvancedExpanderGroup = ui_ExpanderGroupCreate();
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAdvancedExpanderGroup);

		pGroup->pAdvancedExpander = GMDCreateExpander(pGroup->pAdvancedExpanderGroup, "Advanced Options", 0);
		pGroup->pAdvancedExpander->widget.drawF = GMDExpanderDrawAdvancedTint;
		ui_ExpanderSetExpandCallback(pGroup->pAdvancedExpander, GMDExpanderExpanded, pDoc);
		ui_ExpanderSetOpened(pGroup->pAdvancedExpander, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pAdvancedExpanderGroup), X_OFFSET_BASE, y);
	advanced_y = GMDRefreshPathAdvancedOptions(pDoc, pGroup, pGroup->pAdvancedExpander, pOrigPath, pPath);
	if(ui_ExpanderIsOpened(pGroup->pAdvancedExpander))
	{
		ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pAdvancedExpanderGroup), 1.0, STANDARD_ROW_HEIGHT-3+advanced_y, UIUnitPercentage, UIUnitFixed);
		y += advanced_y;
	}
	else
	{
		ui_WidgetSetDimensionsEx(UI_WIDGET(pGroup->pAdvancedExpanderGroup), 1.0, STANDARD_ROW_HEIGHT-3, UIUnitPercentage, UIUnitFixed);	
	}

	y += STANDARD_ROW_HEIGHT;

	GMDRefreshButtonSet(pGroup->pExpander, 0, y, index > 0, index < eaSize(peaPaths)-1, "Delete Path", &pGroup->pRemoveButton, GMDRemovePath, &pGroup->pUpButton, GMDUpPath, &pGroup->pDownButton, GMDDownPath, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}


static void GMDRefreshRoomsAndPaths(MapDescEditDoc *pDoc)
{
	GenesisLayoutRoom ***peaRooms = NULL;
	GenesisLayoutRoom ***peaOrigRooms = NULL;
	GenesisLayoutPath ***peaPaths = NULL;
	GenesisLayoutPath ***peaOrigPaths = NULL;
	int iNumRooms;
	int iNumPaths;
	int i;

	// Set up pointers for updates
	if (pDoc->EditingMapType == GenesisMapType_Exterior) {
		peaRooms = &pDoc->pMapDesc->exterior_layout->rooms;
		peaPaths = &pDoc->pMapDesc->exterior_layout->paths;
		if (pDoc->pOrigMapDesc && pDoc->pOrigMapDesc->exterior_layout) {
			peaOrigRooms = &pDoc->pOrigMapDesc->exterior_layout->rooms;
			peaOrigPaths = &pDoc->pOrigMapDesc->exterior_layout->paths;
		}
	} else if (pDoc->EditingMapType == GenesisMapType_Interior) {
		peaRooms = &pDoc->pEditingInterior->rooms;
		peaPaths = &pDoc->pEditingInterior->paths;
		if (pDoc->pOrigMapDesc && pDoc->pOrigEditingInterior) {
			peaOrigRooms = &pDoc->pOrigEditingInterior->rooms;
			peaOrigPaths = &pDoc->pOrigEditingInterior->paths;
		}
	}
	// Leave both as NULL and it will clean up

	// Remove unused room groups
	iNumRooms = peaRooms ? eaSize(peaRooms) : 0;
	for(i=eaSize(&pDoc->eaRoomGroups)-1; i>=iNumRooms; --i) {
		GMDFreeRoomGroup(pDoc->eaRoomGroups[i]);
		eaRemove(&pDoc->eaRoomGroups, i);
	}
	
	// Refresh rooms
	for(i=0; i<iNumRooms; ++i) {
		GenesisLayoutRoom *pRoom = (*peaRooms)[i];
		GenesisLayoutRoom *pOrigRoom = NULL;

		if (eaSize(&pDoc->eaRoomGroups) <= i) {
			GMDRoomGroup *pGroup = calloc(1, sizeof(GMDRoomGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pLayoutExpanderGroup, "Room", i+1);
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaRoomGroups, pGroup);
		}
		pDoc->eaRoomGroups[i]->index = i;
			
		if (peaOrigRooms && eaSize(peaOrigRooms) > i) {
			pOrigRoom = (*peaOrigRooms)[i];
		}

		GMDRefreshRoom(pDoc, pDoc->eaRoomGroups[i], i, peaRooms, pOrigRoom, pRoom);
	}

	// Remove unused path groups
	iNumPaths = peaPaths ? eaSize(peaPaths) : 0;
	for(i=eaSize(&pDoc->eaPathGroups)-1; i>=iNumPaths; --i) {
		GMDFreePathGroup(pDoc->eaPathGroups[i]);
		eaRemove(&pDoc->eaPathGroups, i);
	}

	// Refresh paths
	for(i=0; i<iNumPaths; ++i) {
		GenesisLayoutPath *pPath = (*peaPaths)[i];
		GenesisLayoutPath *pOrigPath = NULL;

		if (eaSize(&pDoc->eaPathGroups) <= i) {
			GMDPathGroup *pGroup = calloc(1, sizeof(GMDPathGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pLayoutExpanderGroup, "Path", iNumRooms+i+1);
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaPathGroups, pGroup);
		}
		pDoc->eaPathGroups[i]->index = i;
		
		if (peaOrigPaths && eaSize(peaOrigPaths) > i) {
			pOrigPath = (*peaOrigPaths)[i];
		}

		GMDRefreshPath(pDoc, pDoc->eaPathGroups[i], i, peaPaths, pOrigPath, pPath);
	}
}


static F32 GMDRefreshObjectRef(MapDescEditDoc *pDoc, GMDObjectRefGroup *pGroup, F32 y, int index, SSLibObj ***peaObjects, SSLibObj *pObj, SSLibObj *pOrigObj, bool include_offset, bool include_dist, bool include_count)
{
	// Update refresh data
	pGroup->peaObjects = peaObjects;
	pGroup->index = index;

	// Update name
	pGroup->pObjectLabel = GMDRefreshLabel(pGroup->pObjectLabel, "Object", "The name of the object.", X_OFFSET_BASE+20, 0, y, pGroup->pExpander);
	if (!pGroup->pObjectEntry) {
		pGroup->pObjectEntry = ui_TextEntryCreateWithGlobalDictionaryCombo("", X_OFFSET_CONTROL, y, OBJECT_LIBRARY_DICT, "ResourceNotes", true, true, true, true);
		ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pObjectEntry), 1.0, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pObjectEntry), 0, 50, 0, 0);
		ui_TextEntrySetChangedCallback(pGroup->pObjectEntry, GMDObjectRefChanged, pGroup);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pObjectEntry);
	}
	ui_TextEntrySetText(pGroup->pObjectEntry, (*peaObjects)[index]->obj.name_str);
	ui_WidgetSetPosition(UI_WIDGET(pGroup->pObjectEntry), X_OFFSET_CONTROL, y);

	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Del", 0, y, GMDRemoveObjectRef, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 40);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 0, y, 0, 0, UITopRight);

	y += STANDARD_ROW_HEIGHT;

	if(include_count)
	{
		// Update Min Dist
		pGroup->pMinCountLabel = GMDRefreshLabel(pGroup->pMinCountLabel, "Count: Min", "The min and max count to place.  Will never place 0.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinCountField) {
			pGroup->pMinCountField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "MinCount");
			GMDAddFieldToParent(pGroup->pMinCountField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinCountField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinCountField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		// Update Max Dist
		pGroup->pMaxCountLabel = GMDRefreshLabel(pGroup->pMaxCountLabel, "Max", "The min and max count to place.  Will never place 0.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxCountField) {
			pGroup->pMaxCountField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "MaxCount");
			GMDAddFieldToParent(pGroup->pMaxCountField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxCountField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxCountField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		y += STANDARD_ROW_HEIGHT;	
	}

	if(include_dist)
	{
		// Update Min Dist
		pGroup->pMinDistLabel = GMDRefreshLabel(pGroup->pMinDistLabel, "Dist: Min", "The min and max distance from game play.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinDistField) {
			pGroup->pMinDistField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "MinDist");
			GMDAddFieldToParent(pGroup->pMinDistField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinDistField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinDistField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		// Update Max Dist
		pGroup->pMaxDistLabel = GMDRefreshLabel(pGroup->pMaxDistLabel, "Max", "The min and max distance from game play.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxDistField) {
			pGroup->pMaxDistField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "MaxDist");
			GMDAddFieldToParent(pGroup->pMaxDistField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxDistField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxDistField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		y += STANDARD_ROW_HEIGHT;
	}

	if(include_offset)
	{
		// Update Min Horiz
		pGroup->pMinHorizLabel = GMDRefreshLabel(pGroup->pMinHorizLabel, "Horiz: Min", "The min and max horizontal offset variance of the object.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinHorizField) {
			pGroup->pMinHorizField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "OffsetMin");
			GMDAddFieldToParent(pGroup->pMinHorizField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinHorizField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinHorizField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		// Update Max Horiz
		pGroup->pMaxHorizLabel = GMDRefreshLabel(pGroup->pMaxHorizLabel, "Max", "The min and max horizontal offset variance of the object.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxHorizField) {
			pGroup->pMaxHorizField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "OffsetMax");
			GMDAddFieldToParent(pGroup->pMaxHorizField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxHorizField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxHorizField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update Min Vert
		pGroup->pMinVertLabel = GMDRefreshLabel(pGroup->pMinVertLabel, "Vert: Min", "The min and max vertical offset variance of the object.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinVertField) {
			pGroup->pMinVertField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "OffsetMin");
			pGroup->pMinVertField->arrayIndex = 1;
			GMDAddFieldToParent(pGroup->pMinVertField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinVertField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinVertField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		// Update Max Vert
		pGroup->pMaxVertLabel = GMDRefreshLabel(pGroup->pMaxVertLabel, "Max", "The min and max vertical offset variance of the object.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxVertField) {
			pGroup->pMaxVertField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "OffsetMax");
			pGroup->pMaxVertField->arrayIndex = 1;
			GMDAddFieldToParent(pGroup->pMaxVertField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxVertField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxVertField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		y += STANDARD_ROW_HEIGHT;

		//Update Detached Flag
		pGroup->pDetachedLabel = GMDRefreshLabel(pGroup->pDetachedLabel, "Detached", "If the object is detached from the curve then it will only be placed once somewhere along the curve.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pDetachedField) {
			pGroup->pDetachedField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset, parse_SSOffset, "Detached");
			GMDAddFieldToParent(pGroup->pDetachedField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pDetachedField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pDetachedField, pOrigObj ? &pOrigObj->offset : NULL, &pObj->offset);
		}

		y += STANDARD_ROW_HEIGHT;
	}

	return y;
}


static F32 GMDRefreshObjectTag(MapDescEditDoc *pDoc, GMDObjectTagGroup *pGroup, F32 y, int index, SSTagObj ***peaTags, SSTagObj *pTag, SSTagObj *pOrigTag, bool include_offset, bool include_dist, bool include_count)
{
	// Update refresh data
	pGroup->peaTags = peaTags;
	pGroup->index = index;

	// Update name
	GMDRefreshTagsSpecifier( &pGroup->pTagLabel, &pGroup->pTagField, &pGroup->pTagErrorPane,
							 pOrigTag, pTag, parse_SSTagObj, "Tags",
							 NULL, OBJECT_LIBRARY_DICT, &gObjectLibTags, SAFE_MEMBER( pDoc->pEditingSolSys, environment_tags ),
							 X_OFFSET_BASE + 20, y, pGroup->pExpander, pDoc );
	ui_WidgetSetPaddingEx(pGroup->pTagField->pUIWidget, 0, 50, 0, 0);
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Del", 0, y, GMDRemoveObjectTag, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 40);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pRemoveButton);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 0, y, 0, 0, UITopRight);

	y += STANDARD_ROW_HEIGHT;
	
	if(include_count)
	{
		// Update Min Dist
		pGroup->pMinCountLabel = GMDRefreshLabel(pGroup->pMinCountLabel, "Count: Min", "The min and max count to place.  Will never place 0.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinCountField) {
			pGroup->pMinCountField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "MinCount");
			GMDAddFieldToParent(pGroup->pMinCountField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinCountField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinCountField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		// Update Max Dist
		pGroup->pMaxCountLabel = GMDRefreshLabel(pGroup->pMaxCountLabel, "Max", "The min and max count to place.  Will never place 0.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxCountField) {
			pGroup->pMaxCountField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "MaxCount");
			GMDAddFieldToParent(pGroup->pMaxCountField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxCountField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxCountField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		y += STANDARD_ROW_HEIGHT;	
	}

	if(include_dist)
	{
		// Update Min Dist
		pGroup->pMinDistLabel = GMDRefreshLabel(pGroup->pMinDistLabel, "Dist: Min", "The min and max distance from game play.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinDistField) {
			pGroup->pMinDistField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "MinDist");
			GMDAddFieldToParent(pGroup->pMinDistField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinDistField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinDistField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		// Update Max Dist
		pGroup->pMaxDistLabel = GMDRefreshLabel(pGroup->pMaxDistLabel, "Max", "The min and max distance from game play.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxDistField) {
			pGroup->pMaxDistField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "MaxDist");
			GMDAddFieldToParent(pGroup->pMaxDistField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxDistField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxDistField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		y += STANDARD_ROW_HEIGHT;
	}

	if(include_offset)
	{
		// Update Min Horiz
		pGroup->pMinHorizLabel = GMDRefreshLabel(pGroup->pMinHorizLabel, "Horiz: Min", "The min and max horizontal offset variance of the object.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinHorizField) {
			pGroup->pMinHorizField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "OffsetMin");
			GMDAddFieldToParent(pGroup->pMinHorizField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinHorizField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinHorizField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		// Update Max Horiz
		pGroup->pMaxHorizLabel = GMDRefreshLabel(pGroup->pMaxHorizLabel, "Max", "The min and max horizontal offset variance of the object.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxHorizField) {
			pGroup->pMaxHorizField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "OffsetMax");
			GMDAddFieldToParent(pGroup->pMaxHorizField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxHorizField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxHorizField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update Min Vert
		pGroup->pMinVertLabel = GMDRefreshLabel(pGroup->pMinVertLabel, "Vert: Min", "The min and max vertical offset variance of the object.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pMinVertField) {
			pGroup->pMinVertField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "OffsetMin");
			pGroup->pMinVertField->arrayIndex = 1;
			GMDAddFieldToParent(pGroup->pMinVertField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinVertField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinVertField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		// Update Max Vert
		pGroup->pMaxVertLabel = GMDRefreshLabel(pGroup->pMaxVertLabel, "Max", "The min and max vertical offset variance of the object.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxVertField) {
			pGroup->pMaxVertField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "OffsetMax");
			pGroup->pMaxVertField->arrayIndex = 1;
			GMDAddFieldToParent(pGroup->pMaxVertField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxVertField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxVertField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		y += STANDARD_ROW_HEIGHT;

		//Update Detached Flag
		pGroup->pDetachedLabel = GMDRefreshLabel(pGroup->pDetachedLabel, "Detached", "If the object is detached from the curve then it will only be placed once somewhere along the curve.", X_OFFSET_BASE+40, 0, y, pGroup->pExpander);
		if (!pGroup->pDetachedField) {
			pGroup->pDetachedField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset, parse_SSOffset, "Detached");
			GMDAddFieldToParent(pGroup->pDetachedField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pDetachedField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pDetachedField, pOrigTag ? &pOrigTag->offset : NULL, &pTag->offset);
		}

		y += STANDARD_ROW_HEIGHT;
	}

	return y;
}

static void GMDActivePointClone( void* ignored1, GMDPointGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	ShoeboxPointList ***peaLists = &pDoc->pEditingSolSys->shoebox.point_lists;
	ShoeboxPoint *pNewPoint;
	ShoeboxPoint *pPoint;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPoint = (*pGroup->peaPoints)[pGroup->index];

	// Perform the operation
	pNewPoint = StructClone( parse_ShoeboxPoint, pPoint );
	GMDPointUniquifyName( *peaLists, pNewPoint );
	eaInsert( pGroup->peaPoints, pNewPoint, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePointCut( void* ignored1, GMDPointGroup* pGroup )
{
	ShoeboxPoint *pPoint;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPoint = (*pGroup->peaPoints)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_ShoeboxPoint, pPoint, &g_GMDClipboardPoint );
	StructDestroy( parse_ShoeboxPoint, pPoint );
	eaRemove( pGroup->peaPoints, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePointCopy( void* ignored1, GMDPointGroup* pGroup )
{
	ShoeboxPoint *pPoint;

	if( !pGroup ) {
		return;
	}
	pPoint = (*pGroup->peaPoints)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_ShoeboxPoint, pPoint, &g_GMDClipboardPoint );
}

static void GMDActivePointPaste( void* ignored1, GMDPointGroup* pGroup )
{
	ShoeboxPoint *pPoint;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPoint = (*pGroup->peaPoints)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_ShoeboxPoint, &g_GMDClipboardPoint, pPoint );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static int GMDRefreshPoint(MapDescEditDoc *pDoc, F32 y, GMDPointGroup *pGroup, int index, ShoeboxPoint ***peaPoints, ShoeboxPoint *pPoint, ShoeboxPoint *pOrigPoint)
{
	char buf[1024];

	// Update refresh data
	pGroup->peaPoints = peaPoints;
	pGroup->index = index;

	// Update title
	sprintf(buf, "Room: %s", pPoint->name);
	pGroup->pTitleLabel = GMDRefreshLabel(pGroup->pTitleLabel, buf, NULL, X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	ui_WidgetSkin(UI_WIDGET(pGroup->pTitleLabel), gBoldExpanderSkin);
	y += STANDARD_ROW_HEIGHT;
	
	// Add popup menu
	if (!pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpPoint, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownPoint, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemovePoint, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActivePointClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActivePointCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActivePointCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActivePointPaste, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	}
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the room.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPoint, pPoint, parse_ShoeboxPoint, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage,
							4, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigPoint, pPoint);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update radius
	sprintf(buf, "(Default Best Fit)");
	pGroup->pRadiusLabel = GMDRefreshLabel(pGroup->pRadiusLabel, "Radius", "The size of the 'room' at this point.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	pGroup->pRadius2Label = GMDRefreshLabel(pGroup->pRadius2Label, buf, NULL, X_OFFSET_CONTROL+90, 0, y, pGroup->pExpander);
	if (!pGroup->pRadiusField) {
		pGroup->pRadiusField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPoint, pPoint, parse_ShoeboxPoint, "Radius");
		GMDAddFieldToParent(pGroup->pRadiusField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pRadiusField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRadiusField, pOrigPoint, pPoint);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update min cluster dist
	pGroup->pMinClusterDistLabel = GMDRefreshLabel(pGroup->pMinClusterDistLabel, "Min Internal Dist", "Min distance between objects inside the point.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	if (!pGroup->pMinClusterDistField) {
		pGroup->pMinClusterDistField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPoint, pPoint, parse_ShoeboxPoint, "MinDist");
		GMDAddFieldToParent(pGroup->pMinClusterDistField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMinClusterDistField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMinClusterDistField, pOrigPoint, pPoint);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update max cluster dist
	pGroup->pMaxClusterDistLabel = GMDRefreshLabel(pGroup->pMaxClusterDistLabel, "Max Internal Dist", "Min distance between objects inside the point.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	if (!pGroup->pMaxClusterDistField) {
		pGroup->pMaxClusterDistField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPoint, pPoint, parse_ShoeboxPoint, "MaxDist");
		GMDAddFieldToParent(pGroup->pMaxClusterDistField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMaxClusterDistField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxClusterDistField, pOrigPoint, pPoint);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update distance from previous
	sprintf(buf, "(Default %g)", SOLAR_SYSTEM_ENCOUNTER_DIST);
	pGroup->pDistFromPrevLabel = GMDRefreshLabel(pGroup->pDistFromPrevLabel, "Dist from Previous", "The distance from previous room.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	pGroup->pDist2FromPrevLabel = GMDRefreshLabel(pGroup->pDist2FromPrevLabel, buf, NULL, X_OFFSET_CONTROL+90, 0, y, pGroup->pExpander);
	if (!pGroup->pDistFromPrevField) {
		pGroup->pDistFromPrevField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPoint, pPoint, parse_ShoeboxPoint, "DistFromLast");
		GMDAddFieldToParent(pGroup->pDistFromPrevField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pDistFromPrevField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDistFromPrevField, pOrigPoint, pPoint);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update facing
	pGroup->pFacingLabel = GMDRefreshLabel(pGroup->pFacingLabel, "Facing", "The facing to use.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
	if (!pGroup->pFacingField) {
		pGroup->pFacingField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigPoint, pPoint, parse_ShoeboxPoint, "FacingDir", PointListFacingDirectionEnum);
		GMDAddFieldToParent(pGroup->pFacingField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pFacingField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pFacingField, pOrigPoint, pPoint);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pPoint->face_dir == PLFD_Parent) {
		// Update facing offset
		pGroup->pFacingOffsetLabel = GMDRefreshLabel(pGroup->pFacingOffsetLabel, "Offset Angle", "The offset angle in degrees from the facing.", X_OFFSET_BASE + 2 * X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pFacingOffsetField) {
			pGroup->pFacingOffsetField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPoint, pPoint, parse_ShoeboxPoint, "FacingOffset");
			GMDAddFieldToParent(pGroup->pFacingOffsetField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pFacingOffsetField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pFacingOffsetField, pOrigPoint, pPoint);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pFacingOffsetLabel);
		MEFieldSafeDestroy(&pGroup->pFacingOffsetField);
	}

	GMDRefreshButtonSet(pGroup->pExpander, X_OFFSET_INDENT, y, (index > 0), (index < eaSize(peaPoints)-1), "Delete Room", &pGroup->pRemoveButton, GMDRemovePoint, &pGroup->pUpButton, GMDUpPoint, &pGroup->pDownButton, GMDDownPoint, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);

	return y;
}

static void GMDActivePointListClone( void* ignored1, GMDPointListGroup* pGroup )
{
	ShoeboxPointList *pNewPointList;
	ShoeboxPointList *pPointList;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPointList = (*pGroup->peaPointLists)[pGroup->index];

	// Perform the operation
	pNewPointList = StructClone( parse_ShoeboxPointList, pPointList );
	eaInsert( pGroup->peaPointLists, pNewPointList, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePointListCut( void* ignored1, GMDPointListGroup* pGroup )
{
	ShoeboxPointList *pPointList;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPointList = (*pGroup->peaPointLists)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_ShoeboxPointList, pPointList, &g_GMDClipboardPointList );
	StructDestroy( parse_ShoeboxPointList, pPointList );
	eaRemove( pGroup->peaPointLists, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePointListCopy( void* ignored1, GMDPointListGroup* pGroup )
{
	ShoeboxPointList *pPointList;

	if( !pGroup ) {
		return;
	}
	pPointList = (*pGroup->peaPointLists)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_ShoeboxPointList, pPointList, &g_GMDClipboardPointList );
}

static void GMDActivePointListPaste( void* ignored1, GMDPointListGroup* pGroup )
{
	ShoeboxPointList *pPointList;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPointList = (*pGroup->peaPointLists)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_ShoeboxPointList, &g_GMDClipboardPointList, pPointList );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDRefreshPointList(MapDescEditDoc *pDoc, GMDPointListGroup *pGroup, int iListIndex, ShoeboxPointList ***peaLists, ShoeboxPointList *pList, ShoeboxPointList *pOrigList)
{
	F32 y = 0;
	char buf[256];
	int i, j;
	GenesisShoeboxLayout *pShoebox;
	int iNumPoints = 0;

	// Refresh point list data
	pGroup->index = iListIndex;
	pGroup->peaPointLists = peaLists;

	pShoebox = &pDoc->pEditingSolSys->shoebox;

	// Update expander
	sprintf(buf, "Room List #%d", iListIndex+1);
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);

	// Add popup menu
	if (!pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpPointList, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownPointList, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemovePointList, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActivePointListClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActivePointListCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActivePointListCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActivePointListPaste, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	}
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update list type
	pGroup->pListTypeLabel = GMDRefreshLabel(pGroup->pListTypeLabel, "List Shape", "The shape of the event list.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pListTypeField) {
		pGroup->pListTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigList, pList, parse_ShoeboxPointList, "Type", ShoeboxPointListTypeEnum);
		GMDAddFieldToParent(pGroup->pListTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pListTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pListTypeField, pOrigList, pList);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pList->list_type == SBLT_Orbit) {
		char *pcOrbitName = "";
		if (!pList->orbit_object) {
			pList->orbit_object = StructCreate(parse_SSObjSet);
			assert(pList->orbit_object);
		}
		
		// Update name
		pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the room.  Can be empty.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pNameField) {
			pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "Name");
			GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage,
								5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigList, pList);
		}

		y += STANDARD_ROW_HEIGHT;

		// Objects to orbit
		pGroup->pOrbitObjLabel = GMDRefreshLabel(pGroup->pOrbitObjLabel, "Objs to Orbit", "The objects to put at the center of the orbit.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pAddOrbitRefButton) {
			pGroup->pAddOrbitRefButton = ui_ButtonCreate("Add Name", X_OFFSET_CONTROL, y, GMDAddOrbitObjectRef, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddOrbitRefButton), 80);
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddOrbitRefButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddOrbitRefButton), X_OFFSET_CONTROL, y);
		}

		if (!pGroup->pAddOrbitTagButton) {
			pGroup->pAddOrbitTagButton = ui_ButtonCreate("Add Tags", X_OFFSET_CONTROL+90, y, GMDAddOrbitObjectTag, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddOrbitTagButton), 80);
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddOrbitTagButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddOrbitTagButton), X_OFFSET_CONTROL+90, y);
		}

		y += STANDARD_ROW_HEIGHT;

		// Update orbit objects
		for(i=0; i<eaSize(&pList->orbit_object->group_refs); ++i) {
			GMDObjectRefGroup *pObjectGroup;
			SSLibObj *pObj, *pOrigObj = NULL;

			if (i >= eaSize(&pGroup->eaOrbitRefGroups)) {
				pObjectGroup = calloc(1, sizeof(GMDObjectRefGroup));
				pObjectGroup->pDoc = pGroup->pDoc;
				pObjectGroup->pExpander = pGroup->pExpander;
				eaPush(&pGroup->eaOrbitRefGroups, pObjectGroup);
			} else {
				pObjectGroup = pGroup->eaOrbitRefGroups[i];
			}

			pObj = pList->orbit_object->group_refs[i];
			if (pOrigList && pOrigList->orbit_object && (i < eaSize(&pOrigList->orbit_object->group_refs))) {
				pOrigObj = pOrigList->orbit_object->group_refs[i];
			}

			y = GMDRefreshObjectRef(pDoc, pObjectGroup, y, i, &pList->orbit_object->group_refs, pObj, pOrigObj, false, false, false);
		}
		for(j=eaSize(&pGroup->eaOrbitRefGroups)-1; j>=i; --j) {
			GMDFreeObjectRefGroup(pGroup->eaOrbitRefGroups[j]);
			eaRemove(&pGroup->eaOrbitRefGroups, j);
		}

		// Update orbit tags
		for(i=0; i<eaSize(&pList->orbit_object->object_tags); ++i) {
			GMDObjectTagGroup *pObjectGroup;
			SSTagObj *pTag, *pOrigTag = NULL;

			if (i >= eaSize(&pGroup->eaOrbitTagGroups)) {
				pObjectGroup = calloc(1, sizeof(GMDObjectTagGroup));
				pObjectGroup->pDoc = pGroup->pDoc;
				pObjectGroup->pExpander = pGroup->pExpander;
				eaPush(&pGroup->eaOrbitTagGroups, pObjectGroup);
			} else {
				pObjectGroup = pGroup->eaOrbitTagGroups[i];
			}

			pTag = pList->orbit_object->object_tags[i];
			if (pOrigList && pOrigList->orbit_object && (i < eaSize(&pOrigList->orbit_object->object_tags))) {
				pOrigTag = pOrigList->orbit_object->object_tags[i];
			}

			y = GMDRefreshObjectTag(pDoc, pObjectGroup, y, i, &pList->orbit_object->object_tags, pTag, pOrigTag, false, false, false);
		}
		for(j=eaSize(&pGroup->eaOrbitTagGroups)-1; j>=i; --j) {
			GMDFreeObjectTagGroup(pGroup->eaOrbitTagGroups[j]);
			eaRemove(&pGroup->eaOrbitTagGroups, j);
		}

		// Update Min Radius
		pGroup->pMinRadiusLabel = GMDRefreshLabel(pGroup->pMinRadiusLabel, "Radius: Min", "The min and max radius variance in the list.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pMinRadiusField) {
			pGroup->pMinRadiusField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MinRad");
			GMDAddFieldToParent(pGroup->pMinRadiusField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMinRadiusField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pMinRadiusField, pOrigList, pList);
		}

		// Update Max Radius
		pGroup->pMaxRadiusLabel = GMDRefreshLabel(pGroup->pMaxRadiusLabel, "Max", "The min and max radius variance in the list.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
		if (!pGroup->pMaxRadiusField) {
			pGroup->pMaxRadiusField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MaxRad");
			GMDAddFieldToParent(pGroup->pMaxRadiusField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pMaxRadiusField->pUIWidget, X_OFFSET_CONTROL+90, y);
			MEFieldSetAndRefreshFromData(pGroup->pMaxRadiusField, pOrigList, pList);
		}

		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pNameLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pOrbitObjLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pAddOrbitRefButton);
		ui_WidgetQueueFreeAndNull(&pGroup->pAddOrbitTagButton);		
		ui_WidgetQueueFreeAndNull(&pGroup->pMinRadiusLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pMaxRadiusLabel);
		MEFieldSafeDestroy(&pGroup->pNameField);
		MEFieldSafeDestroy(&pGroup->pMinRadiusField);
		MEFieldSafeDestroy(&pGroup->pMaxRadiusField);
		eaClearEx(&pGroup->eaOrbitRefGroups, GMDFreeObjectRefGroup);
		eaClearEx(&pGroup->eaOrbitTagGroups, GMDFreeObjectTagGroup);
	}

	// Update Min Tilt
	pGroup->pMinTiltLabel = GMDRefreshLabel(pGroup->pMinTiltLabel, "Tilt: Min", "The min and max tilt variance in the list.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMinTiltField) {
		pGroup->pMinTiltField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MinTilt");
		GMDAddFieldToParent(pGroup->pMinTiltField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMinTiltField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMinTiltField, pOrigList, pList);
	}

	// Update Max Tilt
	pGroup->pMaxTiltLabel = GMDRefreshLabel(pGroup->pMaxTiltLabel, "Max", "The min and max tilt variance in the list.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
	if (!pGroup->pMaxTiltField) {
		pGroup->pMaxTiltField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MaxTilt");
		GMDAddFieldToParent(pGroup->pMaxTiltField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMaxTiltField->pUIWidget, X_OFFSET_CONTROL+90, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxTiltField, pOrigList, pList);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Min Yaw
	pGroup->pMinYawLabel = GMDRefreshLabel(pGroup->pMinYawLabel, "Yaw: Min", "The min and max yaw change in dir from last.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMinYawField) {
		pGroup->pMinYawField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MinYaw");
		GMDAddFieldToParent(pGroup->pMinYawField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMinYawField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMinYawField, pOrigList, pList);
	}

	// Update Max Yaw
	pGroup->pMaxYawLabel = GMDRefreshLabel(pGroup->pMaxYawLabel, "Max", "The min and max yaw change in dir from last.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
	if (!pGroup->pMaxYawField) {
		pGroup->pMaxYawField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MaxYaw");
		GMDAddFieldToParent(pGroup->pMaxYawField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMaxYawField->pUIWidget, X_OFFSET_CONTROL+90, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxYawField, pOrigList, pList);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Min Horiz
	pGroup->pMinHorizLabel = GMDRefreshLabel(pGroup->pMinHorizLabel, "Horizontal: Min", "The min and max horizontal variance in the list.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMinHorizField) {
		pGroup->pMinHorizField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MinHoriz");
		GMDAddFieldToParent(pGroup->pMinHorizField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMinHorizField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMinHorizField, pOrigList, pList);
	}

	// Update Max Horiz
	pGroup->pMaxHorizLabel = GMDRefreshLabel(pGroup->pMaxHorizLabel, "Max", "The min and max horizontal variance in the list.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
	if (!pGroup->pMaxHorizField) {
		pGroup->pMaxHorizField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MaxHoriz");
		GMDAddFieldToParent(pGroup->pMaxHorizField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMaxHorizField->pUIWidget, X_OFFSET_CONTROL+90, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxHorizField, pOrigList, pList);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Min Vert
	pGroup->pMinVertLabel = GMDRefreshLabel(pGroup->pMinVertLabel, "Vertical: Min", "The min and max vertical variance in the list.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pMinVertField) {
		pGroup->pMinVertField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MinVert");
		GMDAddFieldToParent(pGroup->pMinVertField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMinVertField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pMinVertField, pOrigList, pList);
	}

	// Update Max Vert
	pGroup->pMaxVertLabel = GMDRefreshLabel(pGroup->pMaxVertLabel, "Max", "The min and max vertical variance in the list.", X_OFFSET_CONTROL+60, 0, y, pGroup->pExpander);
	if (!pGroup->pMaxVertField) {
		pGroup->pMaxVertField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigList, pList, parse_ShoeboxPointList, "MaxVert");
		GMDAddFieldToParent(pGroup->pMaxVertField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+90, y, 0, 55, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pMaxVertField->pUIWidget, X_OFFSET_CONTROL+90, y);
		MEFieldSetAndRefreshFromData(pGroup->pMaxVertField, pOrigList, pList);
	}

	y += STANDARD_ROW_HEIGHT;

	// Objects on curve
	pGroup->pCurveObjLabel = GMDRefreshLabel(pGroup->pCurveObjLabel, "Objs Along List", "The objects to put along the room list's curve.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pAddCurveRefButton) {
		pGroup->pAddCurveRefButton = ui_ButtonCreate("Add Name", X_OFFSET_CONTROL, y, GMDAddCurveObjectRef, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddCurveRefButton), 80);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddCurveRefButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddCurveRefButton), X_OFFSET_CONTROL, y);
	}

	if (!pGroup->pAddCurveTagButton) {
		pGroup->pAddCurveTagButton = ui_ButtonCreate("Add Tags", X_OFFSET_CONTROL+90, y, GMDAddCurveObjectTag, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddCurveTagButton), 80);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddCurveTagButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddCurveTagButton), X_OFFSET_CONTROL+90, y);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update curve objects
	for(i=0; i<eaSize(&pList->curve_objects); ++i) {
		GMDObjectRefGroup *pObjectGroup;
		SSLibObj *pObj, *pOrigObj = NULL;

		if (i >= eaSize(&pGroup->eaCurveRefGroups)) {
			pObjectGroup = calloc(1, sizeof(GMDObjectRefGroup));
			pObjectGroup->pDoc = pGroup->pDoc;
			pObjectGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaCurveRefGroups, pObjectGroup);
		} else {
			pObjectGroup = pGroup->eaCurveRefGroups[i];
		}

		pObj = pList->curve_objects[i];
		if (pOrigList && (i < eaSize(&pOrigList->curve_objects))) {
			pOrigObj = pOrigList->curve_objects[i];
		}

		y = GMDRefreshObjectRef(pDoc, pObjectGroup, y, i, &pList->curve_objects, pObj, pOrigObj, true, false, false);
	}
	for(j=eaSize(&pGroup->eaCurveRefGroups)-1; j>=i; --j) {
		GMDFreeObjectRefGroup(pGroup->eaCurveRefGroups[j]);
		eaRemove(&pGroup->eaCurveRefGroups, j);
	}

	// Update curve tags
	for(i=0; i<eaSize(&pList->curve_objects_tags); ++i) {
		GMDObjectTagGroup *pObjectGroup;
		SSTagObj *pTag, *pOrigTag = NULL;

		if (i >= eaSize(&pGroup->eaCurveTagGroups)) {
			pObjectGroup = calloc(1, sizeof(GMDObjectTagGroup));
			pObjectGroup->pDoc = pGroup->pDoc;
			pObjectGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaCurveTagGroups, pObjectGroup);
		} else {
			pObjectGroup = pGroup->eaCurveTagGroups[i];
		}

		pTag = pList->curve_objects_tags[i];
		if (pOrigList && (i < eaSize(&pOrigList->curve_objects_tags))) {
			pOrigTag = pOrigList->curve_objects_tags[i];
		}

		y = GMDRefreshObjectTag(pDoc, pObjectGroup, y, i, &pList->curve_objects_tags, pTag, pOrigTag, true, false, false);
	}
	for(j=eaSize(&pGroup->eaCurveTagGroups)-1; j>=i; --j) {
		GMDFreeObjectTagGroup(pGroup->eaCurveTagGroups[j]);
		eaRemove(&pGroup->eaCurveTagGroups, j);
	}

	// Follow Points
	if (pList->list_type == SBLT_ZigZag) {
		pGroup->pFollowLabel = GMDRefreshLabel(pGroup->pFollowLabel, "Follow Points", "Whether the curve should follow the points or just go straight.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pFollowField) {
			pGroup->pFollowField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigList, pList, parse_ShoeboxPointList, "FollowPoints");
			GMDAddFieldToParent(pGroup->pFollowField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pFollowField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pFollowField, pOrigList, pList);
		}

		y += STANDARD_ROW_HEIGHT;

	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pFollowLabel);
		MEFieldSafeDestroy(&pGroup->pFollowField);
	}

	// Equidistant
	pGroup->pEquiLabel = GMDRefreshLabel(pGroup->pEquiLabel, "Equidistant", "Whether the points should be equidistant or not.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pEquiField) {
		pGroup->pEquiField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigList, pList, parse_ShoeboxPointList, "Equidist");
		GMDAddFieldToParent(pGroup->pEquiField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pEquiField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pEquiField, pOrigList, pList);
	}

	y += STANDARD_ROW_HEIGHT;

	// Remove unused points
	iNumPoints = eaSize(&pList->points);
	for(i=eaSize(&pGroup->eaPointGroups) - 1; i>=iNumPoints; --i) {
		assert(pGroup->eaPointGroups);
		GMDFreePointGroup(pGroup->eaPointGroups[i]);
		eaRemove(&pGroup->eaPointGroups, i);
	}

	// Refresh points
	for(i=0; i < iNumPoints; ++i) {
		ShoeboxPoint *pChildPoint = pList->points[i];
		ShoeboxPoint *pOrigChildPoint = NULL;

		if (eaSize(&pGroup->eaPointGroups) <= i) {
			GMDPointGroup *pPointGroup = calloc(1, sizeof(GMDPointGroup));
			pPointGroup->pDoc = pDoc;
			pPointGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaPointGroups, pPointGroup);
		}
		pGroup->eaPointGroups[i]->index = i;
		
		if (pOrigList && eaSize(&pOrigList->points) > i) {
			pOrigChildPoint = pOrigList->points[i];
		}

		y = GMDRefreshPoint(pDoc, y, pGroup->eaPointGroups[i], i, &pList->points, pChildPoint, pOrigChildPoint);
	}

	if (!pGroup->pAddChildButton) {
		pGroup->pAddChildButton = ui_ButtonCreate("Add Room", X_OFFSET_BASE, y, GMDAddPoint, pGroup);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddChildButton), 140);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddChildButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddChildButton), X_OFFSET_BASE, y);
	}
	y += STANDARD_ROW_HEIGHT;

	GMDRefreshButtonSet(pGroup->pExpander, 0, y, (iListIndex > 0), (iListIndex < eaSize(peaLists)-1), "Delete List", &pGroup->pRemoveButton, GMDRemovePointList, &pGroup->pUpButton, GMDUpPointList, &pGroup->pDownButton, GMDDownPointList, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}


static void GMDRefreshListsAndPoints(MapDescEditDoc *pDoc)
{
	ShoeboxPointList ***peaLists = NULL;
	ShoeboxPointList ***peaOrigLists = NULL;
	int iNumLists;
	int i;

	// Set up pointers for updates
	if (pDoc->EditingMapType == GenesisMapType_SolarSystem) {
		peaLists = &pDoc->pEditingSolSys->shoebox.point_lists;
		if (pDoc->pOrigEditingSolSys) {
			peaOrigLists = &pDoc->pOrigEditingSolSys->shoebox.point_lists;
		}
	}
	// Else leave it as NULL and it will clean up

	// Remove unused point lists groups
	iNumLists = peaLists ? eaSize(peaLists) : 0;
	for(i=eaSize(&pDoc->eaPointListGroups)-1; i>=iNumLists; --i) {
		GMDFreePointListGroup(pDoc->eaPointListGroups[i]);
		eaRemove(&pDoc->eaPointListGroups, i);
	}

	// Refresh point lists
	for(i=0; i<iNumLists; ++i) {
		ShoeboxPointList *pList = (*peaLists)[i];
		ShoeboxPointList *pOrigList = NULL;

		if (eaSize(&pDoc->eaPointListGroups) <= i) {
			GMDPointListGroup *pGroup = calloc(1, sizeof(GMDPointListGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pLayoutExpanderGroup, "Room", i+2);
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaPointListGroups, pGroup);
		}
		if (peaOrigLists && eaSize(peaOrigLists) > i) {
			pOrigList = (*peaOrigLists)[i];
		}

		GMDRefreshPointList(pDoc, pDoc->eaPointListGroups[i], i, peaLists, pList, pOrigList);
	}
}

static void GMDContinuePromptBodyTextAdd( void* ignored1, GMDChallengeStartGroup* pGroup )
{
	GenesisMissionStartDescription *pStartDesc;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pStartDesc = &pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.startDescription;

	// Perform the operation
	eaPush( &pStartDesc->eaContinuePromptBodyText, StructAllocString( "" ));

	// Refresh the UI
	GMDMapDescChanged(pDoc, true );
}

static void GMDContinuePromptBodyTextRemove( UIButton* button, GMDChallengeStartGroup* pGroup )
{
	GenesisMissionStartDescription *pStartDesc;
	MapDescEditDoc* pDoc;
	int index;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pStartDesc = &pDoc->pMapDesc->missions[pDoc->iCurrentMission]->zoneDesc.startDescription;

	for (index = eaSize( &pGroup->eaContinuePromptBodyTextAddRemoveButtons ) - 1; index >= 0; --index ) {
		if( pGroup->eaContinuePromptBodyTextAddRemoveButtons[ index ] == button ) {
			break;
		}
	}
	if( index == -1 ) {
		return;
	}

	// Perform the operation
	StructFreeString( pStartDesc->eaContinuePromptBodyText[ index ]);
	eaRemove( &pStartDesc->eaContinuePromptBodyText, index );

	// Refresh the UI
	GMDMapDescChanged(pDoc, true );
}

static void GMDStartRoomFinished( UITextEntry* textEntry, GMDChallengeStartGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	const char* startRoomLayout = ui_TextEntryGetText( textEntry );
	GenesisMissionDescription* pMission;

	GMDEnsureMission(pDoc, &pMission, NULL);
	GMDSplitRoomLayoutEntry( startRoomLayout, &pMission->zoneDesc.startDescription.pcStartRoom, &pMission->zoneDesc.startDescription.pcStartLayout);

	GMDMapDescChanged(pDoc, true);
}

static void GMDExitRoomFinished( UITextEntry* textEntry, GMDChallengeStartGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	const char* startRoomLayout = ui_TextEntryGetText( textEntry );
	GenesisMissionDescription* pMission;

	GMDEnsureMission(pDoc, &pMission, NULL);
	GMDSplitRoomLayoutEntry( startRoomLayout, &pMission->zoneDesc.startDescription.pcExitRoom, &pMission->zoneDesc.startDescription.pcExitLayout);

	GMDMapDescChanged(pDoc, true);
}

static void GMDContinueRoomFinished( UITextEntry* textEntry, GMDChallengeStartGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	const char* startRoomLayout = ui_TextEntryGetText( textEntry );
	GenesisMissionDescription* pMission;

	GMDEnsureMission(pDoc, &pMission, NULL);
	GMDSplitRoomLayoutEntry( startRoomLayout, &pMission->zoneDesc.startDescription.pcContinueRoom, &pMission->zoneDesc.startDescription.pcContinueLayout);

	GMDMapDescChanged(pDoc, true);
}

static void GMDWhenRoomsFinished( UITextEntry* textEntry, GMDWhenGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	GenesisWhen* pWhen = pGroup->pWhen;
	const char* roomsText = ui_TextEntryGetText( textEntry );

	{
		char** eaRooms = NULL;
		int it;

		DivideString( roomsText, " ", &eaRooms,
					  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE );
		eaDestroyStruct( &pWhen->eaRooms, parse_GenesisWhenRoom );
		for( it = 0; it != eaSize( &eaRooms ); ++it ) {
			char* room = NULL;
			char* layout = NULL;
			GMDSplitRoomLayoutEntry( eaRooms[it], &room, &layout );

			if( room && layout ) {
				GenesisWhenRoom* whenRoom = StructCreate( parse_GenesisWhenRoom );
				whenRoom->roomName = room;
				whenRoom->layoutName = layout;

				eaPush( &pWhen->eaRooms, whenRoom );
			}
		}

		eaDestroyEx( &eaRooms, NULL );
	}

	GMDMapDescChanged(pDoc, true);
}

static void GMDRefreshChallengeStart(MapDescEditDoc *pDoc)
{
	UIExpander *pExpander = pDoc->pChallengeStartGroup->pExpander;
	GMDChallengeStartGroup *pGroup = pDoc->pChallengeStartGroup;
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	F32 y = 0;

	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Update entry from map name
	pGroup->pEntryFromMapLabel = GMDRefreshLabel(pGroup->pEntryFromMapLabel, "Entry From Map", "What map this can be entered from", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pEntryFromMapField) {
		pGroup->pEntryFromMapField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "EntryFromMapName", NULL, &g_GEMapDispNames, NULL);
		GMDAddFieldToParent(pGroup->pEntryFromMapField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pEntryFromMapField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pEntryFromMapField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
	}
	
	y += STANDARD_ROW_HEIGHT;
	
	// Update entry from interactable name
	pGroup->pEntryFromInteractableLabel = GMDRefreshLabel(pGroup->pEntryFromInteractableLabel, "Entry From Obj", "What interactable this can be entered from", X_OFFSET_BASE, 0, y, pExpander );
	if (!pGroup->pEntryFromInteractableField) {
		pGroup->pEntryFromInteractableField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "EntryFromInteractableName");
		GMDAddFieldToParent(pGroup->pEntryFromInteractableField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pEntryFromInteractableField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pEntryFromInteractableField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update start layout
	pGroup->pStartRoomLabel = GMDRefreshLabel(pGroup->pStartRoomLabel, "Starting Room", "The room the player starts in.", X_OFFSET_BASE, 0, y, pExpander);
	GMDRefreshTextEntryRoomLayoutPair( &pGroup->pStartRoomText, pDoc, GMDStartRoomFinished, pGroup,
									   UI_WIDGET( pExpander ), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5 );
	GMDTextEntrySetRoomLayout( pGroup->pStartRoomText,
							   pMission->zoneDesc.startDescription.pcStartRoom, pMission->zoneDesc.startDescription.pcStartLayout,
							   SAFE_MEMBER( pOrigMission, zoneDesc.startDescription.pcStartRoom ), SAFE_MEMBER( pOrigMission, zoneDesc.startDescription.pcStartLayout ));
	y += STANDARD_ROW_HEIGHT;

	if (pDoc->EditingMapType != GenesisMapType_SolarSystem) {
		// Update has door
		pGroup->pHasDoorLabel = GMDRefreshLabel(pGroup->pHasDoorLabel, "Has Entry Door", "If true, there is an entry door.  Otherwise entry arrives somewhere in the room.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pHasDoorField) {
			pGroup->pHasDoorField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "HasEntryDoor");
			GMDAddFieldToParent(pGroup->pHasDoorField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pHasDoorField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pHasDoorField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pHasDoorLabel);
		MEFieldSafeDestroy(&pGroup->pHasDoorField);
	}

	// Update the start transition
	pGroup->pStartTransitionOverrideLabel = GMDRefreshLabel(pGroup->pStartTransitionOverrideLabel, "Start Transition", "Specifies an override to the default region rules transition.  If left blank, the default transition as determined by the region rules will be seen.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pStartTransitionOverrideField) {
		pGroup->pStartTransitionOverrideField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "StartTransitionOverride", g_hDoorTransitionDict, "ResourceName");
		GMDAddFieldToParent(pGroup->pStartTransitionOverrideField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pStartTransitionOverrideField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pStartTransitionOverrideField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
	}
	y += STANDARD_ROW_HEIGHT;

	pGroup->pStartExitSeparator = GMDRefreshSeparator(pGroup->pStartExitSeparator, y, pExpander);
	y += SEPARATOR_HEIGHT;

	// Update the exit Transition
	pGroup->pExitTransitionOverrideLabel = GMDRefreshLabel(pGroup->pExitTransitionOverrideLabel, "Exit Transition", "Specifies an override to the default region rules Transition.  If left blank, the default Transition as determined by the region rules will be seen.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pExitTransitionOverrideField) {
		pGroup->pExitTransitionOverrideField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ExitTransitionOverride", g_hDoorTransitionDict, "ResourceName");
		GMDAddFieldToParent(pGroup->pExitTransitionOverrideField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pExitTransitionOverrideField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pExitTransitionOverrideField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
	}
	y += STANDARD_ROW_HEIGHT;
	
	if (pDoc->EditingMapType != GenesisMapType_SolarSystem) {
		// Update exit from
		pGroup->pExitFromLabel = GMDRefreshLabel(pGroup->pExitFromLabel, "Exit From", "Specifies where the player can exit.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pExitFromField) {
			pGroup->pExitFromField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ExitFrom", GenesisMissionExitFromEnum);
			GMDAddFieldToParent(pGroup->pExitFromField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pExitFromField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pExitFromField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pMission->zoneDesc.startDescription.eExitFrom == GenesisMissionExitFrom_DoorInRoom || pDoc->EditingMapType == GenesisMapType_Exterior) {
			// Update exit room
			pGroup->pExitRoomLabel = GMDRefreshLabel(pGroup->pExitRoomLabel, "Exit Room", "Specifies the room to exit from when applicable.", X_OFFSET_BASE, 0, y, pExpander);
			GMDRefreshTextEntryRoomLayoutPair( &pGroup->pExitRoomText, pDoc, GMDExitRoomFinished, pGroup,
											   UI_WIDGET( pExpander ), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5 );
			GMDTextEntrySetRoomLayout( pGroup->pExitRoomText,
									   pMission->zoneDesc.startDescription.pcExitRoom, pMission->zoneDesc.startDescription.pcExitLayout,
									   SAFE_MEMBER( pOrigMission, zoneDesc.startDescription.pcExitRoom ), SAFE_MEMBER( pOrigMission, zoneDesc.startDescription.pcExitLayout ));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pExitRoomLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pExitRoomText);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pExitFromLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pExitRoomLabel);
		MEFieldSafeDestroy(&pGroup->pExitFromField);
		ui_WidgetQueueFreeAndNull(&pGroup->pExitRoomText);
	}

	if (genesisMissionReturnIsAutogenerated(&pMission->zoneDesc, eaSize(&pDoc->pMapDesc->solar_system_layouts)>0)) {
		pGroup->pExitPromptSMF = GMDRefreshSMFView(pGroup->pExitPromptSMF, "An Exit prompt named \"MissionReturn\" will be automatically created.", NULL, X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);

		y += 2 * STANDARD_ROW_HEIGHT;

		pGroup->pExitUsePetCostumeLabel = GMDRefreshLabel(pGroup->pExitUsePetCostumeLabel, "Use Pet Costume", "If true, specify which pet to use, otherwise specify the costume to use.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
		if (!pGroup->pExitUsePetCostumeField) {
			pGroup->pExitUsePetCostumeField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.exitPromptCostume), &pMission->zoneDesc.startDescription.exitPromptCostume, parse_GenesisMissionCostume, "UsePetCostume");
			GMDAddFieldToParent(pGroup->pExitUsePetCostumeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pExitUsePetCostumeField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pExitUsePetCostumeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.exitPromptCostume), &pMission->zoneDesc.startDescription.exitPromptCostume);
		}
		y += STANDARD_ROW_HEIGHT;

		if (pMission->zoneDesc.startDescription.exitPromptCostume.eCostumeType == GenesisMissionCostumeType_PetCostume) {
			pGroup->pExitPetCostumeLabel = GMDRefreshLabel(pGroup->pExitPetCostumeLabel, "Pet Costume", "The pet costume for the exit prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pExitPetCostumeField) {
				pGroup->pExitPetCostumeField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.exitPromptCostume), &pMission->zoneDesc.startDescription.exitPromptCostume, parse_GenesisMissionCostume, "PetCostume", "PetContactList", "ResourceName");
				GMDAddFieldToParent(pGroup->pExitPetCostumeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pExitPetCostumeField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pExitPetCostumeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.exitPromptCostume), &pMission->zoneDesc.startDescription.exitPromptCostume);
			}

			ui_WidgetQueueFreeAndNull(&pGroup->pExitCostumeLabel);
			MEFieldSafeDestroy(&pGroup->pExitCostumeField);
		} else {
			pGroup->pExitCostumeLabel = GMDRefreshLabel(pGroup->pExitCostumeLabel, "Costume", "The costume for the exit prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pExitCostumeField) {
				pGroup->pExitCostumeField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.exitPromptCostume), &pMission->zoneDesc.startDescription.exitPromptCostume, parse_GenesisMissionCostume, "Costume", "PlayerCostume", "ResourceName");
				GMDAddFieldToParent(pGroup->pExitCostumeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pExitCostumeField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pExitCostumeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.exitPromptCostume), &pMission->zoneDesc.startDescription.exitPromptCostume);
			}

			ui_WidgetQueueFreeAndNull(&pGroup->pExitPetCostumeLabel);
			MEFieldSafeDestroy(&pGroup->pExitPetCostumeField);
		}
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pExitPromptSMF);
		ui_WidgetQueueFreeAndNull(&pGroup->pExitUsePetCostumeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pExitCostumeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pExitPetCostumeLabel);
		MEFieldSafeDestroy(&pGroup->pExitUsePetCostumeField);
		MEFieldSafeDestroy(&pGroup->pExitCostumeField);
		MEFieldSafeDestroy(&pGroup->pExitPetCostumeField);
	}

	pGroup->pExitContinueSeparator = GMDRefreshSeparator(pGroup->pExitContinueSeparator, y, pExpander);
	y += SEPARATOR_HEIGHT;

	pGroup->pContinueLabel = GMDRefreshLabel(pGroup->pContinueLabel, "Continue", "Specifies that the player can go somewhere when this mission is complete.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pContinueField) {
		pGroup->pContinueField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "Continue");
		GMDAddFieldToParent(pGroup->pContinueField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pContinueField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pContinueField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pMission->zoneDesc.startDescription.bContinue) {
		// Continue Transition
		pGroup->pContinueTransitionOverrideLabel = GMDRefreshLabel(pGroup->pContinueTransitionOverrideLabel, "Continue Transition", "Specifies an override to the default region rules Transition.  If left blank, the default Transition as determined by the region rules will be seen.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pContinueTransitionOverrideField) {
			pGroup->pContinueTransitionOverrideField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinueTransitionOverride", g_hDoorTransitionDict, "ResourceName");
			GMDAddFieldToParent(pGroup->pContinueTransitionOverrideField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pContinueTransitionOverrideField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pContinueTransitionOverrideField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
		}
		y += STANDARD_ROW_HEIGHT;
		
		pGroup->pContinueFromLabel = GMDRefreshLabel(pGroup->pContinueFromLabel, "Continue From", "Specifies where the player can continue.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pContinueFromField) {
			pGroup->pContinueFromField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinueFrom", GenesisMissionExitFromEnum);
			GMDAddFieldToParent(pGroup->pContinueFromField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pContinueFromField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pContinueFromField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pMission->zoneDesc.startDescription.eContinueFrom == GenesisMissionExitFrom_DoorInRoom) {
			pGroup->pContinueRoomLabel = GMDRefreshLabel(pGroup->pContinueRoomLabel, "Continue Room", "Specifies the room to continue from when applicable.", X_OFFSET_BASE, 0, y, pExpander);
			GMDRefreshTextEntryRoomLayoutPair( &pGroup->pContinueRoomText, pDoc, GMDContinueRoomFinished, pGroup,
											   UI_WIDGET( pExpander ), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5 );
			GMDTextEntrySetRoomLayout( pGroup->pContinueRoomText,
									   pMission->zoneDesc.startDescription.pcContinueRoom, pMission->zoneDesc.startDescription.pcContinueLayout,
									   SAFE_MEMBER( pOrigMission, zoneDesc.startDescription.pcContinueRoom ), SAFE_MEMBER( pOrigMission, zoneDesc.startDescription.pcContinueLayout ));
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContinueRoomLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinueRoomText);
		}

		if (pMission->zoneDesc.startDescription.eContinueFrom == GenesisMissionExitFrom_Challenge) {
			pGroup->pContinueChallengeLabel = GMDRefreshLabel(pGroup->pContinueChallengeLabel, "Continue Challenge", "Specifies the challenge to continue from when applicable.", X_OFFSET_BASE, 0, y, pExpander);
			if (!pGroup->pContinueChallengeField) {
				pGroup->pContinueChallengeField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinueChallenge", NULL, &pDoc->eaChallengeNames, NULL);
				GMDAddFieldToParent(pGroup->pContinueChallengeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pContinueChallengeField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pContinueChallengeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContinueChallengeLabel);
			MEFieldSafeDestroy(&pGroup->pContinueChallengeField);
		}

		if (genesisMissionContinueIsAutogenerated(&pMission->zoneDesc, eaSize(&pDoc->pMapDesc->solar_system_layouts)>0, pDoc->pMapDesc->exterior_layout != NULL)) {
			pGroup->pContinuePromptSMF = GMDRefreshSMFView(pGroup->pContinuePromptSMF, "A Continue prompt named \"MissionContinue\" will be automatically created.", NULL, X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);

			y += 2 * STANDARD_ROW_HEIGHT;
			
			pGroup->pContinuePromptUsePetCostumeLabel = GMDRefreshLabel(pGroup->pContinuePromptUsePetCostumeLabel, "Use Pet Costume", "If true, specify which pet to use, otherwise specify the costume to use.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pContinuePromptUsePetCostumeField) {
				pGroup->pContinuePromptUsePetCostumeField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.continuePromptCostume), &pMission->zoneDesc.startDescription.continuePromptCostume, parse_GenesisMissionCostume, "UsePetCostume");
				GMDAddFieldToParent(pGroup->pContinuePromptUsePetCostumeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pContinuePromptUsePetCostumeField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pContinuePromptUsePetCostumeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.continuePromptCostume), &pMission->zoneDesc.startDescription.continuePromptCostume);
			}
			y += STANDARD_ROW_HEIGHT;

			if (pMission->zoneDesc.startDescription.continuePromptCostume.eCostumeType == GenesisMissionCostumeType_PetCostume) {
				pGroup->pContinuePromptPetCostumeLabel = GMDRefreshLabel(pGroup->pContinuePromptPetCostumeLabel, "Pet Costume", "The pet whose costume to use for the continue prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
				if (!pGroup->pContinuePromptPetCostumeField) {
					pGroup->pContinuePromptPetCostumeField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.continuePromptCostume), &pMission->zoneDesc.startDescription.continuePromptCostume, parse_GenesisMissionCostume, "PetCostume", "PetContactList", "ResourceName");
					GMDAddFieldToParent(pGroup->pContinuePromptPetCostumeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
				} else {
					ui_WidgetSetPosition(pGroup->pContinuePromptPetCostumeField->pUIWidget, X_OFFSET_CONTROL, y);
					MEFieldSetAndRefreshFromData(pGroup->pContinuePromptPetCostumeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.continuePromptCostume), &pMission->zoneDesc.startDescription.continuePromptCostume);
				}

				ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptCostumeLabel);
				MEFieldSafeDestroy(&pGroup->pContinuePromptCostumeField);
			} else {
				pGroup->pContinuePromptCostumeLabel = GMDRefreshLabel(pGroup->pContinuePromptCostumeLabel, "Costume", "The costume for the continue prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
				if (!pGroup->pContinuePromptCostumeField) {
					pGroup->pContinuePromptCostumeField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.continuePromptCostume), &pMission->zoneDesc.startDescription.continuePromptCostume, parse_GenesisMissionCostume, "Costume", "PlayerCostume", "ResourceName");
					GMDAddFieldToParent(pGroup->pContinuePromptCostumeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
				} else {
					ui_WidgetSetPosition(pGroup->pContinuePromptCostumeField->pUIWidget, X_OFFSET_CONTROL, y);
					MEFieldSetAndRefreshFromData(pGroup->pContinuePromptCostumeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription.continuePromptCostume), &pMission->zoneDesc.startDescription.continuePromptCostume);
				}

				ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptPetCostumeLabel);
				MEFieldSafeDestroy(&pGroup->pContinuePromptPetCostumeField);
			}
			y += STANDARD_ROW_HEIGHT;
			
			pGroup->pContinuePromptButtonTextLabel = GMDRefreshLabel(pGroup->pContinuePromptButtonTextLabel, "Button Text", "Text shown on the button for the continue prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pContinuePromptButtonTextField) {
				pGroup->pContinuePromptButtonTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinuePromptButtonText");
				GMDAddFieldToParent(pGroup->pContinuePromptButtonTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pContinuePromptButtonTextField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pContinuePromptButtonTextField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
			}
			
			y += STANDARD_ROW_HEIGHT;
			
			pGroup->pContinuePromptCategoryLabel = GMDRefreshLabel(pGroup->pContinuePromptCategoryLabel, "Category", "What button category the continue prompt should be in.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pContinuePromptCategoryField) {
				pGroup->pContinuePromptCategoryField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinuePromptCategoryName", NULL, &geaOptionalActionCategories, NULL);
				GMDAddFieldToParent(pGroup->pContinuePromptCategoryField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pContinuePromptCategoryField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pContinuePromptCategoryField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
			}
			
			y += STANDARD_ROW_HEIGHT;
			
			pGroup->pContinuePromptPriorityLabel = GMDRefreshLabel(pGroup->pContinuePromptPriorityLabel, "Priority", "The priority for the continue prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pContinuePromptPriorityField) {
				pGroup->pContinuePromptPriorityField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinuePromptPriority", WorldOptionalActionPriorityEnum);
				GMDAddFieldToParent(pGroup->pContinuePromptPriorityField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pContinuePromptPriorityField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pContinuePromptPriorityField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
			}
			
			y += STANDARD_ROW_HEIGHT;
			
			pGroup->pContinuePromptTitleTextLabel = GMDRefreshLabel(pGroup->pContinuePromptTitleTextLabel, "Title Text", "The text for the title of the continue prompt window.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			if (!pGroup->pContinuePromptTitleTextField) {
				pGroup->pContinuePromptTitleTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinuePromptTitleText");
				GMDAddFieldToParent(pGroup->pContinuePromptTitleTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pContinuePromptTitleTextField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pContinuePromptTitleTextField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
			}
			
			y += STANDARD_ROW_HEIGHT;
			
			pGroup->pContinuePromptBodyTextLabel = GMDRefreshLabel(pGroup->pContinuePromptBodyTextLabel, "Body Text", "The text for the body of the continue prompt window.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pExpander);
			
			if( eaSize( &pMission->zoneDesc.startDescription.eaContinuePromptBodyText ) == 0 ) {
				eaPush( &pMission->zoneDesc.startDescription.eaContinuePromptBodyText, NULL );
			}
			GMDRefreshAddRemoveButtons(&pGroup->eaContinuePromptBodyTextAddRemoveButtons, eaSize(&pMission->zoneDesc.startDescription.eaContinuePromptBodyText), y,
									   UI_WIDGET(pGroup->pExpander), GMDContinuePromptBodyTextAdd, GMDContinuePromptBodyTextRemove, pGroup );
			y = GMDRefreshEArrayFieldSimple(&pGroup->eaContinuePromptBodyTextField, kMEFieldType_SMFTextEntry,
											SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription,
											X_OFFSET_CONTROL, y, 1.0, 5 + 16, UI_WIDGET(pGroup->pExpander), pDoc,
											parse_GenesisMissionStartDescription, "ContinuePromptBodyText");
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptSMF);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptUsePetCostumeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptCostumeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptPetCostumeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptButtonTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptCategoryLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptPriorityLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptTitleTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptBodyTextLabel);
			MEFieldSafeDestroy(&pGroup->pContinuePromptUsePetCostumeField);
			MEFieldSafeDestroy(&pGroup->pContinuePromptCostumeField);
			MEFieldSafeDestroy(&pGroup->pContinuePromptPetCostumeField);
			MEFieldSafeDestroy(&pGroup->pContinuePromptButtonTextField);
			MEFieldSafeDestroy(&pGroup->pContinuePromptCategoryField);
			MEFieldSafeDestroy(&pGroup->pContinuePromptPriorityField);
			MEFieldSafeDestroy(&pGroup->pContinuePromptTitleTextField);
			eaDestroyEx(&pGroup->eaContinuePromptBodyTextField, MEFieldDestroy);
			eaDestroyEx(&pGroup->eaContinuePromptBodyTextAddRemoveButtons, ui_WidgetQueueFree);
		}

		pGroup->pContinueMapLabel = GMDRefreshLabel(pGroup->pContinueMapLabel, "Next Map", "Specifies the map the player will continue to.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pContinueMapField) {
			pGroup->pContinueMapField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription, parse_GenesisMissionStartDescription, "ContinueMap", NULL, &g_GEMapDispNames, NULL);
			GMDAddFieldToParent(pGroup->pContinueMapField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pContinueMapField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pContinueMapField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.startDescription), &pMission->zoneDesc.startDescription);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pContinueTransitionOverrideLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinueFromLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinueRoomLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinueChallengeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinueMapLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptSMF);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptUsePetCostumeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptCostumeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptPetCostumeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptButtonTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptCategoryLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptPriorityLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptTitleTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinuePromptBodyTextLabel);
		MEFieldSafeDestroy(&pGroup->pContinueTransitionOverrideField);
		MEFieldSafeDestroy(&pGroup->pContinueFromField);
		ui_WidgetQueueFreeAndNull(&pGroup->pContinueRoomText);
		MEFieldSafeDestroy(&pGroup->pContinueChallengeField);
		MEFieldSafeDestroy(&pGroup->pContinueMapField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptUsePetCostumeField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptCostumeField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptPetCostumeField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptButtonTextField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptCategoryField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptPriorityField);
		MEFieldSafeDestroy(&pGroup->pContinuePromptTitleTextField);
		eaDestroyEx(&pGroup->eaContinuePromptBodyTextField, MEFieldDestroy);
		eaDestroyEx(&pGroup->eaContinuePromptBodyTextAddRemoveButtons, ui_WidgetQueueFree);
	}
	
	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static F32 GMDRefreshWhen(MapDescEditDoc *pDoc, GMDWhenGroup *pGroup, UIExpander* pParent, const char* labelText, const char* labelTooltip, F32 xIndent, F32 y, GenesisWhen *pOrigWhen, GenesisWhen *pWhen, bool isForMission)
{
	pGroup->pDoc = pDoc;
	pGroup->pWhen = pWhen;
	
	// Update type
	MEExpanderRefreshLabel(&pGroup->pTypeLabel, labelText, labelTooltip, X_OFFSET_BASE + xIndent, 0, y, UI_WIDGET(pParent));
	MEExpanderRefreshEnumField(&pGroup->pTypeField, pOrigWhen, pWhen, parse_GenesisWhen, "whenType", GenesisWhenTypeEnum,
							   UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
							   GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	if( isForMission ) {
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_MapStart);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_Manual);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_MissionComplete);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_MissionNotInProgress);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_ObjectiveComplete);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_ObjectiveCompleteAll);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_ObjectiveInProgress);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_ChallengeAdvance);
	} else {
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_AllOf);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_InOrder);
		ui_ComboBoxEnumRemoveValueInt((UIComboBox*)pGroup->pTypeField->pUIWidget, GenesisWhen_Branch);
	}
	// This second (redundant) call is needed in case the combo box
	// was just created.  It seems the combo box remembers what index
	// was selected, and removing the values just above makes it
	// highlight the wrong entry.
	MEExpanderRefreshEnumField(&pGroup->pTypeField, pOrigWhen, pWhen, parse_GenesisWhen, "whenType", GenesisWhenTypeEnum,
							   UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
							   GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

	y += STANDARD_ROW_HEIGHT;

	if( pWhen->type == GenesisWhen_ObjectiveComplete || pWhen->type == GenesisWhen_ObjectiveCompleteAll ||
		pWhen->type == GenesisWhen_ObjectiveInProgress ) {
		// Update objective names
		if( pWhen->type == GenesisWhen_ObjectiveCompleteAll ) {
			MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Objectives", "When all of the objectives specified are complete", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		} else if( pWhen->type == GenesisWhen_ObjectiveComplete ) {
			MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Objectives", "When any of the objectives specified are complete", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		} else {
			MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Objectives", "When any of the objectives specified are in progress", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));	
		}
		MEExpanderRefreshDataField( &pGroup->pObjectiveNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenObjectiveName", &pDoc->eaObjectiveNames, true,
									UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
									GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy( &pGroup->pObjectiveNamesField );
	}
	if( pWhen->type == GenesisWhen_PromptComplete ) {
		// Update prompt names
		MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Prompts", "When any of the prompts specified are complete", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshDataField( &pGroup->pPromptNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenPromptName", &pDoc->eaPromptNames, true,
									UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
									GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy( &pGroup->pPromptNamesField );
	}
	if( pWhen->type == GenesisWhen_ContactComplete ) {
		// Update contact names
		MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Contacts", "When any of the contacts specified are complete", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pPromptNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenContactName", "ContactDef",
												UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy( &pGroup->pContactNamesField );
	}
	if( pWhen->type == GenesisWhen_ChallengeComplete || pWhen->type == GenesisWhen_ChallengeAdvance ) {
		// Update challenge names
		MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Challenges", "When all of the challenges specified are complete", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshDataField( &pGroup->pChallengeNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenChallengeName", &pDoc->eaChallengeNames, true,
									UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
									GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy( &pGroup->pChallengeNamesField );
	}
	if( pWhen->type == GenesisWhen_ChallengeComplete ) {
		// Update challenge num to complete
		MEExpanderRefreshLabel( &pGroup->pChallengeNumToCompleteLabel, "Num to Complete", "If 0, all of them, otherwise complete this many challenges", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshSimpleField( &pGroup->pChallengeNumToCompleteField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenChallengeNumToComplete", kMEFieldType_TextEntry,
									  UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT; 
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pChallengeNumToCompleteLabel );
		MEFieldSafeDestroy( &pGroup->pChallengeNumToCompleteField );
	}
	if( pWhen->type == GenesisWhen_RoomEntry || pWhen->type == GenesisWhen_RoomEntryAll ) {
		// Update challenge names
		MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Rooms", "When any of the rooms specified are entered", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		GMDRefreshTextEntryRoomLayoutPair( &pGroup->pRoomsText, pDoc, GMDWhenRoomsFinished, pGroup,
										   UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5 );
		ui_ComboBoxSetMultiSelect( pGroup->pRoomsText->cb, true );
		GMDTextEntrySetWhenRoomLayouts( pGroup->pRoomsText, pWhen->eaRooms, SAFE_MEMBER( pOrigWhen, eaRooms ));
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pRoomsText );
	}
	if( pWhen->type == GenesisWhen_CritterKill ) {
		// Update critter names
		MEExpanderRefreshLabel( &pGroup->pNamesLabel, "CritterDefs", "Which CritterDefs being killed count toward the evnt.", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pCritterDefNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenCritterDefName", "CritterDef",
												UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		// Update critter group names
		MEExpanderRefreshLabel( &pGroup->pCritterGroupNamesLabel, "CritterGroups", "Which CritterGroups being killed count toward the event.", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent) );
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pCritterGroupNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenCritterGroupName", "CritterGroup",
												UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		// Update num to kill
		MEExpanderRefreshLabel( &pGroup->pCritterNumToKillLabel, "Count", "How many critters using the above defs and groups need to be killed", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent) );
		MEExpanderRefreshSimpleField( &pGroup->pCritterNumToKillField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenCritterNumToComplete", kMEFieldType_TextEntry,
									  UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy( &pGroup->pCritterDefNamesField );
		ui_WidgetQueueFreeAndNull( &pGroup->pCritterGroupNamesLabel );
		MEFieldSafeDestroy( &pGroup->pCritterGroupNamesField );
		ui_WidgetQueueFreeAndNull( &pGroup->pCritterNumToKillLabel );
		MEFieldSafeDestroy( &pGroup->pCritterNumToKillField );
	}
	if( pWhen->type == GenesisWhen_ItemCount ) {
		// Update critter names
		MEExpanderRefreshLabel( &pGroup->pNamesLabel, "Items", "Which ItemDefs the player must collect.", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent));
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pItemDefNamesField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenItemDefName", "ItemDef",
												UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		// Update num to collect
		MEExpanderRefreshLabel( &pGroup->pItemCountLabel, "Count", "How many items a player needs to have in his inventory", X_OFFSET_BASE + xIndent + X_OFFSET_INDENT, 0, y, UI_WIDGET(pParent) );
		MEExpanderRefreshSimpleField( &pGroup->pItemCountField, pOrigWhen, pWhen, parse_GenesisWhen, "WhenItemCount", kMEFieldType_TextEntry,
									  UI_WIDGET(pParent), X_OFFSET_CONTROL + xIndent, y, 0, 1.0, UIUnitPercentage, 5,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		MEFieldSafeDestroy( &pGroup->pItemDefNamesField );
		ui_WidgetQueueFreeAndNull( &pGroup->pItemCountLabel );
		MEFieldSafeDestroy( &pGroup->pItemCountField );
	}

	if(   !pGroup->pRoomsText && !pGroup->pChallengeNamesField && !pGroup->pObjectiveNamesField
		  && !pGroup->pPromptNamesField && !pGroup->pContactNamesField && !pGroup->pCritterDefNamesField
		  && !pGroup->pItemDefNamesField ) {
		ui_WidgetQueueFreeAndNull( &pGroup->pNamesLabel );
	}

	return y;
}

static void MDEExpanderDrawTinted( UIExpander* expander, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( expander );
	display_sprite( white_tex_atlas, x, y, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0xFFC21430 );
	ui_ExpanderDraw( expander, UI_PARENT_VALUES );
}

static void GMDChallengeMakeShared( UIButton* pButton, GMDChallengeGroup *pGroup )
{
	MapDescEditDoc *pDoc = pGroup->pDoc;
	GenesisMissionChallenge* pChallenge = (*pGroup->peaChallenges)[pGroup->index];

	// Remove the challenge from anywhere it might be
	//eaFindAndRemove(&pDoc->eaSharedChallengeGroups, pGroup);
	//eaFindAndRemove(&pDoc->eaChallengeGroups, pGroup);
	eaRemove(pGroup->peaChallenges, pGroup->index);

	// Add it to the shared group
	//eaPush(&pDoc->eaSharedChallengeGroups, pGroup);
	eaPush(&pDoc->pMapDesc->shared_challenges, pChallenge); 
	
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDChallengeMakeLocal( UIButton* pButton, GMDChallengeGroup *pGroup )
{
	MapDescEditDoc *pDoc = pGroup->pDoc;
	GenesisMissionChallenge* pChallenge = (*pGroup->peaChallenges)[pGroup->index];
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;

	GMDEnsureMission(pDoc, &pMission, &pOrigMission);
	
	// Remove the challenge from anywhere it might be
	//eaFindAndRemove(&pDoc->eaSharedChallengeGroups, pGroup);
	//eaFindAndRemove(&pDoc->eaChallengeGroups, pGroup);
	eaRemove(pGroup->peaChallenges, pGroup->index);

	// Add it to the mission group
	//eaPush(&pDoc->eaChallengeGroups, pGroup);
	eaPush(&pMission->eaChallenges, pChallenge); 
	
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveChallengeClone( void* ignored1, GMDChallengeGroup* pGroup )
{
	GenesisMissionChallenge *pNewChallenge;
	GenesisMissionChallenge *pChallenge;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pChallenge = (*pGroup->peaChallenges)[pGroup->index];

	// Perform the operation
	pNewChallenge = StructClone( parse_GenesisMissionChallenge, pChallenge );
	GMDChallengeUniquifyName( *pGroup->peaChallenges, pNewChallenge );
	eaInsert( pGroup->peaChallenges, pNewChallenge, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveChallengeCut( void* ignored1, GMDChallengeGroup* pGroup )
{
	GenesisMissionChallenge *pChallenge;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pChallenge = (*pGroup->peaChallenges)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionChallenge, pChallenge, &g_GMDClipboardChallenge );
	StructDestroy( parse_GenesisMissionChallenge, pChallenge );
	eaRemove( pGroup->peaChallenges, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveChallengeCopy( void* ignored1, GMDChallengeGroup* pGroup )
{
	GenesisMissionChallenge *pChallenge;

	if( !pGroup ) {
		return;
	}
	pChallenge = (*pGroup->peaChallenges)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionChallenge, pChallenge, &g_GMDClipboardChallenge );
}

static void GMDActiveChallengePaste( void* ignored1, GMDChallengeGroup* pGroup )
{
	GenesisMissionChallenge *pChallenge;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pChallenge = (*pGroup->peaChallenges)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_GenesisMissionChallenge, &g_GMDClipboardChallenge, pChallenge );
	if(pChallenge->pcLayoutName)
		StructFreeString(pChallenge->pcLayoutName);
	pChallenge->pcLayoutName = StructAllocString(GMDGetActiveLayoutName(pDoc));

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static GenesisMissionChallenge* GMDGetNextChallenge(GenesisMissionChallenge **ppChallenges, const char *pcLayoutName, int *idx, bool backwards)
{
	if(!ppChallenges)
		return NULL;

	if( backwards ) {
		for ( (*idx)--; (*idx) >= 0; (*idx)-- ) {
			GenesisMissionChallenge *pChallenge = ppChallenges[(*idx)];
			if(stricmp_safe(pcLayoutName, pChallenge->pcLayoutName) == 0) {
				return pChallenge;
			}
		}
	} else {
		for ( (*idx)++; (*idx) < eaSize(&ppChallenges); (*idx)++ ) {
			GenesisMissionChallenge *pChallenge = ppChallenges[(*idx)];
			if(stricmp_safe(pcLayoutName, pChallenge->pcLayoutName) == 0) {
				return pChallenge;
			}
		}
	}
	
	return NULL;
}

static bool GMDHasNextChallenge(GenesisMissionChallenge **ppChallenges, const char *pcLayoutName, int idx, bool backwards)
{
	return GMDGetNextChallenge(ppChallenges, pcLayoutName, &idx, backwards) != NULL;
}

static void GMDRefreshChallenge(MapDescEditDoc *pDoc, GMDChallengeGroup *pGroup, int index, GenesisMissionChallenge ***peaChallenges, GenesisMissionChallenge *pOrigChallenge, GenesisMissionChallenge *pChallenge, bool bIsShared)
{
	static StaticDefineInt *GenesisChallengeTypeEnumTruncated = NULL;
	char buf[256];
	F32 y = 0;

	if (!GenesisChallengeTypeEnumTruncated)
	{
		int length = 0, i, p = 0;
		do
		{
			length++;
		}
		while (GenesisChallengeTypeEnum[length].key != DM_END);
		length++;
		GenesisChallengeTypeEnumTruncated = calloc(1, length*sizeof(StaticDefineInt));
		for (i = 0; i < length; i++)
		{
			if (GenesisChallengeTypeEnum[i].value != GenesisChallenge_Contact)
				GenesisChallengeTypeEnumTruncated[p++] = GenesisChallengeTypeEnum[i];
		}
	}

	// Refresh the group
	pGroup->peaChallenges = peaChallenges;

	// Update expander
	sprintf(buf, "%s: %s", (bIsShared ? "Shared Challenge" : "Challenge"),
			pChallenge->pcName);
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);
	if (bIsShared) {
		pGroup->pExpander->widget.drawF = MDEExpanderDrawTinted;
	} else {
		pGroup->pExpander->widget.drawF = ui_ExpanderDraw;
	}

	// Add toggle buttons
	if (!bIsShared)
		GMDRefreshButton(pGroup->pExpander, 0, y, 100, "Make Shared", &pGroup->pToggleSharedButton, GMDChallengeMakeShared, pGroup );
	else
		GMDRefreshButton(pGroup->pExpander, 0, y, 100, "Make Specific", &pGroup->pToggleSharedButton, GMDChallengeMakeLocal, pGroup );

	y += STANDARD_ROW_HEIGHT;

	// Add popup menu
	if( !pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpChallenge, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownChallenge, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemoveChallenge, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActiveChallengeClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActiveChallengeCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActiveChallengeCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActiveChallengePaste, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	} 
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the challenge.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage,
							4, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigChallenge, pChallenge);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update rooms
	pGroup->pRoomLabel = GMDRefreshLabel(pGroup->pRoomLabel, "Locations", "The rooms and paths the challenge may be placed in.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pRoomField) {
		pGroup->pRoomField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "RoomName", NULL, &pDoc->eaChallengeLocNames, NULL);
		GMDAddFieldToParent(pGroup->pRoomField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pRoomField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRoomField, pOrigChallenge, pChallenge);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update challenge type
	pGroup->pChallengeTypeLabel = GMDRefreshLabel(pGroup->pChallengeTypeLabel, "Challenge Type", "The type of challenge.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pChallengeTypeField) {
		pGroup->pChallengeTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "Type", GenesisChallengeTypeEnumTruncated);
		GMDAddFieldToParent(pGroup->pChallengeTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pChallengeTypeField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pChallengeTypeField, pOrigChallenge, pChallenge);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update challenge specifier
	pGroup->pChallengeSpecLabel = GMDRefreshLabel(pGroup->pChallengeSpecLabel, "Challenge", "Determines how the Challenge will be chosen.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pChallengeSpecField) {
		pGroup->pChallengeSpecField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "ChallengeSpecifier", GenesisTagOrNameEnum);
		GMDAddFieldToParent(pGroup->pChallengeSpecField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pChallengeSpecField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pChallengeSpecField, pOrigChallenge, pChallenge);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pChallenge->eSpecifier == GenesisTagOrName_RandomByTag) {
		static char** append_tags = NULL;
		if( !append_tags ) {
			eaPush( &append_tags, "genesischallenge" );
		}
		
		// Update challenge tags
		GMDRefreshTagsSpecifier( &pGroup->pChallengeTagsLabel, &pGroup->pChallengeTagsField, &pGroup->pChallengeTagsErrorPane,
								 pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "ChallengeTags2",
								 NULL, OBJECT_LIBRARY_DICT, &gChallengeTags, append_tags,
								 X_OFFSET_BASE + X_OFFSET_INDENT, y, pGroup->pExpander, pDoc );
		y += STANDARD_ROW_HEIGHT;

		// Update challenge heterogenous flag
		pGroup->pChallengeHeterogenousLabel = GMDRefreshLabel(pGroup->pChallengeHeterogenousLabel, "Vary it up!", "The Challenge will be different for each placed challenge.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pChallengeHeterogenousField) {
			pGroup->pChallengeHeterogenousField = MEFieldCreateSimple(kMEFieldType_Check, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "HeterogenousObjects");
			GMDAddFieldToParent(pGroup->pChallengeHeterogenousField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pChallengeHeterogenousField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pChallengeHeterogenousField, pOrigChallenge, pChallenge);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pChallengeTagsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pChallengeTagsErrorPane);
		ui_WidgetQueueFreeAndNull(&pGroup->pChallengeHeterogenousLabel);
		MEFieldSafeDestroy(&pGroup->pChallengeTagsField);
		MEFieldSafeDestroy(&pGroup->pChallengeHeterogenousField);
	}

	if (pChallenge->eSpecifier == GenesisTagOrName_SpecificByName) {
		// Update challenge name
		pGroup->pChallengeNameLabel = GMDRefreshLabel(pGroup->pChallengeNameLabel, "Name", "The Challenge will be the one named.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pChallengeNameField) {
			pGroup->pChallengeNameField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "SpecificChallenge", NULL, &gChallengeNames, NULL);
			GMDAddFieldToParent(pGroup->pChallengeNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pChallengeNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pChallengeNameField, pOrigChallenge, pChallenge);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pChallengeNameLabel);
		MEFieldSafeDestroy(&pGroup->pChallengeNameField);
	}

	// Update count
	pGroup->pCountLabel = GMDRefreshLabel(pGroup->pCountLabel, "Num to Place", "The number of this challenge to place.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pCountField) {
		pGroup->pCountField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "Count");
		GMDAddFieldToParent(pGroup->pCountField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pCountField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCountField, pOrigChallenge, pChallenge);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update num to spawn
	pGroup->pNumSpawnLabel = GMDRefreshLabel(pGroup->pNumSpawnLabel, "Num to Spawn", "The number of this challenge to spawn.  Zero mean to spawn all.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNumSpawnField) {
		pGroup->pNumSpawnField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "NumToSpawn");
		GMDAddFieldToParent(pGroup->pNumSpawnField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNumSpawnField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNumSpawnField, pOrigChallenge, pChallenge);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update Spawn When Challenge
	if (!pGroup->pWhenGroup) {
		pGroup->pWhenGroup = calloc( 1, sizeof( *pGroup->pWhenGroup ));
	}
	y = GMDRefreshWhen(pDoc, pGroup->pWhenGroup, pGroup->pExpander,
					   "Spawn When", "When this is true, the challenge will spawn.", 0, y, SAFE_MEMBER_ADDR(pOrigChallenge, spawnWhen), &pChallenge->spawnWhen, false);
	
	if (pChallenge->eType == GenesisChallenge_Encounter || pChallenge->eType == GenesisChallenge_Encounter2) {
		GenesisMissionChallengeEncounter* pEncounter;
		GenesisMissionChallengeEncounter* pOrigEncounter;
		
		if (!pChallenge->pEncounter) {
			pChallenge->pEncounter = StructCreate( parse_GenesisMissionChallengeEncounter );
		}
		
		pEncounter = pChallenge->pEncounter;
		pOrigEncounter = SAFE_MEMBER(pOrigChallenge, pEncounter);
	
		if(pDoc->EditingMapType == GenesisMapType_SolarSystem) {
			// Update Spawn When Challenge
			pGroup->pSpacePatrolTypeLabel = GMDRefreshLabel(pGroup->pSpacePatrolTypeLabel, "Patrol", "What kind of patrol to use.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			if (!pGroup->pSpacePatrolTypeField) {
				pGroup->pSpacePatrolTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "SpacePatrolType", GenesisSpacePatrolTypeEnum);
				GMDAddFieldToParent(pGroup->pSpacePatrolTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pSpacePatrolTypeField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pSpacePatrolTypeField, pOrigEncounter, pEncounter);
			}

			y += STANDARD_ROW_HEIGHT;
		
			if (pEncounter->eSpacePatrolType == GENESIS_SPACE_PATROL_Path || pEncounter->eSpacePatrolType == GENESIS_SPACE_PATROL_Path_OneWay)
			{
				// Update "patrol to" challenge name
				pGroup->pSpacePatRoomRefLabel = GMDRefreshLabel(pGroup->pSpacePatRoomRefLabel, "Pat. Room", "The room to patrol to.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
				if (!pGroup->pSpacePatRoomRefField) {
					pGroup->pSpacePatRoomRefField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "SpacePatRefChallengeName", NULL, &pDoc->eaRoomNames, NULL);
					GMDAddFieldToParent(pGroup->pSpacePatRoomRefField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
				} else {
					ui_WidgetSetPosition(pGroup->pSpacePatRoomRefField->pUIWidget, X_OFFSET_CONTROL, y);
					MEFieldSetAndRefreshFromData(pGroup->pSpacePatRoomRefField, pOrigEncounter, pEncounter);
				}

				y += STANDARD_ROW_HEIGHT;
			}
			else
			{
				ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatRoomRefLabel);
				MEFieldSafeDestroy(&pGroup->pSpacePatRoomRefField);
			}
		
			if (pEncounter->eSpacePatrolType == GENESIS_SPACE_PATROL_Orbit)
			{
				// Update "patrol to" challenge name
				pGroup->pSpacePatChallengeRefLabel = GMDRefreshLabel(pGroup->pSpacePatChallengeRefLabel, "Pat. Challenge", "The Challenge to patrol around.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
				if (!pGroup->pSpacePatChallengeRefField) {
					pGroup->pSpacePatChallengeRefField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "SpacePatRefChallengeName", NULL, &pDoc->eaChallengeNames, NULL);
					GMDAddFieldToParent(pGroup->pSpacePatChallengeRefField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
				} else {
					ui_WidgetSetPosition(pGroup->pSpacePatChallengeRefField->pUIWidget, X_OFFSET_CONTROL, y);
					MEFieldSetAndRefreshFromData(pGroup->pSpacePatChallengeRefField, pOrigEncounter, pEncounter);
				}

				y += STANDARD_ROW_HEIGHT;
			}
			else
			{
				ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatChallengeRefLabel);
				MEFieldSafeDestroy(&pGroup->pSpacePatChallengeRefField);
			}
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatrolTypeLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatRoomRefLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatChallengeRefLabel);
			MEFieldSafeDestroy(&pGroup->pSpacePatrolTypeField);
			MEFieldSafeDestroy(&pGroup->pSpacePatRoomRefField);
			MEFieldSafeDestroy(&pGroup->pSpacePatChallengeRefField);
		}

		if(pDoc->EditingMapType != GenesisMapType_SolarSystem) {
			// Update Spawn When Challenge
			pGroup->pPatrolTypeLabel = GMDRefreshLabel(pGroup->pPatrolTypeLabel, "Patrol", "What kind of patrol to use.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			if (!pGroup->pPatrolTypeField) {
				pGroup->pPatrolTypeField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "PatrolType", GenesisPatrolTypeEnum);
				GMDAddFieldToParent(pGroup->pPatrolTypeField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pPatrolTypeField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pPatrolTypeField, pOrigEncounter, pEncounter);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pPatrolTypeLabel);
			MEFieldSafeDestroy(&pGroup->pPatrolTypeField);
		}

		if(	pDoc->EditingMapType != GenesisMapType_SolarSystem &&
			pEncounter->ePatrolType == GENESIS_PATROL_Path || pEncounter->ePatrolType == GENESIS_PATROL_Path_OneWay
			|| pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom || pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom_OneWay) {

			if (pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom || pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom_OneWay) {
				pGroup->pPatOtherRoomLabel = GMDRefreshLabel(pGroup->pPatOtherRoomLabel, "Other Room", "What room the encounter patrols to.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
				MEExpanderRefreshDataField(&pGroup->pPatOtherRoomField, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "PatOtherRoom",
										   &pDoc->eaRoomAndPathAndOrbitNames, false, UI_WIDGET(pGroup->pExpander),
										   X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
										   GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc);

				y += STANDARD_ROW_HEIGHT;
			} else {
				ui_WidgetQueueFreeAndNull( &pGroup->pPatOtherRoomLabel );
				MEFieldSafeDestroy( &pGroup->pPatOtherRoomField );
			}

			// Update "patrol to" placement
			pGroup->pPatPlacementLabel = GMDRefreshLabel(pGroup->pPatPlacementLabel, "Patrol To", "Where in the room to place the encounter patrols to.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pPatPlacementField) {
				pGroup->pPatPlacementField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "PatPlacement", GenesisChallengePlacementEnum);
				GMDAddFieldToParent(pGroup->pPatPlacementField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pPatPlacementField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pPatPlacementField, pOrigEncounter, pEncounter);
			}

			y += STANDARD_ROW_HEIGHT;

			if (pEncounter->ePatPlacement == GenesisChallengePlace_Near_Challenge)
			{
				// Update "patrol to" challenge name
				pGroup->pPatChallengeRefLabel = GMDRefreshLabel(pGroup->pPatChallengeRefLabel, "Pat. Challenge", "The challenge to patrol to.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
				if (!pGroup->pPatChallengeRefField) {
					pGroup->pPatChallengeRefField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigEncounter, pEncounter, parse_GenesisMissionChallengeEncounter, "PatRefChallengeName", NULL, &pDoc->eaChallengeNames, NULL);
					GMDAddFieldToParent(pGroup->pPatChallengeRefField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
				} else {
					ui_WidgetSetPosition(pGroup->pPatChallengeRefField->pUIWidget, X_OFFSET_CONTROL, y);
					MEFieldSetAndRefreshFromData(pGroup->pPatChallengeRefField, pOrigEncounter, pEncounter);
				}

				y += STANDARD_ROW_HEIGHT;
			}
			else
			{
				ui_WidgetQueueFreeAndNull(&pGroup->pPatChallengeRefLabel);
				MEFieldSafeDestroy(&pGroup->pPatChallengeRefField);
			}
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pPatOtherRoomLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pPatPlacementLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pPatChallengeRefLabel);
			MEFieldSafeDestroy(&pGroup->pPatOtherRoomField);
			MEFieldSafeDestroy(&pGroup->pPatPlacementField);
			MEFieldSafeDestroy(&pGroup->pPatChallengeRefField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatrolTypeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatRoomRefLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pSpacePatChallengeRefLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPatrolTypeLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPatOtherRoomLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPatPlacementLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPatChallengeRefLabel);
			
		MEFieldSafeDestroy(&pGroup->pSpacePatrolTypeField);
		MEFieldSafeDestroy(&pGroup->pSpacePatRoomRefField);
		MEFieldSafeDestroy(&pGroup->pSpacePatChallengeRefField);
		MEFieldSafeDestroy(&pGroup->pPatrolTypeField);
		MEFieldSafeDestroy(&pGroup->pPatOtherRoomField);
		MEFieldSafeDestroy(&pGroup->pPatPlacementField);
		MEFieldSafeDestroy(&pGroup->pPatChallengeRefField);
	}

	if (pChallenge->eType == GenesisChallenge_Clickie) {
		GenesisMissionChallengeClickie* pClickie;
		GenesisMissionChallengeClickie* pOrigClickie;
		
		if (!pChallenge->pClickie) {
			pChallenge->pClickie = StructCreate( parse_GenesisMissionChallengeClickie );
		}
		
		pClickie = pChallenge->pClickie;
		pOrigClickie = SAFE_MEMBER(pOrigChallenge, pClickie);
		
		pGroup->pClickieInteractionDefLabel = GMDRefreshLabel(pGroup->pClickieInteractionDefLabel, "Interaction Def", "Behavior definition of the interaction.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pClickieInteractionDefField) {
			pGroup->pClickieInteractionDefField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigClickie, pClickie, parse_GenesisMissionChallengeClickie, "InteractionDef", "InteractionDef", "ResourceName");
			GMDAddFieldToParent(pGroup->pClickieInteractionDefField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pClickieInteractionDefField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pClickieInteractionDefField, pOrigClickie, pClickie);
		}
		y += STANDARD_ROW_HEIGHT;

		if (IS_HANDLE_ACTIVE(pClickie->hInteractionDef)) {
			pGroup->pClickieInteractTextLabel = GMDRefreshLabel(pGroup->pClickieInteractTextLabel, "Interact Text", "Text displayed on the button that starts interaction.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pClickieInteractTextField) {
				pGroup->pClickieInteractTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigClickie, pClickie, parse_GenesisMissionChallengeClickie, "InteractText");
				GMDAddFieldToParent(pGroup->pClickieInteractTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pClickieInteractTextField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pClickieInteractTextField, pOrigClickie, pClickie);
			}
			y += STANDARD_ROW_HEIGHT;
		
			pGroup->pClickieSuccessTextLabel = GMDRefreshLabel(pGroup->pClickieSuccessTextLabel, "Success Text", "Text displayed when the interaction succeeds.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pClickieSuccessTextField) {
				pGroup->pClickieSuccessTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigClickie, pClickie, parse_GenesisMissionChallengeClickie, "SuccessText");
				GMDAddFieldToParent(pGroup->pClickieSuccessTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pClickieSuccessTextField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pClickieSuccessTextField, pOrigClickie, pClickie);
			}
			y += STANDARD_ROW_HEIGHT;
		
			pGroup->pClickieFailureTextLabel = GMDRefreshLabel(pGroup->pClickieFailureTextLabel, "Failure Text", "Text displayed when the interaction fails.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pClickieFailureTextField) {
				pGroup->pClickieFailureTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigClickie, pClickie, parse_GenesisMissionChallengeClickie, "FailureText");
				GMDAddFieldToParent(pGroup->pClickieFailureTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pClickieFailureTextField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pClickieFailureTextField, pOrigClickie, pClickie);
			}
			y += STANDARD_ROW_HEIGHT;
		
			pGroup->pClickieInteractAnimLabel = GMDRefreshLabel(pGroup->pClickieInteractAnimLabel, "Interact Anim", "The animation played while interacting with this.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pClickieInteractAnimField) {
				pGroup->pClickieInteractAnimField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigClickie, pClickie, parse_GenesisMissionChallengeClickie, "InteractAnim", "AIAnimList", "ResourceName");
				GMDAddFieldToParent(pGroup->pClickieInteractAnimField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pClickieInteractAnimField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pClickieInteractAnimField, pOrigClickie, pClickie);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pClickieInteractTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pClickieSuccessTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pClickieFailureTextLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pClickieInteractAnimLabel);

			MEFieldSafeDestroy(&pGroup->pClickieInteractTextField);
			MEFieldSafeDestroy(&pGroup->pClickieSuccessTextField);
			MEFieldSafeDestroy(&pGroup->pClickieFailureTextField);
			MEFieldSafeDestroy(&pGroup->pClickieInteractAnimField);
		}
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pClickieInteractionDefLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pClickieInteractTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pClickieSuccessTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pClickieFailureTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pClickieInteractAnimLabel);

		MEFieldSafeDestroy(&pGroup->pClickieInteractionDefField);
		MEFieldSafeDestroy(&pGroup->pClickieInteractTextField);
		MEFieldSafeDestroy(&pGroup->pClickieSuccessTextField);
		MEFieldSafeDestroy(&pGroup->pClickieFailureTextField);
		MEFieldSafeDestroy(&pGroup->pClickieInteractAnimField);
	}

	if (pDoc->EditingMapType != GenesisMapType_SolarSystem) {

		// Update challenge placement
		pGroup->pPlacementLabel = GMDRefreshLabel(pGroup->pPlacementLabel, "Placement", "Where in the room to place this challenge.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		if (!pGroup->pPlacementField) {
			pGroup->pPlacementField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "Placement", GenesisChallengePlacementEnum);
			GMDAddFieldToParent(pGroup->pPlacementField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pPlacementField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pPlacementField, pOrigChallenge, pChallenge);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pChallenge->ePlacement == GenesisChallengePlace_Prefab_Location) {
			// Update names of prefab locations
			if( eaSize( &pChallenge->eaRoomNames ) != 1 ) {
				eaDestroy( &pGroup->eaPrefabLocations );
			} else {
				GenesisLayoutRoom* room = genesisFindRoom( pGroup->pDoc->pMapDesc, pChallenge->eaRoomNames[ 0 ], pChallenge->pcLayoutName);

				eaDestroy( &pGroup->eaPrefabLocations );
				if( room ) {
					GenesisRoomDef* roomDef = GET_REF(room->room);
					if( roomDef && roomDef->library_piece ) {
						GroupDef* groupDef = objectLibraryGetGroupDefByName( roomDef->library_piece, false );
						if( groupDef ) {
							FOR_EACH_IN_STASHTABLE2( groupDef->name_to_path, elem ) {
								const char* key = stashElementGetKey( elem );
								if( key ) {
									eaPush( &pGroup->eaPrefabLocations, key );
								}
							} FOR_EACH_END;
						}
					}
				}
			}
			
			// Update prefab location
			MEExpanderRefreshLabel(&pGroup->pPlacementPrefabLocationLabel, "Location", NULL, X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshDataField(&pGroup->pPlacementPrefabLocationField, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "RefPrefabLocation", &pGroup->eaPrefabLocations, false,
									   UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
									   GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
			y += STANDARD_ROW_HEIGHT;

			ui_WidgetQueueFreeAndNull(&pGroup->pFacingLabel);
			MEFieldSafeDestroy(&pGroup->pFacingField);

			ui_WidgetQueueFreeAndNull(&pGroup->pRotationIncrementLabel);
			MEFieldSafeDestroy(&pGroup->pRotationIncrementField);
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pPlacementPrefabLocationLabel);
			MEFieldSafeDestroy(&pGroup->pPlacementPrefabLocationField);
		
			// Update challenge facing
			pGroup->pFacingLabel = GMDRefreshLabel(pGroup->pFacingLabel, "Facing", "Which direction this challenge is facing.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			if (!pGroup->pFacingField) {
				pGroup->pFacingField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "Facing", GenesisChallengeFacingEnum);
				GMDAddFieldToParent(pGroup->pFacingField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pFacingField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pFacingField, pOrigChallenge, pChallenge);
			}

			y += STANDARD_ROW_HEIGHT;

			// Update rotation increment
			pGroup->pRotationIncrementLabel = GMDRefreshLabel(pGroup->pRotationIncrementLabel, "Rot. Increment", "If non-zero, objects will only place at this rotation increment.  If zero, they can be at any rotation.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
			MEExpanderRefreshSimpleField( &pGroup->pRotationIncrementField, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "RotationIncrement", kMEFieldType_TextEntry,
										  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5,
										  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

			y += STANDARD_ROW_HEIGHT;
		}

		// Update exclude dist
		pGroup->pExcludeDistLabel = GMDRefreshLabel(pGroup->pExcludeDistLabel, "Exclude Dist", "Distance to keep other challenges that have exclude distances and are of similar challenge type.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
		MEExpanderRefreshSimpleField( &pGroup->pExcludeDistField, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "ExcludeDist", kMEFieldType_TextEntry,
			UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

		y += STANDARD_ROW_HEIGHT;


		if ((pChallenge->eFacing == GenesisChallengeFace_Challenge_Away ||
			pChallenge->eFacing == GenesisChallengeFace_Challenge_Toward ||
			pChallenge->ePlacement == GenesisChallengePlace_Near_Challenge) &&
			pChallenge->ePlacement != GenesisChallengePlace_Prefab_Location)
		{
			// Update challenge name
			pGroup->pChallengeRefLabel = GMDRefreshLabel(pGroup->pChallengeRefLabel, "Ref. Challenge", "The challenge to be used for facing or placement.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pChallengeRefField) {
				pGroup->pChallengeRefField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigChallenge, pChallenge, parse_GenesisMissionChallenge, "RefChallengeName", NULL, &pDoc->eaChallengeNames, NULL);
				GMDAddFieldToParent(pGroup->pChallengeRefField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pChallengeRefField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pChallengeRefField, pOrigChallenge, pChallenge);
			}

			y += STANDARD_ROW_HEIGHT;
		}
		else
		{
			ui_WidgetQueueFreeAndNull(&pGroup->pChallengeRefLabel);
			MEFieldSafeDestroy(&pGroup->pChallengeRefField);
		}
	}
	else
	{
		ui_WidgetQueueFreeAndNull(&pGroup->pPlacementLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pFacingLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRotationIncrementLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pExcludeDistLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pChallengeRefLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pPlacementPrefabLocationLabel);
		MEFieldSafeDestroy(&pGroup->pPlacementField);
		MEFieldSafeDestroy(&pGroup->pFacingField);
		MEFieldSafeDestroy(&pGroup->pRotationIncrementField);
		MEFieldSafeDestroy(&pGroup->pExcludeDistField);
		MEFieldSafeDestroy(&pGroup->pChallengeRefField);
		MEFieldSafeDestroy(&pGroup->pPlacementPrefabLocationField);
	}
	
	GMDRefreshButtonSet(pGroup->pExpander, 0, y,
						GMDHasNextChallenge(*peaChallenges, pDoc->pcEditingLayoutName, pGroup->index, true), GMDHasNextChallenge(*peaChallenges, pDoc->pcEditingLayoutName, pGroup->index, false),
						"Del Challenge", &pGroup->pRemoveButton, GMDRemoveChallenge, &pGroup->pUpButton, GMDUpChallenge, &pGroup->pDownButton, GMDDownChallenge, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static int GMDGetChallangeCountFromLayoutName(GenesisMissionChallenge **ppChallenges, const char *pcLayoutName)
{
	int i;
	int cnt = 0;
	for(i=eaSize(&ppChallenges)-1; i>=0; --i) {
		if(stricmp_safe(pcLayoutName, ppChallenges[i]->pcLayoutName) == 0)
			cnt++;
	}
	return cnt;
}

static void GMDRefreshChallenges(MapDescEditDoc *pDoc)
{
	const char *pcLayoutName = GMDGetActiveLayoutName(pDoc);
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	int iNumChallenges;
	int iNumSharedChallenges;
	int i, idx=0;

	// Make sure mission is present
	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Remove unused challenge groups
	iNumSharedChallenges = GMDGetChallangeCountFromLayoutName(pDoc->pMapDesc->shared_challenges, pcLayoutName);
	for(i=eaSize(&pDoc->eaSharedChallengeGroups)-1; i>=iNumSharedChallenges; --i) {
		assert(pDoc->eaSharedChallengeGroups);//For some reason the compiler requires this.
		GMDFreeChallengeGroup(pDoc->eaSharedChallengeGroups[i]);
		eaRemove(&pDoc->eaSharedChallengeGroups, i);
	}
	iNumChallenges = GMDGetChallangeCountFromLayoutName(pMission->eaChallenges, pcLayoutName);
	for(i=eaSize(&pDoc->eaChallengeGroups)-1; i>=iNumChallenges; --i) {
		assert(pDoc->eaChallengeGroups);//For some reason the compiler requires this.
		GMDFreeChallengeGroup(pDoc->eaChallengeGroups[i]);
		eaRemove(&pDoc->eaChallengeGroups, i);
	}

	// Refresh challenges
	idx = -1;
	for(i=0; i<iNumSharedChallenges; ++i) {
		GenesisMissionChallenge *pChallenge = GMDGetNextChallenge(pDoc->pMapDesc->shared_challenges, pcLayoutName, &idx, false);
		GenesisMissionChallenge *pOrigChallenge = NULL;

		if (eaSize(&pDoc->eaSharedChallengeGroups) <= i) {
			GMDChallengeGroup *pGroup = calloc(1, sizeof(GMDChallengeGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pChallengeExpanderGroup, "Challenge", i+1);
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaSharedChallengeGroups, pGroup);
		}
		pDoc->eaSharedChallengeGroups[i]->index = idx;
			
		if (pDoc->pOrigMapDesc && idx >=0 && idx < eaSize(&pDoc->pOrigMapDesc->shared_challenges)) {
			pOrigChallenge = pDoc->pOrigMapDesc->shared_challenges[idx];
		}

		GMDRefreshChallenge(pDoc, pDoc->eaSharedChallengeGroups[i], i, &pDoc->pMapDesc->shared_challenges, pOrigChallenge, pChallenge, true);
	}
	idx = -1;
	for(i=0; i<iNumChallenges; ++i) {
		GenesisMissionChallenge *pChallenge = GMDGetNextChallenge(pMission->eaChallenges, pcLayoutName, &idx, false);
		GenesisMissionChallenge *pOrigChallenge = NULL;

		if (eaSize(&pDoc->eaChallengeGroups) <= i) {
			GMDChallengeGroup *pGroup = calloc(1, sizeof(GMDChallengeGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pChallengeExpanderGroup, "Challenge", iNumSharedChallenges+i+1);
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaChallengeGroups, pGroup);
		}
		pDoc->eaChallengeGroups[i]->index = idx;
		
		if (pOrigMission && idx >=0 && idx < eaSize(&pOrigMission->eaChallenges)) {
			pOrigChallenge = pOrigMission->eaChallenges[idx];
		}

		GMDRefreshChallenge(pDoc, pDoc->eaChallengeGroups[i], i, &pMission->eaChallenges, pOrigChallenge, pChallenge, false);
	}
}


static F32 GMDRefreshPromptAction(MapDescEditDoc *pDoc, F32 y, GMDPromptActionGroup *pGroup, int index, GenesisMissionPromptAction ***peaActions, GenesisMissionPromptAction *pOrigAction, GenesisMissionPromptAction *pAction)
{
	char buf[256];

	// Refresh the group
	pGroup->peaActions = peaActions;
	pGroup->pAction = pAction;
	pGroup->pOrigAction = pOrigAction;

	// Update text
	sprintf(buf, "Action Button #%d", index+1);
	pGroup->pGroupLabel = GMDRefreshLabel(pGroup->pGroupLabel, buf, NULL, X_OFFSET_BASE, 0, y, pGroup->pGroup->pExpander);
	if (!pGroup->pRemoveButton) {
		pGroup->pRemoveButton = ui_ButtonCreate("Remove", 0, y, GMDRemovePromptAction, pGroup); 
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pRemoveButton), 80);
		ui_ExpanderAddChild(pGroup->pGroup->pExpander, pGroup->pRemoveButton);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pRemoveButton), 0, y, 0, 0, UITopRight);

	y += STANDARD_ROW_HEIGHT;

	// Update text
	pGroup->pTextLabel = GMDRefreshLabel(pGroup->pTextLabel, "Button Text", "The text for this action's button.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pGroup->pExpander);
	if (!pGroup->pTextField) {
		pGroup->pTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigAction, pAction, parse_GenesisMissionPromptAction, "Text");
		GMDAddFieldToParent(pGroup->pTextField, UI_WIDGET(pGroup->pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTextField, pOrigAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update next prompt
	pGroup->pNextLabel = GMDRefreshLabel(pGroup->pNextLabel, "Next Prompt", "The next prompt to show.  If empty, then none.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pGroup->pExpander);
	if (!pGroup->pNextField) {
		pGroup->pNextField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigAction, pAction, parse_GenesisMissionPromptAction, "NextPromptName", NULL, &pGroup->pGroup->pDoc->eaPromptNames, NULL);
		GMDAddFieldToParent(pGroup->pNextField, UI_WIDGET(pGroup->pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNextField, pOrigAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update grant mission
	pGroup->pGrantMissionLabel = GMDRefreshLabel(pGroup->pGrantMissionLabel, "Grant Mission", "If this should grant the mission.  Only use this with MissionGrantType Manual", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pGroup->pExpander);
	if (!pGroup->pGrantMissionField) {
		pGroup->pGrantMissionField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigAction, pAction, parse_GenesisMissionPromptAction, "GrantMission");
		GMDAddFieldToParent(pGroup->pGrantMissionField, UI_WIDGET(pGroup->pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pGrantMissionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pGrantMissionField, pOrigAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update dismiss action
	pGroup->pDismissActionLabel = GMDRefreshLabel(pGroup->pDismissActionLabel, "Fail Action", "If true, then a PromptSucceeded event will not be fired off for this action.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pGroup->pExpander);
	if (!pGroup->pDismissActionField) {
		pGroup->pDismissActionField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigAction, pAction, parse_GenesisMissionPromptAction, "DismissAction");
		GMDAddFieldToParent(pGroup->pDismissActionField, UI_WIDGET(pGroup->pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pDismissActionField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDismissActionField, pOrigAction, pAction);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update game action
	pGroup->pActionsLabel = GMDRefreshLabel(pGroup->pActionsLabel, "Game Actions", "The game actions to perform (if any).", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pGroup->pExpander);
	if (!pGroup->pActionButton) {
		pGroup->pActionButton = ui_GameActionEditButtonCreate(NULL, &pGroup->pAction->actionBlock, SAFE_MEMBER_ADDR(pGroup->pOrigAction, actionBlock), GMDGameActionChangeCB, NULL, pGroup); 
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), X_OFFSET_CONTROL, y);
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pActionButton), 120);
		ui_ExpanderAddChild(pGroup->pGroup->pExpander, pGroup->pActionButton);
	} else {
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pActionButton), X_OFFSET_CONTROL, y);
		ui_GameActionEditButtonSetData(pGroup->pActionButton, &pGroup->pAction->actionBlock, SAFE_MEMBER_ADDR(pGroup->pOrigAction, actionBlock));
	}

	y += STANDARD_ROW_HEIGHT;

	return y;
}

static void GMDActivePromptClone( void* ignored1, GMDPromptGroup* pGroup )
{
	GenesisMissionPrompt *pNewPrompt;
	GenesisMissionPrompt *pPrompt;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];

	// Perform the operation
	pNewPrompt = StructClone( parse_GenesisMissionPrompt, pPrompt );
	GMDPromptUniquifyName( *pGroup->peaPrompts, pNewPrompt );
	eaInsert( pGroup->peaPrompts, pNewPrompt, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePromptCut( void* ignored1, GMDPromptGroup* pGroup )
{
	GenesisMissionPrompt *pPrompt;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionPrompt, pPrompt, &g_GMDClipboardPrompt );
	StructDestroy( parse_GenesisMissionPrompt, pPrompt );
	eaRemove( pGroup->peaPrompts, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePromptCopy( void* ignored1, GMDPromptGroup* pGroup )
{
	GenesisMissionPrompt *pPrompt;

	if( !pGroup ) {
		return;
	}
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionPrompt, pPrompt, &g_GMDClipboardPrompt );
}

static void GMDActivePromptPaste( void* ignored1, GMDPromptGroup* pGroup )
{
	GenesisMissionPrompt *pPrompt;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_GenesisMissionPrompt, &g_GMDClipboardPrompt, pPrompt );
	if(pPrompt->pcLayoutName)
		StructFreeString(pPrompt->pcLayoutName);
	pPrompt->pcLayoutName = StructAllocString(GMDGetActiveLayoutName(pDoc));

	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDPromptBodyTextAdd( void* ignored1, GMDPromptGroup* pGroup )
{
	GenesisMissionPrompt *pPrompt;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];

	// Perform the operation
	eaPush( &pPrompt->sPrimaryBlock.eaBodyText, StructAllocString( "" ));

	// Refresh the UI
	GMDMapDescChanged(pDoc, true );
}

static void GMDPromptBodyTextRemove( UIButton* button, GMDPromptGroup* pGroup )
{
	GenesisMissionPrompt *pPrompt;
	MapDescEditDoc* pDoc;
	int index;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPrompt = (*pGroup->peaPrompts)[pGroup->index];

	for (index = eaSize( &pGroup->eaBodyTextAddRemoveButtons ) - 1; index >= 0; --index ) {
		if( pGroup->eaBodyTextAddRemoveButtons[ index ] == button ) {
			break;
		}
	}
	if( index == -1 ) {
		return;
	}

	// Perform the operation
	StructFreeString( pPrompt->sPrimaryBlock.eaBodyText[ index ]);
	eaRemove( &pPrompt->sPrimaryBlock.eaBodyText, index );

	// Refresh the UI
	GMDMapDescChanged(pDoc, true );
}

static GenesisMissionPrompt* GMDGetNextPrompt(GenesisMissionPrompt **ppPrompts, const char *pcLayoutName, int *idx, bool backwards)
{
	if(!ppPrompts)
		return NULL;

	if( backwards ) {
		for ( (*idx)--; (*idx) >= 0; (*idx)-- ) {
			GenesisMissionPrompt *pPrompt = ppPrompts[(*idx)];
			if(stricmp_safe(pcLayoutName, pPrompt->pcLayoutName) == 0) {
				return pPrompt;
			}
		}
	} else {
		for ( (*idx)++; (*idx) < eaSize(&ppPrompts); (*idx)++ ) {
			GenesisMissionPrompt *pPrompt = ppPrompts[(*idx)];
			if(stricmp_safe(pcLayoutName, pPrompt->pcLayoutName) == 0) {
				return pPrompt;
			}
		}
	}
	return NULL;
}

static bool GMDHasNextPrompt(GenesisMissionPrompt **ppPrompts, const char *pcLayoutName, int idx, bool backwards)
{
	return GMDGetNextPrompt(ppPrompts, pcLayoutName, &idx, backwards) != NULL;
}

static void GMDRefreshPrompt(MapDescEditDoc *pDoc, GMDPromptGroup *pGroup, int index, GenesisMissionPrompt ***peaPrompts, GenesisMissionPrompt *pOrigPrompt, GenesisMissionPrompt *pPrompt)
{
	char buf[256];
	F32 y = 0;
	int i;
	int iNumActions;

	// Refresh the group
	pGroup->peaPrompts = peaPrompts;

	// Update expander
	sprintf(buf, "Prompt: %s", pPrompt->pcName);
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);
	
	// Add popup menu
	if( !pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpPrompt, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownPrompt, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemovePrompt, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActivePromptClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActivePromptCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActivePromptCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActivePromptPaste, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	} 
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the prompt.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage,
							4, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigPrompt, pPrompt);
	}
	y += STANDARD_ROW_HEIGHT;

	// Update flags
	MEExpanderRefreshLabel( &pGroup->pDialogFlagsLabel, "Flags", "Add extra behavior to this prompt.  ForceOnTeam: Forces this prompt onto all teammates; if the teammate has a dialog already open, this one will queue.  Synchronized: Not yet implemented.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshFlagEnumField( &pGroup->pDialogFlagsField, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "DialogFlags", SpecialDialogFlagsEnum,
									UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
									GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Update CostumeType
	MEExpanderRefreshLabel( &pGroup->pCostumeTypeLabel, "Costume Type", NULL, X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshEnumField( &pGroup->pCostumeTypeField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "CostumeType", GenesisMissionCostumeTypeEnum,
								UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 120, UIUnitFixed, 5,
								GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	if (pPrompt->sPrimaryBlock.costume.eCostumeType == GenesisMissionCostumeType_Specified) {
		// Update costume
		MEExpanderRefreshLabel( &pGroup->pCostumeSpecifiedLabel, "Costume", "The costume for the prompt headshot.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pCostumeSpecifiedField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "Costume", "PlayerCostume",
												UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pCostumeSpecifiedLabel );
		MEFieldSafeDestroy( &pGroup->pCostumeSpecifiedField );
	}

	if (pPrompt->sPrimaryBlock.costume.eCostumeType == GenesisMissionCostumeType_PetCostume) {
		// Update costume
		MEExpanderRefreshLabel(&pGroup->pCostumePetLabel, "Pet Costume", "The pet whose costume to use for the prompt headshot.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pCostumePetField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "PetCostume", "PetContactList",
												UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pCostumePetLabel );
		MEFieldSafeDestroy( &pGroup->pCostumePetField );
	}

	if (pPrompt->sPrimaryBlock.costume.eCostumeType == GenesisMissionCostumeType_CritterGroup) {
		// Update CritterGroupType
		MEExpanderRefreshLabel(&pGroup->pCostumeCritterGroupTypeLabel, "From", "Where the critter group should be gathered from.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander));
		MEExpanderRefreshEnumField( &pGroup->pCostumeCritterGroupTypeField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "CostumeCritterGroupType", ContactMapVarOverrideTypeEnum,
									UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
									GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		if( pPrompt->sPrimaryBlock.costume.eCostumeCritterGroupType == ContactMapVarOverrideType_Specified ) {
			// Update CritterGroup
			MEExpanderRefreshLabel(&pGroup->pCostumeCritterGroupSpecifiedLabel, "Critter Group", "The specified Critter Group which the contact's costume will be generated from.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshGlobalDictionaryField( &pGroup->pCostumeCritterGroupSpecifiedField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "CostumeCritterGroup", "CritterGroup",
													UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
													GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pCostumeCritterGroupSpecifiedLabel );
			MEFieldSafeDestroy( &pGroup->pCostumeCritterGroupSpecifiedField );
		}

		if( pPrompt->sPrimaryBlock.costume.eCostumeCritterGroupType == ContactMapVarOverrideType_MapVar ) {
			// Update CritterGroup
			MEExpanderRefreshLabel(&pGroup->pCostumeCritterGroupMapVarLabel, "Map Variable", "The map variable where the Critter Group should be pulled from.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander));
			MEExpanderRefreshSimpleField( &pGroup->pCostumeCritterGroupMapVarField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "CostumeMapVar", kMEFieldType_TextEntry,
										  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
										  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pCostumeCritterGroupMapVarLabel );
			MEFieldSafeDestroy( &pGroup->pCostumeCritterGroupMapVarField );
		}

		// Update identifier
		MEExpanderRefreshLabel( &pGroup->pCostumeCritterGroupIDLabel, "Name", "A name for this costume.  To use this same costume again, use the same critter group and same name.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshSimpleField( &pGroup->pCostumeCritterGroupIDField, SAFE_MEMBER_ADDR(pOrigPrompt, sPrimaryBlock.costume), &pPrompt->sPrimaryBlock.costume, parse_GenesisMissionCostume, "CostumeIdentifier", kMEFieldType_TextEntry,
									  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pCostumeCritterGroupTypeLabel );
		MEFieldSafeDestroy( &pGroup->pCostumeCritterGroupTypeField );
		ui_WidgetQueueFreeAndNull( &pGroup->pCostumeCritterGroupSpecifiedLabel );
		MEFieldSafeDestroy( &pGroup->pCostumeCritterGroupSpecifiedField );
		ui_WidgetQueueFreeAndNull( &pGroup->pCostumeCritterGroupMapVarLabel );
		MEFieldSafeDestroy( &pGroup->pCostumeCritterGroupMapVarField );
		ui_WidgetQueueFreeAndNull( &pGroup->pCostumeCritterGroupIDLabel );
		MEFieldSafeDestroy( &pGroup->pCostumeCritterGroupIDField );
	}

	// Update headshotStyle
	MEExpanderRefreshLabel( &pGroup->pHeadshotStyleLabel, "Headshot Style", "The headshot style to use for this contact.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander));
	if( !pGroup->pHeadshotStyleField ) {
		pGroup->pHeadshotStyleField = MEFieldCreateSimpleDictionary( kMEFieldType_ValidatedTextEntry, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "HeadshotStyle", "HeadshotStyleDef", parse_HeadshotStyleDef, "Name");
		GMDAddFieldToParent( pGroup->pHeadshotStyleField, UI_WIDGET( pGroup->pExpander ), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc );
	} else {
		ui_WidgetSetPosition( pGroup->pHeadshotStyleField->pUIWidget, X_OFFSET_CONTROL, y );
		MEFieldSetAndRefreshFromData( pGroup->pHeadshotStyleField, pOrigPrompt, pPrompt );
	}
	y += STANDARD_ROW_HEIGHT;

	// Update show when
	if (!pGroup->pShowWhenGroup) {
		pGroup->pShowWhenGroup = calloc( 1, sizeof( *pGroup->pShowWhenGroup ));
	}
	y = GMDRefreshWhen(pDoc, pGroup->pShowWhenGroup, pGroup->pExpander,
					   "Show When", "When the prompt should be shown.", 0, y,
					   SAFE_MEMBER_ADDR(pOrigPrompt, showWhen), &pPrompt->showWhen, false);

	if( pPrompt->showWhen.type != GenesisWhen_Manual ) {
		// Update optional
		pGroup->pOptionalLabel = GMDRefreshLabel(pGroup->pOptionalLabel, "Has Button", "Show an OptionalAction button that when clicked shows the prompt.", X_OFFSET_BASE, 0, y, pGroup->pExpander);

		if (!pGroup->pOptionalField) {
			pGroup->pOptionalField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalPrompt");
			GMDAddFieldToParent(pGroup->pOptionalField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOptionalField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOptionalField, pOrigPrompt, pPrompt);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalLabel);
		MEFieldSafeDestroy(&pGroup->pOptionalField);
	}

	// Update optional fields
	if (pPrompt->bOptional && pPrompt->showWhen.type != GenesisWhen_Manual) {
		// Update button text
		pGroup->pOptionalButtonTextLabel = GMDRefreshLabel(pGroup->pOptionalButtonTextLabel, "Button Text", "Text shown on the button for this prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pOptionalButtonTextField) {
			pGroup->pOptionalButtonTextField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalButtonText");
			GMDAddFieldToParent(pGroup->pOptionalButtonTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOptionalButtonTextField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOptionalButtonTextField, pOrigPrompt, pPrompt);
		}

		y += STANDARD_ROW_HEIGHT;
		
		pGroup->pOptionalCategoryLabel = GMDRefreshLabel(pGroup->pOptionalCategoryLabel, "Category", "The category for the prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pOptionalCategoryField) {
			pGroup->pOptionalCategoryField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalCategoryName", NULL, &geaOptionalActionCategories, NULL);
			GMDAddFieldToParent(pGroup->pOptionalCategoryField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOptionalCategoryField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOptionalCategoryField, pOrigPrompt, pPrompt);
		}

		y += STANDARD_ROW_HEIGHT;
		
		pGroup->pOptionalPriorityLabel = GMDRefreshLabel(pGroup->pOptionalPriorityLabel, "Priority", "The priority for the prompt.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pOptionalPriorityField) {
			pGroup->pOptionalPriorityField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalPriority", WorldOptionalActionPriorityEnum);
			GMDAddFieldToParent(pGroup->pOptionalPriorityField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOptionalPriorityField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOptionalPriorityField, pOrigPrompt, pPrompt);
		}

		y += STANDARD_ROW_HEIGHT;

		pGroup->pOptionalHideOnCompleteLabel = GMDRefreshLabel(pGroup->pOptionalHideOnCompleteLabel, "Hide On Complete", "If true, the optional button will hide itself when the prompt completes", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pOptionalHideOnCompleteField) {
			pGroup->pOptionalHideOnCompleteField = MEFieldCreateSimple(kMEFieldType_Check, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalHideOnComplete");
			GMDAddFieldToParent(pGroup->pOptionalHideOnCompleteField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOptionalHideOnCompleteField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOptionalHideOnCompleteField, pOrigPrompt, pPrompt);
		}

		y += STANDARD_ROW_HEIGHT;

		if (pPrompt->bOptionalHideOnComplete) {
			pGroup->pOptionalHideOnCompletePromptLabel = GMDRefreshLabel(pGroup->pOptionalHideOnCompletePromptLabel, "Alt. Prompt", "An alternative prompt to hide the optional button.  If empty, the button will disappear when this prompt completes.", X_OFFSET_BASE + 2*X_OFFSET_INDENT, 0, y, pGroup->pExpander);
			if (!pGroup->pOptionalHideOnCompletePromptField) {
				pGroup->pOptionalHideOnCompletePromptField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalHideOnCompletePrompt", NULL, &pDoc->eaPromptNames, NULL);
				GMDAddFieldToParent(pGroup->pOptionalHideOnCompletePromptField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL + 15, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pOptionalHideOnCompletePromptField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pOptionalHideOnCompletePromptField, pOrigPrompt, pPrompt);
			}

			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pOptionalHideOnCompletePromptLabel);
			MEFieldSafeDestroy(&pGroup->pOptionalHideOnCompletePromptField);
		}

		// Update auto execute flag
		pGroup->pOptionalAutoExecuteLabel = GMDRefreshLabel(pGroup->pOptionalAutoExecuteLabel, "AutoExecute", "If the prompt should automatically show once.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, pGroup->pExpander);
		if (!pGroup->pOptionalAutoExecuteField) {
			pGroup->pOptionalAutoExecuteField = MEFieldCreateSimple(kMEFieldType_Check, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "OptionalAutoExecute");
			GMDAddFieldToParent(pGroup->pOptionalAutoExecuteField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pOptionalAutoExecuteField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pOptionalAutoExecuteField, pOrigPrompt, pPrompt);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalButtonTextLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalCategoryLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalPriorityLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalAutoExecuteLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalHideOnCompleteLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pOptionalHideOnCompletePromptLabel);
		MEFieldSafeDestroy(&pGroup->pOptionalButtonTextField);
		MEFieldSafeDestroy(&pGroup->pOptionalCategoryField);
		MEFieldSafeDestroy(&pGroup->pOptionalPriorityField);
		MEFieldSafeDestroy(&pGroup->pOptionalAutoExecuteField);
		MEFieldSafeDestroy(&pGroup->pOptionalHideOnCompleteField);
		MEFieldSafeDestroy(&pGroup->pOptionalHideOnCompletePromptField);
	}

	// Update title text
	pGroup->pTitleTextLabel = GMDRefreshLabel(pGroup->pTitleTextLabel, "Title Text", "The text for the title of the prompt window.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	if (!pGroup->pTitleTextField) {
		pGroup->pTitleTextField = MEFieldCreateSimple(kMEFieldType_MultiText, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "TitleText");
		GMDAddFieldToParent(pGroup->pTitleTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTitleTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTitleTextField, pOrigPrompt, pPrompt);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update body text
	pGroup->pBodyTextLabel = GMDRefreshLabel(pGroup->pBodyTextLabel, "Body Text", "The text for the body of the prompt window.", X_OFFSET_BASE, 0, y, pGroup->pExpander);
	
	if( eaSize( &pPrompt->sPrimaryBlock.eaBodyText ) == 0 ) {
		eaPush( &pPrompt->sPrimaryBlock.eaBodyText, NULL );
	}
	GMDRefreshAddRemoveButtons(&pGroup->eaBodyTextAddRemoveButtons, eaSize(&pPrompt->sPrimaryBlock.eaBodyText), y,
							   UI_WIDGET(pGroup->pExpander), GMDPromptBodyTextAdd, GMDPromptBodyTextRemove, pGroup );
	y = GMDRefreshEArrayFieldSimple(&pGroup->eaBodyTextField, kMEFieldType_SMFTextEntry, pOrigPrompt, pPrompt,
									X_OFFSET_CONTROL, y, 1.0, 5 + 16, UI_WIDGET(pGroup->pExpander), pDoc,
									parse_GenesisMissionPrompt, "BodyText");

	// Update phrase
	MEExpanderRefreshLabel( &pGroup->pPhraseLabel, "Phrase", "Voice over phrase to say based on the contact's costume.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshDataField( &pGroup->pPhraseField, pOrigPrompt, pPrompt, parse_GenesisMissionPrompt, "Phrase", &gPhraseNames, true, UI_WIDGET(pGroup->pExpander),
								X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
								GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;
	
	// Remove unused prompt action groups
	iNumActions = eaSize(&pPrompt->sPrimaryBlock.eaActions);
	for(i=eaSize(&pGroup->eaPromptActions)-1; i>=iNumActions; --i) {
		GMDFreePromptActionGroup(pGroup->eaPromptActions[i]);
		eaRemove(&pGroup->eaPromptActions, i);
	}

	// Refresh actions
	for(i=0; i<iNumActions; ++i) {
		GenesisMissionPromptAction *pAction = pPrompt->sPrimaryBlock.eaActions[i];
		GenesisMissionPromptAction *pOrigAction = NULL;

		if (eaSize(&pGroup->eaPromptActions) <= i) {
			GMDPromptActionGroup *pActionGroup = calloc(1, sizeof(GMDPromptActionGroup));
			pActionGroup->pGroup = pGroup;
			eaPush(&pGroup->eaPromptActions, pActionGroup);
		}
		pGroup->eaPromptActions[i]->index = i;
			
		if (pOrigPrompt && eaSize(&pOrigPrompt->sPrimaryBlock.eaActions) > i) {
			pOrigAction = pOrigPrompt->sPrimaryBlock.eaActions[i];
		}

		y = GMDRefreshPromptAction(pDoc, y, pGroup->eaPromptActions[i], i, &pPrompt->sPrimaryBlock.eaActions, pOrigAction, pAction);
	}

	if (!pGroup->pAddActionButton) {
		pGroup->pAddActionButton = ui_ButtonCreate("Add Action", X_OFFSET_BASE, y, GMDAddPromptAction, pGroup); 
		ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddActionButton), 100);
		ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddActionButton);
	}
	ui_WidgetSetPositionEx(UI_WIDGET(pGroup->pAddActionButton), X_OFFSET_BASE, y, 0, 0, UITopLeft);

	y += STANDARD_ROW_HEIGHT;

	// Add Delete/Up/Down buttons
	GMDRefreshButtonSet(pGroup->pExpander, 0, y,
						GMDHasNextPrompt(*peaPrompts, pDoc->pcEditingLayoutName, pGroup->index, true), GMDHasNextPrompt(*peaPrompts, pDoc->pcEditingLayoutName, pGroup->index, false),
						"Delete Prompt", &pGroup->pRemoveButton, GMDRemovePrompt, &pGroup->pUpButton, GMDUpPrompt, &pGroup->pDownButton, GMDDownPrompt, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static int GMDGetPromptCountFromLayoutName(GenesisMissionPrompt **ppPrompts, const char *pcLayoutName)
{
	int i;
	int cnt = 0;
	for(i=eaSize(&ppPrompts)-1; i>=0; --i) {
		if(stricmp_safe(pcLayoutName, ppPrompts[i]->pcLayoutName) == 0)
			cnt++;
	}
	return cnt;
}

static void GMDRefreshPrompts(MapDescEditDoc *pDoc)
{
	const char *pcLayoutName = GMDGetActiveLayoutName(pDoc);
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	int iNumPrompts;
	int i, idx=0;

	// Make sure mission is present
	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Remove unused prompt groups
	iNumPrompts = GMDGetPromptCountFromLayoutName(pMission->zoneDesc.eaPrompts, pcLayoutName);
	for(i=eaSize(&pDoc->eaPromptGroups)-1; i>=iNumPrompts; --i) {
		assert(pDoc->eaPromptGroups);
		GMDFreePromptGroup(pDoc->eaPromptGroups[i]);
		eaRemove(&pDoc->eaPromptGroups, i);
	}

	// Refresh prompts
	idx = -1;
	for(i=0; i<iNumPrompts; ++i) {
		GenesisMissionPrompt *pPrompt = GMDGetNextPrompt(pMission->zoneDesc.eaPrompts, pcLayoutName, &idx, false);
		GenesisMissionPrompt *pOrigPrompt = NULL;

		if (eaSize(&pDoc->eaPromptGroups) <= i) {
			GMDPromptGroup *pGroup = calloc(1, sizeof(GMDPromptGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pChallengeExpanderGroup, "Prompt", i+1+eaSize(&pDoc->eaChallengeGroups)+eaSize(&pDoc->eaSharedChallengeGroups));
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaPromptGroups, pGroup);
		}
		pDoc->eaPromptGroups[i]->index = idx;
			
		if (pOrigMission && eaSize(&pOrigMission->zoneDesc.eaPrompts) > i) {
			pOrigPrompt = pOrigMission->zoneDesc.eaPrompts[i];
		}

		GMDRefreshPrompt(pDoc, pDoc->eaPromptGroups[i], i, &pMission->zoneDesc.eaPrompts, pOrigPrompt, pPrompt);
	}
}

static void GMDActivePortalClone( void* ignored1, GMDPortalGroup* pGroup )
{
	GenesisMissionPortal *pPortal;
	GenesisMissionPortal *pNewPortal;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPortal = (*pGroup->peaPortals)[pGroup->index];

	// Perform the operation
	pNewPortal = StructClone( parse_GenesisMissionPortal, pPortal );
	GMDPortalUniquifyName( *pGroup->peaPortals, pNewPortal );
	eaInsert( pGroup->peaPortals, pNewPortal, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePortalCut( void* ignored1, GMDPortalGroup* pGroup )
{
	GenesisMissionPortal *pPortal;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPortal = (*pGroup->peaPortals)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionPortal, pPortal, &g_GMDClipboardPortal );
	StructDestroy( parse_GenesisMissionPortal, pPortal );
	eaRemove( pGroup->peaPortals, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActivePortalCopy( void* ignored1, GMDPortalGroup* pGroup )
{
	GenesisMissionPortal *pPortal;

	if( !pGroup ) {
		return;
	}
	pPortal = (*pGroup->peaPortals)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionPortal, pPortal, &g_GMDClipboardPortal );
}

static void GMDActivePortalPaste( void* ignored1, GMDPortalGroup* pGroup )
{
	GenesisMissionPortal *pPortal;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pPortal = (*pGroup->peaPortals)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_GenesisMissionPortal, &g_GMDClipboardPortal, pPortal );
	StructCopyString(&pPortal->pcStartLayout, GMDGetActiveLayoutName(pDoc));
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDPortalAddVariable(UIButton *pButton, GMDPortalGroup *pGroup)
{
	WorldVariableDef *pVarDef = StructCreate( parse_WorldVariableDef );
	pVarDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
	pVarDef->pSpecificValue = StructCreate( parse_WorldVariable );
	eaPush( &(*pGroup->peaPortals)[ pGroup->index ]->eaEndVariables, pVarDef );

	// Notify of change
	GMDFieldChangedCB( NULL, true, pGroup->pDoc );
}

static void GMDPortalStartRoomFinished( UITextEntry* textEntry, GMDPortalGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	GenesisMissionPortal* pPortal = (*pGroup->peaPortals)[ pGroup->index ];
	const char* roomLayoutText = ui_TextEntryGetText( textEntry );

	GMDSplitRoomLayoutEntry( roomLayoutText, &pPortal->pcStartRoom, &pPortal->pcStartLayout );	
	GMDMapDescChanged(pDoc, true);
}

static void GMDPortalEndRoomFinished( UITextEntry* textEntry, GMDPortalGroup* pGroup )
{
	MapDescEditDoc* pDoc = pGroup->pDoc;
	GenesisMissionPortal* pPortal = (*pGroup->peaPortals)[ pGroup->index ];
	const char* roomLayoutText = ui_TextEntryGetText( textEntry );

	GMDSplitRoomLayoutEntry( roomLayoutText, &pPortal->pcEndRoom, &pPortal->pcEndLayout );	
	GMDMapDescChanged(pDoc, true);
}

static GenesisMissionPortal* GMDGetNextPortal(GenesisMissionPortal **ppPortals, const char *pcLayoutName, int *idx, bool backwards)
{
	if(!ppPortals)
		return NULL;

	if( backwards ) {
		for ( (*idx)--; (*idx) >= 0; (*idx)-- ) {
			GenesisMissionPortal *pPortal = ppPortals[(*idx)];
			if(pPortal->eType == GenesisMissionPortal_BetweenLayouts || stricmp_safe(pcLayoutName, pPortal->pcStartLayout) == 0) {
				return pPortal;
			}
		}
	} else {
		for ( (*idx)++; (*idx) < eaSize(&ppPortals); (*idx)++ ) {
			GenesisMissionPortal *pPortal = ppPortals[(*idx)];
			if(pPortal->eType == GenesisMissionPortal_BetweenLayouts || stricmp_safe(pcLayoutName, pPortal->pcStartLayout) == 0) {
				return pPortal;
			}
		}
	}
	return NULL;
}

static bool GMDHasNextPortal(GenesisMissionPortal **ppPortals, const char *pcLayoutName, int idx, bool bBackwards)
{
	return GMDGetNextPortal(ppPortals, pcLayoutName, &idx, bBackwards) != NULL;
}

static bool GMDCanAddDoorPortal(MapDescEditDoc *pDoc, GenesisMissionPortal *pPortal)
{
	if(pPortal->eType == GenesisMissionPortal_BetweenLayouts) {
		return eaSize(&pDoc->pMapDesc->interior_layouts) > 0;
	}
	return pDoc->EditingMapType == GenesisMapType_Interior;
}

static void GMDRefreshPortal(MapDescEditDoc *pDoc, GMDPortalGroup *pGroup, int index, GenesisMissionPortal ***peaPortals, GenesisMissionPortal *pOrigPortal, GenesisMissionPortal *pPortal)
{
	char buf[256];
	F32 y = 0;
	
	// Refresh the group
	pGroup->peaPortals = peaPortals;

	// Update expander
	sprintf(buf, "Portal: %s", pPortal->pcName);
	ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);
	
	// Add popup menu 
	if( !pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpPortal, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownPortal, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemovePortal, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActivePortalClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActivePortalCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActivePortalCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActivePortalPaste, pGroup, NULL ),
				NULL );
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	}
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update Name
	MEExpanderRefreshLabel( &pGroup->pNameLabel, "Name", "A unique name for this portal.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshSimpleField( &pGroup->pNameField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "Name", kMEFieldType_TextEntry,
								  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
								  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Update Type
	MEExpanderRefreshLabel( &pGroup->pTypeLabel, "Type", "What type of portal this is.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshEnumField( &pGroup->pTypeField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "Type", GenesisMissionPortalTypeEnum,
								UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
								GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Update Use Type
	if( GMDCanAddDoorPortal(pDoc, pPortal) ) {
		MEExpanderRefreshLabel( &pGroup->pUseTypeLabel, "Interact", "How do you use the portal.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshEnumField( &pGroup->pUseTypeField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "UseType", GenesisMissionPortalUseTypeEnum,
			UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pUseTypeLabel );
		MEFieldSafeDestroy(&pGroup->pUseTypeField );		
	}

	// Update Start Room
	MEExpanderRefreshLabel( &pGroup->pStartRoomLabel, "Start Room", "One room this portal links up.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	if( pPortal->eType == GenesisMissionPortal_BetweenLayouts ) {
		GMDRefreshTextEntryRoomLayoutPair( &pGroup->pStartRoomText, pDoc, GMDPortalStartRoomFinished, pGroup,
										   UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4 );
		GMDTextEntrySetRoomLayout( pGroup->pStartRoomText,
								   pPortal->pcStartRoom, pPortal->pcStartLayout,
								   SAFE_MEMBER(pOrigPortal, pcStartRoom), SAFE_MEMBER(pPortal, pcStartLayout));

		MEFieldSafeDestroy( &pGroup->pStartRoomField );
	} else {
		MEExpanderRefreshDataField( &pGroup->pStartRoomField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "StartRoom", &pDoc->eaRoomNames, true,
									UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
									GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

		ui_WidgetQueueFreeAndNull( &pGroup->pStartRoomText );
	}
	y += STANDARD_ROW_HEIGHT;

	if( pPortal->eUseType == GenesisMissionPortal_Door && GMDCanAddDoorPortal(pDoc, pPortal) ) {
		// Update Start Door Name
		MEExpanderRefreshLabel( &pGroup->pStartDoorLabel, "Start Door", "Optional: The name of the door to use.  Use this if you want to have two portals use the same door.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshSimpleField( &pGroup->pStartDoorField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "StartDoor", kMEFieldType_TextEntry,
			UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pStartDoorLabel );
		MEFieldSafeDestroy(&pGroup->pStartDoorField );	
	}

	if( pPortal->eType == GenesisMissionPortal_Normal || pPortal->eType == GenesisMissionPortal_BetweenLayouts ) {
		// Update Warp To Start Text
		MEExpanderRefreshLabel( &pGroup->pWarpToStartTextLabel, "Warp To Start Text", "Text displayed when this portal warps to the start room.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshSimpleField( &pGroup->pWarpToStartTextField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "WarpToStartText", kMEFieldType_TextEntry,
									  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pWarpToStartTextLabel );
		MEFieldSafeDestroy(&pGroup->pWarpToStartTextField );
	}

	if( pPortal->eType == GenesisMissionPortal_OneWayOutOfMap ) {
		// Update Target ZoneMap
		MEExpanderRefreshLabel( &pGroup->pEndZmapLabel, "End ZMap", "What map this portal leads to.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshGlobalDictionaryField( &pGroup->pEndZmapField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "EndZmap", "ZoneMap",
												UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
												GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		// Update End Room
		if( pGroup->pEndRoomField && pGroup->pEndRoomField->peaComboModel ) {
			MEFieldSafeDestroy( &pGroup->pEndRoomField );
		}
		MEExpanderRefreshLabel( &pGroup->pEndRoomLabel, "End Spawn", "The Spawn in this map the portal leads to.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
		MEExpanderRefreshSimpleField( &pGroup->pEndRoomField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "EndRoom", kMEFieldType_TextEntry,
									  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		// Update variable groups
		{
			int varIt;
			for( varIt = 0; varIt != eaSize( &pPortal->eaEndVariables ); ++varIt ) {
				WorldVariableDef* pVarDef = pPortal->eaEndVariables[ varIt ];
				WorldVariableDef* pOrigVarDef = NULL;

				if( pOrigPortal && varIt < eaSize( &pOrigPortal->eaEndVariables )) {
					pOrigVarDef = pOrigPortal->eaEndVariables[ varIt ];
				}
				if( varIt >= eaSize( &pGroup->eaEndVariablesGroup )) {
					GEVariableDefGroup *pVarDefGroup = calloc( 1, sizeof( *pVarDefGroup ));
					pVarDefGroup->pData = pGroup;
					eaPush( &pGroup->eaEndVariablesGroup, pVarDefGroup );
				}
				{
					char** varNames = NULL;
					genesisVariableDefNames(&varNames);
					y = GEUpdateVariableDefGroup( pGroup->eaEndVariablesGroup[ varIt ], UI_WIDGET( pGroup->pExpander ), &pPortal->eaEndVariables, pVarDef, pOrigVarDef, NULL, varNames, pPortal->pcEndZmap, varIt, X_OFFSET_BASE, X_OFFSET_CONTROL, y, GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
				}
			}
			for( varIt = eaSize( &pGroup->eaEndVariablesGroup ) - 1; varIt >= eaSize( &pPortal->eaEndVariables ); --varIt ) {
				GEFreeVariableDefGroup( pGroup->eaEndVariablesGroup[ varIt ]);
				eaRemoveFast( &pGroup->eaEndVariablesGroup, varIt );
			}

			// Add button
			if( !pGroup->pEndVariableAddButton ) {
				pGroup->pEndVariableAddButton = ui_ButtonCreate( "Set Variable", X_OFFSET_BASE + 20, y, GMDPortalAddVariable, pGroup );
				ui_WidgetAddChild( UI_WIDGET( pGroup->pExpander ), UI_WIDGET( pGroup->pEndVariableAddButton ));
			} else {
				ui_WidgetSetPosition( UI_WIDGET( pGroup->pEndVariableAddButton ), X_OFFSET_BASE + 20, y );
			}
			
			y += STANDARD_ROW_HEIGHT;
		}
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pEndZmapLabel );
		MEFieldSafeDestroy(&pGroup->pEndZmapField );

		// Update End Room
		if( pGroup->pEndRoomField && !pGroup->pEndRoomField->peaComboModel ) {
			MEFieldSafeDestroy( &pGroup->pEndRoomField );
		}
		MEExpanderRefreshLabel( &pGroup->pEndRoomLabel, "End Room", "One room this portal links up.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
		if( pPortal->eType == GenesisMissionPortal_BetweenLayouts ) {
			GMDRefreshTextEntryRoomLayoutPair( &pGroup->pEndRoomText, pDoc, GMDPortalEndRoomFinished, pGroup,
											   UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4 );
			GMDTextEntrySetRoomLayout( pGroup->pEndRoomText, pPortal->pcEndRoom, pPortal->pcEndLayout,
									   SAFE_MEMBER(pPortal, pcEndRoom), SAFE_MEMBER(pPortal, pcEndLayout));

			MEFieldSafeDestroy( &pGroup->pEndRoomField );
		} else {
			MEExpanderRefreshDataField( &pGroup->pEndRoomField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "EndRoom", &pDoc->eaRoomNames, true,
										UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
										GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

			ui_WidgetQueueFreeAndNull( &pGroup->pEndRoomText );
		}
		y += STANDARD_ROW_HEIGHT;

		if( pPortal->eUseType == GenesisMissionPortal_Door && GMDCanAddDoorPortal(pDoc, pPortal) ) {
			// Update End Door Name
			MEExpanderRefreshLabel( &pGroup->pEndDoorLabel, "End Door", "Optional: The name of the door to use.  Use this if you want to have two portals use the same door.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
			MEExpanderRefreshSimpleField( &pGroup->pEndDoorField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "EndDoor", kMEFieldType_TextEntry,
				UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
				GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pEndDoorLabel );
			MEFieldSafeDestroy(&pGroup->pEndDoorField );	
		}
		
		// Update variables
		eaDestroyEx( &pGroup->eaEndVariablesGroup, GEFreeVariableDefGroup );
		ui_WidgetQueueFreeAndNull( &pGroup->pEndVariableAddButton );
	}

	// Update Warp To End Text
	MEExpanderRefreshLabel( &pGroup->pWarpToEndTextLabel, "Warp To End Text", "Text displayed when this portal warps to the end room.", X_OFFSET_BASE, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshSimpleField( &pGroup->pWarpToEndTextField, pOrigPortal, pPortal, parse_GenesisMissionPortal, "WarpToEndText", kMEFieldType_TextEntry,
								  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 4,
								  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Update When
	if (!pGroup->pWhenGroup) {
		pGroup->pWhenGroup = calloc( 1, sizeof( *pGroup->pWhenGroup ));
	}
	y = GMDRefreshWhen(pDoc, pGroup->pWhenGroup, pGroup->pExpander,
					   "Active When", "When this is true, the portal can be used.", 0, y, SAFE_MEMBER_ADDR(pOrigPortal, when), &pPortal->when, false);
  
	// Add Delete/Up/Down buttons
	GMDRefreshButtonSet(pGroup->pExpander, 0, y,
						GMDHasNextPortal(*peaPortals, pDoc->pcEditingLayoutName, pGroup->index, true), GMDHasNextPortal(*peaPortals, pDoc->pcEditingLayoutName, pGroup->index, false),
						"Delete Portal", &pGroup->pRemoveButton, GMDRemovePortal, &pGroup->pUpButton, GMDUpPortal, &pGroup->pDownButton, GMDDownPortal, pGroup);

	y += STANDARD_ROW_HEIGHT;

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static int GMDGetPortalCountFromLayoutName(GenesisMissionPortal **ppPortals, const char *pcLayoutName)
{
	int i;
	int cnt = 0;
	for(i=eaSize(&ppPortals)-1; i>=0; --i) {
		if(ppPortals[i]->eType == GenesisMissionPortal_BetweenLayouts || stricmp_safe(pcLayoutName, ppPortals[i]->pcStartLayout) == 0)
			cnt++;
	}
	return cnt;
}

static void GMDRefreshPortals(MapDescEditDoc *pDoc)
{
	const char *pcLayoutName = GMDGetActiveLayoutName(pDoc);
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	int iNumPortals;
	int i, idx=0;

	// Make sure mission is present
	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Remove unused portal groups
	iNumPortals = GMDGetPortalCountFromLayoutName(pMission->zoneDesc.eaPortals, pcLayoutName);
	for(i=eaSize(&pDoc->eaPortalGroups)-1; i>=iNumPortals; --i) {
		assert(pDoc->eaPortalGroups);
		GMDFreePortalGroup(pDoc->eaPortalGroups[i]);
		eaRemove(&pDoc->eaPortalGroups, i);
	}

	// Refresh portals
	idx = -1;
	for(i=0; i<iNumPortals; ++i) {
		GenesisMissionPortal *pPortal = GMDGetNextPortal(pMission->zoneDesc.eaPortals, pcLayoutName, &idx, false);
		GenesisMissionPortal *pOrigPortal = NULL;

		if (eaSize(&pDoc->eaPortalGroups) <= i) {
			GMDPortalGroup *pGroup = calloc(1, sizeof(GMDPortalGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pChallengeExpanderGroup, "Portal", i+1+eaSize(&pDoc->eaChallengeGroups)+eaSize(&pDoc->eaSharedChallengeGroups) + eaSize(&pDoc->eaPromptGroups));
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaPortalGroups, pGroup);
		}
		pDoc->eaPortalGroups[i]->index = idx;
			
		if (pOrigMission && eaSize(&pOrigMission->zoneDesc.eaPortals) > i) {
			pOrigPortal = pOrigMission->zoneDesc.eaPortals[i];
		}

		GMDRefreshPortal(pDoc, pDoc->eaPortalGroups[i], i, &pMission->zoneDesc.eaPortals, pOrigPortal, pPortal);
	}
}


static void GMDRefreshMissionInfo(MapDescEditDoc *pDoc)
{
	UIExpander *pExpander = pDoc->pMissionInfoGroup->pExpander;
	GMDMissionInfoGroup *pGroup = pDoc->pMissionInfoGroup;
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	F32 y = 0;

	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Mission Name", "The name of the mission.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigMission, pMission, parse_GenesisMissionDescription, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigMission, pMission);
	}
	y += STANDARD_ROW_HEIGHT;

	// Update mission type
	MEExpanderRefreshLabel( &pGroup->pGenerationTypeLabel, "Type", "What type of mission should be generated.", X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshEnumField( &pGroup->pGenerationTypeField, pOrigMission, pMission, parse_GenesisMissionDescription, "GenerationType", GenesisMissionGenerationTypeEnum, UI_WIDGET(pExpander),
								X_OFFSET_CONTROL, y, 0, 200, UIUnitFixed, 5,
								GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	{
		char* primaryPrefix = NULL;
		char* openPrefix = NULL;
		char buffer[ 256 ];

		switch( pMission->zoneDesc.generationType ) {
			case GenesisMissionGenerationType_PlayerMission: case GenesisMissionGenerationType_OpenMission_NoPlayerMission:
				primaryPrefix = "";
				openPrefix = NULL;

			xcase GenesisMissionGenerationType_OpenMission:
				primaryPrefix = "Open ";
				openPrefix = "Player ";
		}
		
		// Update display name
		sprintf( buffer, "%sDisp Name", primaryPrefix );
		pGroup->pDisplayNameLabel = GMDRefreshLabel(pGroup->pDisplayNameLabel, buffer, "The display name of the mission for the Journal and UI.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pDisplayNameField) {
			pGroup->pDisplayNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigMission, pMission, parse_GenesisMissionDescription, "DisplayName");
			GMDAddFieldToParent(pGroup->pDisplayNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pDisplayNameField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pDisplayNameField, pOrigMission, pMission);
		}
		y += STANDARD_ROW_HEIGHT;
		
		// Update short text
		sprintf( buffer, "%sUI String", primaryPrefix );
		pGroup->pShortTextLabel = GMDRefreshLabel(pGroup->pShortTextLabel, buffer, "The text for the mission to show on the HUD.  If not set, then only the mission name will be displayed.", X_OFFSET_BASE, 0, y, pExpander);
		if (!pGroup->pShortTextField) {
			pGroup->pShortTextField = MEFieldCreateSimple(kMEFieldType_MultiText, pOrigMission, pMission, parse_GenesisMissionDescription, "ShortText");
			GMDAddFieldToParent(pGroup->pShortTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pShortTextField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pShortTextField, pOrigMission, pMission);
		}
		y += STANDARD_ROW_HEIGHT;
	
		if (pMission->zoneDesc.generationType == GenesisMissionGenerationType_OpenMission) {
			if (!pMission->zoneDesc.pOpenMissionDescription) {
				pMission->zoneDesc.pOpenMissionDescription = StructCreate( parse_GenesisMissionOpenMissionDescription );
			}

			// Update player mission name
			sprintf( buffer, "%sDisp Name", openPrefix );
			pGroup->pOpenMissionNameLabel = GMDRefreshLabel(pGroup->pOpenMissionNameLabel, buffer, "The display name of the mission for the Journal and UI.", X_OFFSET_BASE, 0, y, pExpander);
			if (!pGroup->pOpenMissionNameField) {
				pGroup->pOpenMissionNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pOrigMission, zoneDesc.pOpenMissionDescription), pMission->zoneDesc.pOpenMissionDescription, parse_GenesisMissionOpenMissionDescription, "DisplayName");
				GMDAddFieldToParent(pGroup->pOpenMissionNameField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pOpenMissionNameField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pOpenMissionNameField, SAFE_MEMBER(pOrigMission, zoneDesc.pOpenMissionDescription), pMission->zoneDesc.pOpenMissionDescription);
			}
			y += STANDARD_ROW_HEIGHT;

			// Update player short text
			sprintf( buffer, "%sUI String", openPrefix );
			pGroup->pOpenMissionShortTextLabel = GMDRefreshLabel(pGroup->pOpenMissionShortTextLabel, buffer, "The text for the mission to show on the HUD.  If not set, then only the mission name will be displayed.", X_OFFSET_BASE, 0, y, pExpander);
			if (!pGroup->pOpenMissionShortTextField) {
				pGroup->pOpenMissionShortTextField = MEFieldCreateSimple(kMEFieldType_MultiText, SAFE_MEMBER(pOrigMission, zoneDesc.pOpenMissionDescription), pMission->zoneDesc.pOpenMissionDescription, parse_GenesisMissionOpenMissionDescription, "ShortText");
				GMDAddFieldToParent(pGroup->pOpenMissionShortTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
			} else {
				ui_WidgetSetPosition(pGroup->pOpenMissionShortTextField->pUIWidget, X_OFFSET_CONTROL, y);
				MEFieldSetAndRefreshFromData(pGroup->pOpenMissionShortTextField, SAFE_MEMBER(pOrigMission, zoneDesc.pOpenMissionDescription), pMission->zoneDesc.pOpenMissionDescription);
			}
			y += STANDARD_ROW_HEIGHT;
		} else {
			ui_WidgetQueueFreeAndNull(&pGroup->pOpenMissionNameLabel);
			ui_WidgetQueueFreeAndNull(&pGroup->pOpenMissionShortTextLabel);
			MEFieldSafeDestroy(&pGroup->pOpenMissionNameField);
			MEFieldSafeDestroy(&pGroup->pOpenMissionShortTextField);
		}
	}

	// Update description
	pGroup->pDescriptionTextLabel = GMDRefreshLabel(pGroup->pDescriptionTextLabel, "Journal Description", "The long text for the mission that displays in the mission journal.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pDescriptionTextField) {
		pGroup->pDescriptionTextField = MEFieldCreateSimple(kMEFieldType_SMFTextEntry, pOrigMission, pMission, parse_GenesisMissionDescription, "DescriptionText");
		GMDAddFieldToParent(pGroup->pDescriptionTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pDescriptionTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pDescriptionTextField, pOrigMission, pMission);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update summary
	pGroup->pSummaryTextLabel = GMDRefreshLabel(pGroup->pSummaryTextLabel, "Journal Summary", "The summary for the mission that displays in the mission journal.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pSummaryTextField) {
		pGroup->pSummaryTextField = MEFieldCreateSimple(kMEFieldType_MultiText, pOrigMission, pMission, parse_GenesisMissionDescription, "Summary");
		GMDAddFieldToParent(pGroup->pSummaryTextField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pSummaryTextField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pSummaryTextField, pOrigMission, pMission);
	}

	y += STANDARD_ROW_HEIGHT;
	
	// Update category
	pGroup->pCategoryLabel = GMDRefreshLabel(pGroup->pCategoryLabel, "Journal Category", "The mission journal category for the mission.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pCategoryField) {
		pGroup->pCategoryField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigMission, pMission, parse_GenesisMissionDescription, "Category", "MissionCategory", "ResourceName");
		GMDAddFieldToParent(pGroup->pCategoryField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pCategoryField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pCategoryField, pOrigMission, pMission);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update shareable
	pGroup->pShareableLabel = GMDRefreshLabel(pGroup->pShareableLabel, "Shareable", NULL, X_OFFSET_BASE, 0, y, pExpander);
	MEExpanderRefreshEnumField( &pGroup->pShareableField, pOrigMission, pMission, parse_GenesisMissionDescription, "Shareable", MissionShareableTypeEnum,
								UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5,
								GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Update level
	if( !pGroup->pLevelDefGroup ) {
		pGroup->pLevelDefGroup = calloc( 1, sizeof( *pGroup->pLevelDefGroup ));
	}
	y = GEUpdateMissionLevelDefGroup(pGroup->pLevelDefGroup, pExpander, &pMission->zoneDesc.levelDef, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.levelDef),
									 X_OFFSET_BASE, X_OFFSET_CONTROL, y,
									 GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );

	// Update rewards
	pGroup->pRewardLabel = GMDRefreshLabel(pGroup->pRewardLabel, "Reward", "Reward given when the mission is completed.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pRewardField) {
		pGroup->pRewardField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_TextEntry, pOrigMission, pMission, parse_GenesisMissionDescription, "Reward", "RewardTable", "ResourceName");
		GMDAddFieldToParent(pGroup->pRewardField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pRewardField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRewardField, pOrigMission, pMission);
	}
	
	y += STANDARD_ROW_HEIGHT;

	// Update reward scale
	pGroup->pRewardScaleLabel = GMDRefreshLabel(pGroup->pRewardScaleLabel, "Reward Scale", "The multiplier on numeric rewards for the mission.  1.0 is normal.", X_OFFSET_BASE, 0, y, pExpander );
	if (!pGroup->pRewardScaleField) {
		pGroup->pRewardScaleField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigMission, pMission, parse_GenesisMissionDescription, "RewardScale");
		GMDAddFieldToParent(pGroup->pRewardScaleField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 0, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pRewardScaleField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pRewardScaleField, pOrigMission, pMission);
	}

	y += STANDARD_ROW_HEIGHT;

	// Mission drops
	MEExpanderRefreshLabel(&pGroup->pDropRewardTableLabel, "Drop Reward Table", NULL, X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshGlobalDictionaryField( &pGroup->pDropRewardTableField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc), &pMission->zoneDesc, parse_GenesisMissionZoneDescription, "DropRewardTable", "RewardTable",
											UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
											GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;
	
	MEExpanderRefreshLabel(&pGroup->pDropChallengeNamesLabel, "Drop Challenges", NULL, X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshDataField( &pGroup->pDropChallengeNamesField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc), &pMission->zoneDesc, parse_GenesisMissionZoneDescription, "DropChallengeName", &pDoc->eaChallengeNames
								, true,
								UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
								GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;
	

	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}


static void GMDRefreshMissionStart(MapDescEditDoc *pDoc)
{
	UIExpander *pExpander = pDoc->pMissionStartGroup->pExpander;
	GMDMissionStartGroup *pGroup = pDoc->pMissionStartGroup;
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	F32 y = 0;

	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Update grant
	pGroup->pGrantLabel = GMDRefreshLabel(pGroup->pGrantLabel, "Grant Type", "How the mission is granted to the player.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pGrantField) {
		pGroup->pGrantField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "GrantType", GenesisMissionGrantTypeEnum);
		GMDAddFieldToParent(pGroup->pGrantField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pGrantField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pGrantField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pMission->zoneDesc.grantDescription.eGrantType == GenesisMissionGrantType_Contact) {
		if (!pMission->zoneDesc.grantDescription.pGrantContact) {
			pMission->zoneDesc.grantDescription.pGrantContact = StructCreate( parse_GenesisMissionGrant_Contact );
		}
		
		// Grant offer text
		pGroup->pGrantContactOfferLabel = GMDRefreshLabel(pGroup->pGrantContactOfferLabel, "Offer Text", "Contact's dialog when offering mission to the player.", X_OFFSET_BASE+X_OFFSET_INDENT, 0, y, pExpander);
		if (!pGroup->pGrantContactOfferField) {
			pGroup->pGrantContactOfferField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pGrantContact), pMission->zoneDesc.grantDescription.pGrantContact, parse_GenesisMissionGrant_Contact, "OfferText");
			GMDAddFieldToParent(pGroup->pGrantContactOfferField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pGrantContactOfferField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pGrantContactOfferField, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pGrantContact), pMission->zoneDesc.grantDescription.pGrantContact);
		}

		y += STANDARD_ROW_HEIGHT;

		// Grant In progress text
		pGroup->pGrantContactInProgressLabel = GMDRefreshLabel(pGroup->pGrantContactInProgressLabel, "In Progress", "Contact's dialog if player returns while mission still in progress.", X_OFFSET_BASE+X_OFFSET_INDENT, 0, y, pExpander);
		if (!pGroup->pGrantContactInProgressField) {
			pGroup->pGrantContactInProgressField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pGrantContact), pMission->zoneDesc.grantDescription.pGrantContact, parse_GenesisMissionGrant_Contact, "InProgressText");
			GMDAddFieldToParent(pGroup->pGrantContactInProgressField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pGrantContactInProgressField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pGrantContactInProgressField, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pGrantContact), pMission->zoneDesc.grantDescription.pGrantContact);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pGrantContactOfferLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pGrantContactInProgressLabel);
		MEFieldSafeDestroy(&pGroup->pGrantContactOfferField);
		MEFieldSafeDestroy(&pGroup->pGrantContactInProgressField);
	}
	
	// Update turn in
	pGroup->pTurnInLabel = GMDRefreshLabel(pGroup->pTurnInLabel, "Turn In Type", "How the mission is turned in.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pTurnInField) {
		pGroup->pTurnInField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "TurnInType", GenesisMissionTurnInTypeEnum);
		GMDAddFieldToParent(pGroup->pTurnInField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 140, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pTurnInField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pTurnInField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription);
	}

	y += STANDARD_ROW_HEIGHT;

	if (pMission->zoneDesc.grantDescription.eTurnInType == GenesisMissionTurnInType_GrantingContact || pMission->zoneDesc.grantDescription.eTurnInType == GenesisMissionTurnInType_DifferentContact) {
		if (!pMission->zoneDesc.grantDescription.pTurnInContact) {
			pMission->zoneDesc.grantDescription.pTurnInContact = StructCreate( parse_GenesisMissionTurnIn_Contact );
		}

		// Mission return text
		pGroup->pTurnInContactCompletedLabel = GMDRefreshLabel(pGroup->pTurnInContactCompletedLabel, "Completed", "Contact's dialog when player returns after completing mission.", X_OFFSET_BASE+X_OFFSET_INDENT, 0, y, pExpander);
		if (!pGroup->pTurnInContactCompletedField) {
			pGroup->pTurnInContactCompletedField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pTurnInContact), pMission->zoneDesc.grantDescription.pTurnInContact, parse_GenesisMissionTurnIn_Contact, "CompletedText");
			GMDAddFieldToParent(pGroup->pTurnInContactCompletedField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pTurnInContactCompletedField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pTurnInContactCompletedField, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pTurnInContact), pMission->zoneDesc.grantDescription.pTurnInContact);
		}

		y += STANDARD_ROW_HEIGHT;

		// Mission's Return to contact text
		pGroup->pTurnInContactMissionReturnLabel = GMDRefreshLabel(pGroup->pTurnInContactMissionReturnLabel, "Mission Return", "Mission text instructing player to return the mission and complete it.", X_OFFSET_BASE+X_OFFSET_INDENT, 0, y, pExpander);
		if (!pGroup->pTurnInContactMissionReturnField) {
			pGroup->pTurnInContactMissionReturnField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pTurnInContact), pMission->zoneDesc.grantDescription.pTurnInContact, parse_GenesisMissionTurnIn_Contact, "MissionReturnText");
			GMDAddFieldToParent(pGroup->pTurnInContactMissionReturnField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
		} else {
			ui_WidgetSetPosition(pGroup->pTurnInContactMissionReturnField->pUIWidget, X_OFFSET_CONTROL, y);
			MEFieldSetAndRefreshFromData(pGroup->pTurnInContactMissionReturnField, SAFE_MEMBER(pOrigMission, zoneDesc.grantDescription.pTurnInContact), pMission->zoneDesc.grantDescription.pTurnInContact);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pTurnInContactCompletedLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pTurnInContactMissionReturnLabel);
		MEFieldSafeDestroy(&pGroup->pTurnInContactCompletedField);
		MEFieldSafeDestroy(&pGroup->pTurnInContactMissionReturnField);
	}

	pGroup->pFailTimeoutSecondsLabel = GMDRefreshLabel(pGroup->pFailTimeoutSecondsLabel, "Timeout", "Time in seconds before the mission fails.  If zero, the mission will never fail.", X_OFFSET_BASE, 0, y, pExpander);
	if (!pGroup->pFailTimeoutSecondsField) {
		pGroup->pFailTimeoutSecondsField = MEFieldCreateSimple(kMEFieldType_TextEntry, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "FailTimeoutSeconds");
		GMDAddFieldToParent(pGroup->pFailTimeoutSecondsField, UI_WIDGET(pExpander), X_OFFSET_CONTROL, y, 0, 55, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pFailTimeoutSecondsField->pUIWidget, X_OFFSET_CONTROL, y);
		MEFieldSetAndRefreshFromData(pGroup->pFailTimeoutSecondsField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription);
	}
	
	if (pMission->zoneDesc.grantDescription.iFailTimeoutSeconds > 0)
	{
		pMission->zoneDesc.grantDescription.eFailType = GenesisMissionFailType_Timeout;
	}
	else
	{
		pMission->zoneDesc.grantDescription.eFailType = GenesisMissionFailType_Never;
	}

	y += STANDARD_ROW_HEIGHT;

	MEExpanderRefreshLabel(&pGroup->pCanRepeatLabel, "Repeatable", "Whether or not the mission can be repeated.", X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshSimpleField( &pGroup->pCanRepeatField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "CanRepeat", kMEFieldType_BooleanCombo,
								  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5,
								  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;
	
	if( pMission->zoneDesc.grantDescription.bRepeatable ) {
		MEExpanderRefreshLabel(&pGroup->pRepeatCooldownsLabel, "Repeat Cooldowns:", NULL, X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
		y += STANDARD_ROW_HEIGHT;
		
		MEExpanderRefreshLabel(&pGroup->pRepeatCooldownHoursLabel, "Since Completed", "How much time must pass since the last time the mission was completed before it can be repeated (hours)", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pExpander));
		MEExpanderRefreshSimpleField( &pGroup->pRepeatCooldownHoursField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "RepeatCooldownHours", kMEFieldType_TextEntry,
									  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
		
		MEExpanderRefreshLabel(&pGroup->pRepeatCooldownHoursFromStartLabel, "Since Started", "How much time must pass since the last time the mission was completed before it can be repeated (hours)", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pExpander));
		MEExpanderRefreshSimpleField( &pGroup->pRepeatCooldownHoursFromStartField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "RepeatCooldownHoursFromStart", kMEFieldType_TextEntry,
									  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5,
									  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
		
		MEExpanderRefreshLabel(&pGroup->pRepeatRepeatCooldownCountLabel, "Repeat Count Max", "How how many times can mission be completed in cooldown window.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pExpander));
		MEExpanderRefreshSimpleField( &pGroup->pRepeatRepeatCooldownCountField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "RepeatCooldownCount", kMEFieldType_TextEntry,
			UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 60, UIUnitFixed, 5,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;

		MEExpanderRefreshLabel(&pGroup->pRepeatCooldownBlockTimeLabel, "Repeat Blocktime", "If true cooldown blocks are in fixed blocks of time. Example 24 hour cooldown would always start at 12:00am.", X_OFFSET_BASE + X_OFFSET_INDENT, 0, y, UI_WIDGET(pExpander));
		MEExpanderRefreshSimpleField( &pGroup->pRepeatCooldownBlockTimeField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "RepeatCooldownBlockTime", kMEFieldType_BooleanCombo,
			UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 80, UIUnitFixed, 5,
			GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
		y += STANDARD_ROW_HEIGHT;
		
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pRepeatCooldownsLabel);
		ui_WidgetQueueFreeAndNull(&pGroup->pRepeatCooldownHoursLabel);
		MEFieldSafeDestroy(&pGroup->pRepeatCooldownHoursField);
		ui_WidgetQueueFreeAndNull(&pGroup->pRepeatCooldownHoursFromStartLabel);
		MEFieldSafeDestroy(&pGroup->pRepeatCooldownHoursFromStartField);
		ui_WidgetQueueFreeAndNull(&pGroup->pRepeatRepeatCooldownCountLabel);
		MEFieldSafeDestroy(&pGroup->pRepeatRepeatCooldownCountField);
		ui_WidgetQueueFreeAndNull(&pGroup->pRepeatCooldownBlockTimeLabel);
		MEFieldSafeDestroy(&pGroup->pRepeatCooldownBlockTimeField);
	}

	MEExpanderRefreshLabel(&pGroup->pRequiresMissionsLabel, "Requires: Missions", "Missions that must be completed before this mission can be granted", X_OFFSET_BASE, 0, y, UI_WIDGET(pExpander));
	MEExpanderRefreshGlobalDictionaryField( &pGroup->pRequiresMissionsField, SAFE_MEMBER_ADDR(pOrigMission, zoneDesc.grantDescription), &pMission->zoneDesc.grantDescription, parse_GenesisMissionGrantDescription, "RequiresMission", "Mission",
											UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL, y, 0, 1.0, UIUnitPercentage, 5,
											GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;
	
	// Set the expander height
	ui_ExpanderSetHeight(pGroup->pExpander, y);
}

static void GMDActiveObjectiveClone( void* ignored1, GMDObjectiveGroup* pGroup )
{
	GenesisMissionObjective *pNewObjective;
	GenesisMissionObjective *pObjective;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pObjective = (*pGroup->peaObjectives)[pGroup->index];

	// Perform the operation
	pNewObjective = StructClone( parse_GenesisMissionObjective, pObjective );
	GMDObjectiveUniquifyName( *pGroup->peaObjectives, pNewObjective );
	eaInsert( pGroup->peaObjectives, pNewObjective, pGroup->index + 1 );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveObjectiveCut( void* ignored1, GMDObjectiveGroup* pGroup )
{
	GenesisMissionObjective *pObjective;
	MapDescEditDoc* pDoc;

	pDoc = pGroup->pDoc;
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pObjective = (*pGroup->peaObjectives)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionObjective, pObjective, &g_GMDClipboardObjective );
	StructDestroy( parse_GenesisMissionObjective, pObjective );
	eaRemove( pGroup->peaObjectives, pGroup->index );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}

static void GMDActiveObjectiveCopy( void* ignored1, GMDObjectiveGroup* pGroup )
{
	GenesisMissionObjective *pObjective;

	if( !pGroup ) {
		return;
	}
	pObjective = (*pGroup->peaObjectives)[pGroup->index];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionObjective, pObjective, &g_GMDClipboardObjective );
}

static void GMDActiveObjectivePaste( void* ignored1, GMDObjectiveGroup* pGroup )
{
	GenesisMissionObjective *pObjective;
	MapDescEditDoc* pDoc;

	if( !pGroup ) {
		return;
	}
	pDoc = pGroup->pDoc;
	
	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pObjective = (*pGroup->peaObjectives)[pGroup->index];
	
	// Perform the operation
	StructCopyAll( parse_GenesisMissionObjective, &g_GMDClipboardObjective, pObjective );
	
	// Refresh the UI
	GMDMapDescChanged(pGroup->pDoc, true);
}


static F32 GMDRefreshObjective(MapDescEditDoc *pDoc, F32 y, int iIndent, GMDObjectiveGroup *pGroup, int index, GenesisMissionObjective ***peaObjectives, GenesisMissionObjective *pOrigObjective, GenesisMissionObjective *pObjective)
{
	char buf[256];
	int i;
	int iNumObjectives = 0;
	F32 fIndent = iIndent * 30;

	// Refresh the group
	pGroup->peaObjectives = peaObjectives;
	pGroup->iIndent = iIndent;

	// Update expander
	sprintf(buf, "%s%sObjective: %s",
			pObjective->bOptional ? "Optional " : "",
			iIndent != 0 ? "Sub-" : "",
			pObjective->pcName );
	if (iIndent == 0) {
		ui_WidgetSetTextString(UI_WIDGET(pGroup->pExpander), buf);
	} else {
		pGroup->pTitleLabel = GMDRefreshLabel(pGroup->pTitleLabel, buf, NULL, X_OFFSET_BASE+fIndent, 0, y, pGroup->pExpander);
		ui_WidgetSkin(UI_WIDGET(pGroup->pTitleLabel), gBoldExpanderSkin);
		y += STANDARD_ROW_HEIGHT;
	}
	
	// Add popup menu
	if( !pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Up", UIMenuCallback, GMDUpObjective, pGroup, NULL ),
				ui_MenuItemCreate("Down", UIMenuCallback, GMDDownObjective, pGroup, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, GMDRemoveObjective, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, GMDActiveObjectiveClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, GMDActiveObjectiveCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, GMDActiveObjectiveCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, GMDActiveObjectivePaste, pGroup, NULL ),
				NULL );
	}
	if( pGroup->iIndent == 0 ) {
		ui_ExpanderAddLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	} else {
		ui_ExpanderRemoveLabel( pGroup->pExpander, UI_WIDGET( pGroup->pPopupMenuButton ));
	}
	ui_WidgetSetPositionEx( UI_WIDGET(pGroup->pPopupMenuButton), 4, 2, 0, 0, UITopRight );

	// Update name
	pGroup->pNameLabel = GMDRefreshLabel(pGroup->pNameLabel, "Name", "The name of the objective.", X_OFFSET_BASE+fIndent, 0, y, pGroup->pExpander);
	if (!pGroup->pNameField) {
		pGroup->pNameField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigObjective, pObjective, parse_GenesisMissionObjective, "Name");
		GMDAddFieldToParent(pGroup->pNameField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+fIndent, y, 0, 1.0, UIUnitPercentage,
							4, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pNameField->pUIWidget, X_OFFSET_CONTROL+fIndent, y);
		MEFieldSetAndRefreshFromData(pGroup->pNameField, pOrigObjective, pObjective);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update short text
	pGroup->pShortTextLabel = GMDRefreshLabel(pGroup->pShortTextLabel, "UI String", "The text for the objective to show on the HUD.", X_OFFSET_BASE+fIndent, 0, y, pGroup->pExpander);
	if (!pGroup->pShortTextField) {
		pGroup->pShortTextField = MEFieldCreateSimple(kMEFieldType_MultiText, pOrigObjective, pObjective, parse_GenesisMissionObjective, "ShortText");
		GMDAddFieldToParent(pGroup->pShortTextField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+fIndent, y, 0, 1.0, UIUnitPercentage, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pShortTextField->pUIWidget, X_OFFSET_CONTROL+fIndent, y);
		MEFieldSetAndRefreshFromData(pGroup->pShortTextField, pOrigObjective, pObjective);
	}

	y += STANDARD_ROW_HEIGHT;

	// Update optional-ness
	pGroup->pOptionalLabel = GMDRefreshLabel(pGroup->pOptionalLabel, "Optional", "If the objective is optional.  Make sure you place optional objectives before other objectives granted at the same time.  In particular, do not place optional objectives at the end of the mission.", X_OFFSET_BASE + fIndent, 0, y, pGroup->pExpander );
	if (!pGroup->pOptionalField) {
		pGroup->pOptionalField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pOrigObjective, pObjective, parse_GenesisMissionObjective, "Optional");
		GMDAddFieldToParent(pGroup->pOptionalField, UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL+fIndent, y, 0, 80, UIUnitFixed, 5, pDoc);
	} else {
		ui_WidgetSetPosition(pGroup->pOptionalField->pUIWidget, X_OFFSET_CONTROL+fIndent, y);
		MEFieldSetAndRefreshFromData(pGroup->pOptionalField, pOrigObjective, pObjective);
	}
	y += STANDARD_ROW_HEIGHT;

	// Update succeed when
	if (!pGroup->pWhenGroup) {
		pGroup->pWhenGroup = calloc( 1, sizeof( *pGroup->pWhenGroup ));
	}
	y = GMDRefreshWhen(pDoc, pGroup->pWhenGroup, pGroup->pExpander, "Type", "The type of the objective.", fIndent, y, SAFE_MEMBER_ADDR(pOrigObjective, succeedWhen), &pObjective->succeedWhen, true);

	// Timeout
	pGroup->pTimeoutLabel = GMDRefreshLabel(pGroup->pTimeoutLabel, "Timer", "Time in seconds until the objective auto-completes.  If set to zero, then never autocomplete", X_OFFSET_BASE + fIndent, 0, y, pGroup->pExpander );
	MEExpanderRefreshSimpleField( &pGroup->pTimeoutField, pOrigObjective, pObjective, parse_GenesisMissionObjective, "TimeToComplete", kMEFieldType_TextEntry,
								  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL + fIndent, y, 0, 60, UIUnitFixed, 5,
								  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Show Waypoints
	MEExpanderRefreshLabel( &pGroup->pShowWaypointsLabel, "Show Waypoints", "If true, show waypoints for this objective.  Waypoints will be automatically deduced.", X_OFFSET_BASE + fIndent, 0, y, UI_WIDGET(pGroup->pExpander) );
	MEExpanderRefreshSimpleField( &pGroup->pShowWaypointsField, pOrigObjective, pObjective, parse_GenesisMissionObjective, "ShowWaypoints", kMEFieldType_BooleanCombo,
								  UI_WIDGET(pGroup->pExpander), X_OFFSET_CONTROL + fIndent, y, 0, 80, UIUnitFixed, 5,
								  GMDFieldChangedCB, GMDFieldPreChangeCB, pDoc );
	y += STANDARD_ROW_HEIGHT;

	// Remove unused objective groups
	if (  (pObjective->succeedWhen.type == GenesisWhen_AllOf) || (pObjective->succeedWhen.type == GenesisWhen_InOrder)
		  || (pObjective->succeedWhen.type == GenesisWhen_Branch)) {
		iNumObjectives = eaSize(&pObjective->eaChildren);
	} else {
		iNumObjectives = 0;
	}
	for(i=eaSize(&pGroup->eaSubGroups)-1; i>=iNumObjectives; --i) {
		assert(pGroup->eaSubGroups);
		GMDFreeObjectiveGroup(pGroup->eaSubGroups[i]);
		eaRemove(&pGroup->eaSubGroups, i);
	}

	// Refresh objectives
	for(i=0; i<iNumObjectives; ++i) {
		GenesisMissionObjective *pChildObjective = pObjective->eaChildren[i];
		GenesisMissionObjective *pOrigChildObjective = NULL;

		if (eaSize(&pGroup->eaSubGroups) <= i) {
			GMDObjectiveGroup *pSubGroup = calloc(1, sizeof(GMDObjectiveGroup));
			pSubGroup->pDoc = pDoc;
			pSubGroup->pExpander = pGroup->pExpander;
			eaPush(&pGroup->eaSubGroups, pSubGroup);
		}
		pGroup->eaSubGroups[i]->index = i;
		
		if (pOrigObjective && eaSize(&pOrigObjective->eaChildren) > i) {
			pOrigChildObjective = pOrigObjective->eaChildren[i];
		}

		y = GMDRefreshObjective(pDoc, y, iIndent+1, pGroup->eaSubGroups[i], i, &pObjective->eaChildren, pOrigChildObjective, pChildObjective);
	}

	if (  (pObjective->succeedWhen.type == GenesisWhen_AllOf) || (pObjective->succeedWhen.type == GenesisWhen_InOrder)
		  || (pObjective->succeedWhen.type == GenesisWhen_Branch)) {
		// Update add child button
		if (!pGroup->pAddChildButton) {
			pGroup->pAddChildButton = ui_ButtonCreate("Add Child Objective", X_OFFSET_BASE+fIndent, y, GMDAddSubObjective, pGroup);
			ui_WidgetSetWidth(UI_WIDGET(pGroup->pAddChildButton), 140);
			ui_ExpanderAddChild(pGroup->pExpander, pGroup->pAddChildButton);
		} else {
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pAddChildButton), X_OFFSET_BASE+fIndent, y);
		}

		y += STANDARD_ROW_HEIGHT;
	} else {
		ui_WidgetQueueFreeAndNull(&pGroup->pAddChildButton);
	}

	GMDRefreshButtonSet(pGroup->pExpander, fIndent, y, index > 0, index < eaSize(peaObjectives)-1, "Del Objective", &pGroup->pRemoveButton, GMDRemoveObjective, &pGroup->pUpButton, GMDUpObjective, &pGroup->pDownButton, GMDDownObjective, pGroup);

	y += STANDARD_ROW_HEIGHT;

	return y;
}


static void GMDRefreshObjectives(MapDescEditDoc *pDoc)
{
	GenesisMissionDescription *pMission = NULL;
	GenesisMissionDescription *pOrigMission = NULL;
	int iNumObjectives;
	int i;
	F32 y;

	// Make sure a mission is present
	GMDEnsureMission(pDoc, &pMission, &pOrigMission);

	// Remove unused objective groups
	iNumObjectives = eaSize(&pMission->zoneDesc.eaObjectives);
	for(i=eaSize(&pDoc->eaObjectiveGroups)-1; i>=iNumObjectives; --i) {
		GMDFreeObjectiveGroup(pDoc->eaObjectiveGroups[i]);
		eaRemove(&pDoc->eaObjectiveGroups, i);
	}

	// Refresh objectives
	for(i=0; i<iNumObjectives; ++i) {
		GenesisMissionObjective *pObjective = pMission->zoneDesc.eaObjectives[i];
		GenesisMissionObjective *pOrigObjective = NULL;

		if (eaSize(&pDoc->eaObjectiveGroups) <= i) {
			GMDObjectiveGroup *pGroup = calloc(1, sizeof(GMDObjectiveGroup));
			pGroup->pExpander = GMDCreateExpander(pDoc->pObjectiveExpanderGroup, "Objective", i+2);
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaObjectiveGroups, pGroup);
		}
		pDoc->eaObjectiveGroups[i]->index = i;
		
		if (pOrigMission && eaSize(&pOrigMission->zoneDesc.eaObjectives) > i) {
			pOrigObjective = pOrigMission->zoneDesc.eaObjectives[i];
		}

		y = GMDRefreshObjective(pDoc, 0, 0, pDoc->eaObjectiveGroups[i], i, &pMission->zoneDesc.eaObjectives, pOrigObjective, pObjective);

		// Set the expander height
		ui_ExpanderSetHeight(pDoc->eaObjectiveGroups[i]->pExpander, y);
	}
}


static void GMDUpdateDisplay(MapDescEditDoc *pDoc)
{
	int i;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	//Update Layout Pointers
	GMDUpdateSelectedLayout(pDoc);

	// Refresh data
	GMDRefreshData(pDoc);

	// Refresh doc-level fields
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigMapDesc, pDoc->pMapDesc);
	}

	if (pDoc->EditingMapType != GenesisMapType_SolarSystem) {
		ui_ButtonSetCallback(pDoc->pAddLayout1Button, GMDAddRoom, pDoc);
		ui_ButtonSetText(pDoc->pAddLayout1Button, "Add Room");

		ui_ButtonSetCallback(pDoc->pAddLayout2Button, GMDAddPath, pDoc);
		ui_ButtonSetText(pDoc->pAddLayout2Button, "Add Path");
		ui_WindowAddChild(pDoc->pMainWindow, UI_WIDGET(pDoc->pAddLayout2Button));
	} else {
		ui_ButtonSetCallback(pDoc->pAddLayout1Button, GMDAddPointList, pDoc);
		ui_ButtonSetText(pDoc->pAddLayout1Button, "Add List");
		
		ui_WidgetRemoveFromGroup(UI_WIDGET(pDoc->pAddLayout2Button));
	}

	{
		int it;
		for( it = 0; it != eaSize( &pDoc->eaMissionNames ); ++it ) {
			if( stricmp( pDoc->eaMissionNames[ it ], pDoc->pMapDesc->missions[ pDoc->iCurrentMission ]->zoneDesc.pcName ) == 0 ) {
				ui_ComboBoxSetSelected( pDoc->pMissionCombo, it );
				break;
			}
		}
	}

	// Refresh the dynamic expanders
	GMDRefreshLayoutInfo(pDoc);
	GMDRefreshShoebox(pDoc);
	GMDRefreshRoomsAndPaths(pDoc);
	GMDRefreshListsAndPoints(pDoc);
	GMDRefreshChallengeStart(pDoc);
	GMDRefreshChallenges(pDoc);
	GMDRefreshPrompts(pDoc);
	GMDRefreshPortals(pDoc);
	GMDRefreshMissionInfo(pDoc);
	GMDRefreshMissionStart(pDoc);
	GMDRefreshObjectives(pDoc);

	if (!pDoc->bEmbeddedMode) {
		// Update non-field UI components
		ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pMapDesc->name);
		ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pMapDesc);
		ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pMapDesc->filename);
	}

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigMapDesc && (StructCompare(parse_GenesisMapDescription, pDoc->pOrigMapDesc, pDoc->pMapDesc, 0, 0, 0) == 0);

	if (pDoc->bEmbeddedMode) {
		// Update buttons
		if (pDoc->emDoc.saved) {
			ui_SetActive(UI_WIDGET(pDoc->pReseedButton), true);
			ui_ButtonSetText(pDoc->pSaveButton, "Reseed Detail");
			ui_ButtonSetCallback(pDoc->pSaveButton, GMDReseedDetailEmbedded, pDoc);
			ui_ButtonSetText(pDoc->pCloseButton, "Close");
		} else {
			ui_SetActive(UI_WIDGET(pDoc->pReseedButton), false);
			ui_ButtonSetText(pDoc->pSaveButton, "Apply Changes");
			ui_ButtonSetCallback(pDoc->pSaveButton, GMDSaveEmbedded, pDoc);
			ui_ButtonSetText(pDoc->pCloseButton, "Cancel");
		}
	}

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = false;
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------

static void GMDMissionUniquifyName(GenesisMissionDescription** eaMissions, GenesisMissionDescription* pMission)
{
	char bufSansNumbers[256];
	char buf[256];
	int iNum = 0;
	int i;

	strcpy(bufSansNumbers, pMission->zoneDesc.pcName);
	i = (int)strlen(bufSansNumbers) - 1;
	while( i >= 0 && isdigit(bufSansNumbers[i])) {
		bufSansNumbers[i] = '\0';
		--i;
	}
	
	while(true) {
		if( iNum > 0 ) {
			sprintf(buf, "%s%d", bufSansNumbers, iNum);
		} else {
			strcpy( buf, pMission->zoneDesc.pcName );
		}
		
		for(i=eaSize(&eaMissions)-1; i>=0; --i) {
			if (eaMissions[i] != pMission && stricmp(buf, eaMissions[i]->zoneDesc.pcName) == 0) {
				break;
			}
		}
		if (i < 0) {
			break;
		}
		++iNum;
	}

	StructCopyString( &pMission->zoneDesc.pcName, buf );
}

static void GMDActiveMissionClone( void* ignored1, MapDescEditDoc* pDoc )
{
	GenesisMissionDescription *pNewMission;
	GenesisMissionDescription *pMission;

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}
	pMission = pDoc->pMapDesc->missions[ pDoc->iCurrentMission ];

	// Perform the operation
	pNewMission = StructClone( parse_GenesisMissionDescription, pMission );
	GMDMissionUniquifyName( pDoc->pMapDesc->missions, pNewMission );
	eaInsert( &pDoc->pMapDesc->missions, pNewMission, pDoc->iCurrentMission + 1 );
	
	// Refresh the UI
	GMDMapDescChanged( pDoc, true);
}

static void GMDActiveMissionCut( void* ignored1, MapDescEditDoc* pDoc )
{
	GenesisMissionDescription* pMission = pDoc->pMapDesc->missions[ pDoc->iCurrentMission ];

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Perform the operation
	StructCopyAll( parse_GenesisMissionDescription, pMission, &g_GMDClipboardMission );
	eaRemove( &pDoc->pMapDesc->missions, pDoc->iCurrentMission );
	StructDestroy( parse_GenesisMissionDescription, pMission );

	pDoc->iCurrentMission = MAX( 0, MIN( eaSize( &pDoc->pMapDesc->missions ) - 1, pDoc->iCurrentMission - 1 ));
	GMDEnsureMission(pDoc, NULL, NULL);
	
	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDActiveMissionCopy( void* ignored1, MapDescEditDoc* pDoc )
{
	GenesisMissionDescription* pMission = pDoc->pMapDesc->missions[ pDoc->iCurrentMission ];

	// Perform the operation
	StructCopyAll( parse_GenesisMissionDescription, pMission, &g_GMDClipboardMission );
}

static void GMDActiveMissionPaste( void* ignored1, MapDescEditDoc* pDoc )
{
	GenesisMissionDescription* pMission = pDoc->pMapDesc->missions[ pDoc->iCurrentMission ];

	if (!pDoc->bEmbeddedMode && !emDocIsEditable(&pDoc->emDoc, true)) {
		return;
	}

	// Perform the operation
	StructCopyAll( parse_GenesisMissionDescription, &g_GMDClipboardMission, pMission );
	
	// Refresh the UI
	GMDMapDescChanged(pDoc, true);
}

static void GMDExpanderGroupToggle( UIExpanderGroup* group )
{
	bool allCollapsed = true;
	
	int it;
	for( it = 0; it != eaSize( &group->childrenInOrder ); ++it ) {
		UIWidget* child = group->childrenInOrder[ it ];

		// if so, this must be an expander...
		if( child->tickF == ui_ExpanderTick ) {
			UIExpander* expander = (UIExpander*)child;

			allCollapsed = allCollapsed && !ui_ExpanderIsOpened( expander );
			ui_ExpanderSetOpened( expander, false );
		}
	}

	if( allCollapsed ) {
		for( it = 0; it != eaSize( &group->childrenInOrder ); ++it ) {
			UIWidget* child = group->childrenInOrder[ it ];

			// if so, this must be an expander...
			if( child->tickF == ui_ExpanderTick ) {
				UIExpander* expander = (UIExpander*)child;

				ui_ExpanderSetOpened( expander, true );
			}
		}
	}
}

static void GMDToggleLayout( void* ignored, MapDescEditDoc* pDoc )
{
	GMDExpanderGroupToggle( pDoc->pLayoutExpanderGroup );
	ui_ExpanderGroupReflow( pDoc->pLayoutExpanderGroup );
}

static void GMDToggleChallenges( void* ignored, MapDescEditDoc* pDoc )
{
	GMDExpanderGroupToggle( pDoc->pChallengeExpanderGroup );
	ui_ExpanderGroupReflow( pDoc->pChallengeExpanderGroup );
}

static void GMDToggleMission( void* ignored, MapDescEditDoc* pDoc )
{
	GMDExpanderGroupToggle( pDoc->pObjectiveExpanderGroup );
	ui_ExpanderGroupReflow( pDoc->pObjectiveExpanderGroup );
}

static void GMDTypeChangedCB( void* ignored, int new_type, MapDescEditDoc* pDoc )
{
	int i;
	if(pDoc->EditingMapType == new_type)
		return;

	if (pDoc->bEmbeddedMode && ui_ModalDialog(  "Confirm Map Type Change",
												"Changing map type can not be undone. "
												"All layouts, challenges, prompts, and portals will be deleted. "
												"\n"
												"Are you absolutely sure you want to change the map type?",
												ColorBlack, UIYes | UINo) == UINo)
	{
		ui_ComboBoxSetSelectedEnum(pDoc->pTypeCombo, pDoc->EditingMapType);
		GMDUpdateDisplay(pDoc);
		return;
	}

	eaDestroyStruct(&pDoc->pMapDesc->interior_layouts, parse_GenesisInteriorLayout);
	eaDestroyStruct(&pDoc->pMapDesc->solar_system_layouts, parse_GenesisSolSysLayout);
	StructDestroySafe(parse_GenesisExteriorLayout, &pDoc->pMapDesc->exterior_layout);
	for ( i=0; i < eaSize(&pDoc->pMapDesc->missions); i++ )
	{
		GenesisMissionDescription *pMission = pDoc->pMapDesc->missions[i];
		eaDestroyStruct(&pMission->eaChallenges, parse_GenesisMissionChallenge);
		eaDestroyStruct(&pMission->zoneDesc.eaPrompts, parse_GenesisMissionPrompt);
		eaDestroyStruct(&pMission->zoneDesc.eaPortals, parse_GenesisMissionPortal);		
	}
	eaDestroyStruct(&pDoc->pMapDesc->shared_challenges, parse_GenesisMissionChallenge);

	pDoc->EditingMapType = new_type;
	pDoc->iEditingLayoutIdx = 0;
	GMDAddLayoutByType(pDoc, pDoc->EditingMapType);

	GMDMapDescChanged(pDoc, true);
}

static UIWindow *GMDInitMainWindow(MapDescEditDoc *pDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	MEField *pField;
	UIButton *pButton;
	UIMenuButton *pMenuButton;
	UISeparator *pSeparator;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;

	// Create the window
	pWin = ui_WindowCreate(pDoc->bEmbeddedMode ? "Editing Map Description" : pDoc->pMapDesc->name, 15, 50, 950, 600);
	pWin->minW = 950;
	pWin->minH = 400;
	EditorPrefGetWindowPosition(MAPDESC_EDITOR, "Window Position", "Main", pWin);

	fBottomY += STANDARD_ROW_HEIGHT; 

	if (!pDoc->bEmbeddedMode) {
		// Name
		pLabel = ui_LabelCreate("Name", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "Name");
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
		MEFieldSetChangeCallback(pField, GMDSetNameCB, pDoc);
		eaPush(&pDoc->eaDocFields, pField);

		//Make Maps Button
		pButton = ui_ButtonCreate("Make Maps", 0, 0, GMDMakeMaps, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), 5, y, 0, 0, UITopRight);
		ui_WindowAddChild(pWin, pButton);

		y += STANDARD_ROW_HEIGHT;

		// Scope
		pLabel = ui_LabelCreate("Scope", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "Scope", NULL, &geaScopes, NULL);
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
		MEFieldSetChangeCallback(pField, GMDSetScopeCB, pDoc);
		eaPush(&pDoc->eaDocFields, pField);

		y += STANDARD_ROW_HEIGHT;

		// File Name
		pLabel = ui_LabelCreate("File Name", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "MapDescription", pDoc->pMapDesc->name, pDoc->pMapDesc);
		ui_WindowAddChild(pWin, pDoc->pFileButton);
		pLabel = ui_LabelCreate(pDoc->pMapDesc->filename, X_OFFSET_CONTROL+20, y);
		ui_WindowAddChild(pWin, pLabel);
		ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 0.4, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
		pDoc->pFilenameLabel = pLabel;

		y += STANDARD_ROW_HEIGHT;

		// Tracking
		pLabel = ui_LabelCreate("Enable Tracking", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "TrackingEnabled");
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 21, pDoc);
		eaPush(&pDoc->eaDocFields, pField);

		y += STANDARD_ROW_HEIGHT;

		// Comments
		pLabel = ui_LabelCreate("Comments", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "Comments");
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
		eaPush(&pDoc->eaDocFields, pField);

		y += STANDARD_ROW_HEIGHT;

		fTopY = MAX( fTopY, y );		
	} else {
		// Name
		pLabel = ui_LabelCreate("Source MapDesc", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "Name");
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
		ui_SetActive( pField->pUIWidget, false );
		eaPush(&pDoc->eaDocFields, pField);

		y += STANDARD_ROW_HEIGHT;

		// Tracking
		pLabel = ui_LabelCreate("Enable Tracking", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "TrackingEnabled");
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 100, UIUnitFixed, 21, pDoc);
		eaPush(&pDoc->eaDocFields, pField);

		y += STANDARD_ROW_HEIGHT;
		
		// Comments
		pLabel = ui_LabelCreate("Comments", 0, y);
		ui_WindowAddChild(pWin, pLabel);
		pField = MEFieldCreateSimple(kMEFieldType_MultiText, pDoc->pOrigMapDesc, pDoc->pMapDesc, parse_GenesisMapDescription, "Comments");
		GMDAddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
		eaPush(&pDoc->eaDocFields, pField);

		y += STANDARD_ROW_HEIGHT;

		fTopY = MAX( fTopY, y );
		
		ui_WindowSetCloseCallback(pWin, GMDWindowCloseCB, pDoc);

		pSeparator = ui_SeparatorCreate(UIHorizontal);
		ui_WidgetSetPositionEx(UI_WIDGET(pSeparator), 0, STANDARD_ROW_HEIGHT, 0, 0, UIBottomRight);
		ui_WindowAddChild(pWin, pSeparator);

		pDoc->pCloseButton = ui_ButtonCreate("Close", 0, 0, GMDCancelEmbedded, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pCloseButton), 120);
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pCloseButton), 0, 0, 0, 0, UIBottomRight);
		ui_WindowAddChild(pWin, pDoc->pCloseButton);

		pDoc->pSaveButton = ui_ButtonCreate("Reseed Detail", 130, 0, GMDReseedDetailEmbedded, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pSaveButton), 120);
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pSaveButton), 130, 0, 0, 0, UIBottomRight);
		ui_WindowAddChild(pWin, pDoc->pSaveButton);

		pDoc->pReseedButton = ui_ButtonCreate("Reseed All", 260, 0, GMDReseedEmbedded, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pDoc->pReseedButton), 120);
		ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pReseedButton), 260, 0, 0, 0, UIBottomRight);
		ui_WindowAddChild(pWin, pDoc->pReseedButton);

		fBottomY += STANDARD_ROW_HEIGHT + SEPARATOR_HEIGHT;
	}

	/// LAYOUT DEFINITION
	y = fTopY;

	pLabel = ui_LabelCreate("Layout Definition", 0, y);
	ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	ui_WindowAddChild(pWin, pLabel);

	y += LABEL_ROW_HEIGHT;

	// Layout selection
	{
		pLabel = ui_LabelCreate("Selected Layout", 0, y);
		ui_WindowAddChild(pWin, pLabel);

		{
			pDoc->pLayoutCombo = ui_ComboBoxCreate(X_OFFSET_CONTROL, y, 140, NULL, &pDoc->eaLayoutNames, NULL);
			ui_ComboBoxSetSelected( pDoc->pLayoutCombo, 0 );
			ui_WindowAddChild(pWin, pDoc->pLayoutCombo);
			ui_ComboBoxSetSelectedCallback(pDoc->pLayoutCombo, GMDSetCurrentLayout, pDoc);
		}

		pMenuButton = ui_MenuButtonCreate( X_OFFSET_CONTROL + 140 + 10, y );
		ui_MenuButtonAppendItems(
			pMenuButton,
			ui_MenuItemCreate("Add",	UIMenuCallback, GMDAddLayout, pDoc, NULL ),
			ui_MenuItemCreate("Delete", UIMenuCallback, GMDDeleteCurrentLayout, pDoc, NULL ),
			ui_MenuItemCreate("---",	UIMenuSeparator, NULL, NULL, NULL ),
			ui_MenuItemCreate("Clone",	UIMenuCallback, GMDActiveLayoutClone, pDoc, NULL ),
			ui_MenuItemCreate("Cut",	UIMenuCallback, GMDActiveLayoutCut, pDoc, NULL ),
			ui_MenuItemCreate("Copy",	UIMenuCallback, GMDActiveLayoutCopy, pDoc, NULL ),
			ui_MenuItemCreate("Paste",	UIMenuCallback, GMDActiveLayoutPaste, pDoc, NULL ),
			ui_MenuItemCreate("Reseed",	UIMenuCallback, GMDActiveLayoutReseed, pDoc, NULL ),
			NULL );
		ui_WindowAddChild(pWin, pMenuButton);

		y += STANDARD_ROW_HEIGHT;
	}

	// Layout Area
	pDoc->pLayoutExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pLayoutExpanderGroup), 0, y);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pLayoutExpanderGroup), 0, 0, 0, fBottomY + STANDARD_ROW_HEIGHT);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pLayoutExpanderGroup), 0.3, 1.0, UIUnitPercentage, UIUnitPercentage);
	UI_SET_STYLE_BORDER_NAME(pDoc->pLayoutExpanderGroup->hBorder, "Default_MiniFrame_Empty");
	ui_WindowAddChild(pWin, pDoc->pLayoutExpanderGroup);

	pDoc->pLayoutInfoGroup = calloc(1, sizeof(GMDLayoutInfoGroup));
	pDoc->pLayoutInfoGroup->pExpander = GMDCreateExpander(pDoc->pLayoutExpanderGroup, "Layout Info", 0);

	pDoc->pAddLayout1Button = ui_ButtonCreate("Add Layout1", 0, 0, NULL, NULL);
	ui_WidgetSetWidth(UI_WIDGET(pDoc->pAddLayout1Button), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pAddLayout1Button), 5, fBottomY, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pDoc->pAddLayout1Button);

	pDoc->pAddLayout2Button = ui_ButtonCreate("Add Layout2", 0, 0, NULL, NULL);
	ui_WidgetSetWidth(UI_WIDGET(pDoc->pAddLayout2Button), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pAddLayout2Button), 115, fBottomY, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pDoc->pAddLayout2Button);
	
	pButton = ui_ButtonCreate("Collapse", 0, 0, GMDToggleLayout, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 60);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -60, fBottomY, 0.3, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);


	y = 0;

	// Map Type control
	pLabel = ui_LabelCreate("Map Type", 0, y);
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 10, y, 0.4, 0, UITopLeft );
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pTypeCombo = ui_ComboBoxCreateWithEnum(X_OFFSET_CONTROL, y, 140, GenesisMapTypeEnum, GMDTypeChangedCB, pDoc);
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pTypeCombo), 80, y, 0.4, 0, UITopLeft );
	ui_ComboBoxEnumRemoveValueInt(pDoc->pTypeCombo, GenesisMapType_MiniSolarSystem);
	ui_ComboBoxEnumRemoveValueInt(pDoc->pTypeCombo, GenesisMapType_None);	
	ui_WindowAddChild(pWin, pDoc->pTypeCombo);
	GMDUpdateSelectedLayout(pDoc);

	y = fTopY;


	pLabel = ui_LabelCreate("Challenges & Prompts", 0, y);
	ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 10, y, 0.3, 0, UITopLeft);
	ui_WindowAddChild(pWin, pLabel);

	y += LABEL_ROW_HEIGHT;

	// Challenge Area
	pDoc->pChallengeExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pChallengeExpanderGroup), 10, y, 0.3, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pChallengeExpanderGroup), 0, 0, 0, fBottomY + STANDARD_ROW_HEIGHT);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pChallengeExpanderGroup), 0.3, 1.0, UIUnitPercentage, UIUnitPercentage);
	UI_SET_STYLE_BORDER_NAME(pDoc->pChallengeExpanderGroup->hBorder, "Default_MiniFrame_Empty");
	ui_WindowAddChild(pWin, pDoc->pChallengeExpanderGroup);

	pDoc->pChallengeStartGroup = calloc(1, sizeof(GMDChallengeStartGroup));
	pDoc->pChallengeStartGroup->pExpander = GMDCreateExpander(pDoc->pChallengeExpanderGroup, "Start and Exit Detail", 0);
	pDoc->pChallengeStartGroup->pDoc = pDoc;

	pButton = ui_ButtonCreate("Add Challenge", 0, 0, GMDAddChallenge, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 15, fBottomY, 0.3, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Add Prompt", 0, 0, GMDAddPrompt, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 125, fBottomY, 0.3, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Add Portal", 0, 0, GMDAddPortal, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 15, fBottomY - STANDARD_ROW_HEIGHT, 0.3, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Collapse", 0, 0, GMDToggleChallenges, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 60);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -60, fBottomY, 0.6, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	y = fTopY;
	
	pLabel = ui_LabelCreate("Mission Definition", 0, y);
	ui_LabelSetFont(pLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 10, y, 0.6, 0, UITopLeft);
	ui_WindowAddChild(pWin, pLabel);

	y += LABEL_ROW_HEIGHT;

	// Mission selection
	{
		pLabel = ui_LabelCreate("Selected Mission", 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel), 10, y, 0.6, 0, UITopLeft );
		ui_WindowAddChild(pWin, pLabel);

		{
			pDoc->pMissionCombo = ui_ComboBoxCreate(0, y, 200, NULL, &pDoc->eaMissionNames, NULL);
			ui_ComboBoxSetSelected( pDoc->pMissionCombo, 0 );
			ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pMissionCombo), 10 + X_OFFSET_CONTROL, y, 0.6, 0, UITopLeft);
			ui_WindowAddChild(pWin, pDoc->pMissionCombo);
			ui_ComboBoxSetSelectedCallback(pDoc->pMissionCombo, GMDSetCurrentMission, pDoc);
		}

		pMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
			pMenuButton,
			ui_MenuItemCreate("Add",	UIMenuCallback, GMDAddMission, pDoc, NULL ),
			ui_MenuItemCreate("Delete", UIMenuCallback, GMDDeleteCurrentMission, pDoc, NULL ),
			ui_MenuItemCreate("---",	UIMenuSeparator, NULL, NULL, NULL),
			ui_MenuItemCreate("Clone",	UIMenuCallback, GMDActiveMissionClone, pDoc, NULL ),
			ui_MenuItemCreate("Cut",	UIMenuCallback, GMDActiveMissionCut, pDoc, NULL ),
			ui_MenuItemCreate("Copy",	UIMenuCallback, GMDActiveMissionCopy, pDoc, NULL ),
			ui_MenuItemCreate("Paste",	UIMenuCallback, GMDActiveMissionPaste, pDoc, NULL ),
			NULL );
		ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 10 + X_OFFSET_CONTROL + 200 + 10, y, 0.6, 0, UITopLeft );
		ui_WindowAddChild(pWin, pMenuButton);

		y += STANDARD_ROW_HEIGHT;
	}

	// Objective Area
	pDoc->pObjectiveExpanderGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetPositionEx(UI_WIDGET(pDoc->pObjectiveExpanderGroup), 10, y, 0.6, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDoc->pObjectiveExpanderGroup), 0, 0, 0, fBottomY + STANDARD_ROW_HEIGHT);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pObjectiveExpanderGroup), 0.4, 1.0, UIUnitPercentage, UIUnitPercentage);
	UI_SET_STYLE_BORDER_NAME(pDoc->pObjectiveExpanderGroup->hBorder, "Default_MiniFrame_Empty");
	ui_WindowAddChild(pWin, pDoc->pObjectiveExpanderGroup);

	pDoc->pMissionInfoGroup = calloc(1, sizeof(GMDMissionInfoGroup));
	pDoc->pMissionInfoGroup->pExpander = GMDCreateExpander(pDoc->pObjectiveExpanderGroup, "Mission Info", 0);

	pDoc->pMissionStartGroup = calloc(1, sizeof(GMDMissionStartGroup));
	pDoc->pMissionStartGroup->pExpander = GMDCreateExpander(pDoc->pObjectiveExpanderGroup, "Mission Grant and Turn In", 1);

	pButton = ui_ButtonCreate("Add Objective", 0, 0, GMDAddObjective, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 15, fBottomY, 0.6, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	pButton = ui_ButtonCreate("Collapse", 0, 0, GMDToggleMission, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 60);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -60, fBottomY, 1.0, 0, UIBottomLeft);
	ui_WindowAddChild(pWin, pButton);

	return pWin;
}


static void GMDInitDisplay(EMEditor *pEditor, MapDescEditDoc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = GMDInitMainWindow(pDoc);
	pDoc->bIgnoreFieldChanges = false;
	pDoc->bIgnoreFilenameChanges = false;

	// Show the window
	ui_WindowPresent(pDoc->pMainWindow);

	if (!pDoc->bEmbeddedMode) {
		// Editor Manager needs to be told about the windows used
		pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
		eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);
	}

	// Update the rest of the UI
	GMDUpdateDisplay(pDoc);
}


static void GMDInitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "gmd_revertmapdesc", "Revert", NULL, NULL, "GMD_RevertMapDesc");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "gmd_revertmapdesc", NULL));
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

void GMDInitData(EMEditor *pEditor)
{
	if (pEditor && !gInitializedEditor) {
		GMDInitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "MapDescription", true, NULL, NULL, NULL, NULL, NULL);

		resGetUniqueScopes(g_MapDescDictionary, &geaScopes);

		gInitializedEditor = true;

		// Request all guild stat defs from the server
		resRequestAllResourcesInDictionary("GuildStatDef");
	}

	if (!gInitializedEditorData) {
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);

		// Load the tag lists
		GMDRefreshTagLists();

		eaClear( &geaOptionalActionCategories );
		{
			int it;
			for( it = 0; it != eaSize( &g_eaOptionalActionCategoryDefs ); ++it ) {
				eaPush( &geaOptionalActionCategories, g_eaOptionalActionCategoryDefs[it]->pcName );
			}
			eaPush( &geaOptionalActionCategories, allocAddString( "None" ));
		}

		// Make sure lists refresh if dictionary changes
		resDictRegisterEventCallback(g_MapDescDictionary, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_GEOTYPE_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_ECOTYPE_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_INTERIORS_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_ROOM_DEF_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_PATH_DEF_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_BACKDROP_FILE_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(GENESIS_DETAIL_DICTIONARY, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback(OBJECT_LIBRARY_DICT, GMDContentDictChanged, NULL);
		resDictRegisterEventCallback("InteractionDef", GMDContentDictChanged, NULL);
		resDictRegisterEventCallback("AIAnimList", GMDContentDictChanged, NULL);

		gInitializedEditorData = true;
	}
}

static void GMDMapDescPostOpenDetailKitFixup(GenesisDetailKitLayout *pDetailKit)
{
	if (pDetailKit->detail_tag_list && (pDetailKit->detail_kit_specifier != GenesisTagOrName_RandomByTag)) {
		pDetailKit->detail_kit_specifier = GenesisTagOrName_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(pDetailKit->detail_kit) && (pDetailKit->detail_kit_specifier != GenesisTagOrName_SpecificByName)) {
		pDetailKit->detail_kit_specifier = GenesisTagOrName_SpecificByName;
	}
}

static void GMDMapDescPostOpenRoomDetailKitFixup(GenesisRoomDetailKitLayout *pDetailKit)
{
	if (pDetailKit->detail_tag_list && (pDetailKit->detail_specifier != GenesisTagNameDefault_RandomByTag)) {
		pDetailKit->detail_specifier = GenesisTagNameDefault_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(pDetailKit->detail_kit) && (pDetailKit->detail_specifier != GenesisTagNameDefault_SpecificByName)) {
		pDetailKit->detail_specifier = GenesisTagNameDefault_SpecificByName;
	}
}

static void GMDMapDescPostOpenFixupCommonData(GenesisLayoutCommonData *pCommonData)
{
	// Deal with proper setting of specifier enums based on data
	if (pCommonData->backdrop_info.backdrop_tag_list && (pCommonData->backdrop_info.backdrop_specifier != GenesisTagOrName_RandomByTag)) {
		pCommonData->backdrop_info.backdrop_specifier = GenesisTagOrName_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(pCommonData->backdrop_info.backdrop) && (pCommonData->backdrop_info.backdrop_specifier != GenesisTagOrName_SpecificByName)) {
		pCommonData->backdrop_info.backdrop_specifier = GenesisTagOrName_SpecificByName;
	}
}

static void GMDMapDescPostOpenFixupExterior(GenesisExteriorLayout *exterior_layout)
{
	int i;

	GMDMapDescPostOpenFixupCommonData(&exterior_layout->common_data);

	if (exterior_layout->info.geotype_tag_list && (exterior_layout->info.geotype_specifier != GenesisTagOrName_RandomByTag)) {
		exterior_layout->info.geotype_specifier = GenesisTagOrName_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(exterior_layout->info.geotype) && (exterior_layout->info.geotype_specifier != GenesisTagOrName_SpecificByName)) {
		exterior_layout->info.geotype_specifier = GenesisTagOrName_SpecificByName;
	}

	if (exterior_layout->info.ecosystem_tag_list && (exterior_layout->info.ecosystem_specifier != GenesisTagOrName_RandomByTag)) {
		exterior_layout->info.ecosystem_specifier = GenesisTagOrName_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(exterior_layout->info.ecosystem) && (exterior_layout->info.ecosystem_specifier != GenesisTagOrName_SpecificByName)) {
		exterior_layout->info.ecosystem_specifier = GenesisTagOrName_SpecificByName;
	}

	GMDMapDescPostOpenDetailKitFixup(&exterior_layout->detail_kit_1);
	GMDMapDescPostOpenDetailKitFixup(&exterior_layout->detail_kit_2);

	for(i=eaSize(&exterior_layout->rooms)-1; i>=0; --i) {
		GenesisLayoutRoom *pRoom = exterior_layout->rooms[i];

		if (pRoom->room_tag_list && (pRoom->room_specifier != GenesisTagOrName_RandomByTag)) {
			pRoom->room_specifier = GenesisTagOrName_RandomByTag;
		}
		if (IS_HANDLE_ACTIVE(pRoom->room) && (pRoom->room_specifier != GenesisTagOrName_SpecificByName)) {
			pRoom->room_specifier = GenesisTagOrName_SpecificByName;
		}

		GMDMapDescPostOpenRoomDetailKitFixup(&pRoom->detail_kit_1);
		GMDMapDescPostOpenRoomDetailKitFixup(&pRoom->detail_kit_2);
	}
	for(i=eaSize(&exterior_layout->paths)-1; i>=0; --i) {
		GenesisLayoutPath *pPath = exterior_layout->paths[i];

		if (pPath->path_tag_list && (pPath->path_specifier != GenesisTagOrName_RandomByTag)) {
			pPath->path_specifier = GenesisTagOrName_RandomByTag;
		}
		if (IS_HANDLE_ACTIVE(pPath->path) && (pPath->path_specifier != GenesisTagOrName_SpecificByName)) {
			pPath->path_specifier = GenesisTagOrName_SpecificByName;
		}

		GMDMapDescPostOpenRoomDetailKitFixup(&pPath->detail_kit_1);
		GMDMapDescPostOpenRoomDetailKitFixup(&pPath->detail_kit_2);
	}
}

static void GMDMapDescPostOpenFixupInterior(GenesisInteriorLayout *interior_layout)
{
	int i;

	GMDMapDescPostOpenFixupCommonData(&interior_layout->common_data);

	if (interior_layout->info.room_kit_tag_list && (interior_layout->info.room_kit_specifier != GenesisTagOrName_RandomByTag)) {
		interior_layout->info.room_kit_specifier = GenesisTagOrName_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(interior_layout->info.room_kit) && (interior_layout->info.room_kit_specifier != GenesisTagOrName_SpecificByName)) {
		interior_layout->info.room_kit_specifier = GenesisTagOrName_SpecificByName;
	}

	if (interior_layout->info.light_kit_tag_list && (interior_layout->info.light_kit_specifier != GenesisTagOrName_RandomByTag)) {
		interior_layout->info.light_kit_specifier = GenesisTagOrName_RandomByTag;
	}
	if (IS_HANDLE_ACTIVE(interior_layout->info.light_kit) && (interior_layout->info.light_kit_specifier != GenesisTagOrName_SpecificByName)) {
		interior_layout->info.light_kit_specifier = GenesisTagOrName_SpecificByName;
	}

	GMDMapDescPostOpenDetailKitFixup(&interior_layout->detail_kit_1);
	GMDMapDescPostOpenDetailKitFixup(&interior_layout->detail_kit_2);

	for(i=eaSize(&interior_layout->rooms)-1; i>=0; --i) {
		GenesisLayoutRoom *pRoom = interior_layout->rooms[i];

		if (pRoom->room_tag_list && (pRoom->room_specifier != GenesisTagOrName_RandomByTag)) {
			pRoom->room_specifier = GenesisTagOrName_RandomByTag;
		}
		if (IS_HANDLE_ACTIVE(pRoom->room) && (pRoom->room_specifier != GenesisTagOrName_SpecificByName)) {
			pRoom->room_specifier = GenesisTagOrName_SpecificByName;
		}

		GMDMapDescPostOpenRoomDetailKitFixup(&pRoom->detail_kit_1);
		GMDMapDescPostOpenRoomDetailKitFixup(&pRoom->detail_kit_2);
	}
	for(i=eaSize(&interior_layout->paths)-1; i>=0; --i) {
		GenesisLayoutPath *pPath = interior_layout->paths[i];

		if (pPath->path_tag_list && (pPath->path_specifier != GenesisTagOrName_RandomByTag)) {
			pPath->path_specifier = GenesisTagOrName_RandomByTag;
		}
		if (IS_HANDLE_ACTIVE(pPath->path) && (pPath->path_specifier != GenesisTagOrName_SpecificByName)) {
			pPath->path_specifier = GenesisTagOrName_SpecificByName;
		}

		GMDMapDescPostOpenRoomDetailKitFixup(&pPath->detail_kit_1);
		GMDMapDescPostOpenRoomDetailKitFixup(&pPath->detail_kit_2);
	}
}

static void GMDMapDescPostOpenFixupSolSys(GenesisSolSysLayout *solar_system_layout)
{
	GenesisShoeboxLayout *shoebox = &solar_system_layout->shoebox;

	GMDMapDescPostOpenFixupCommonData(&solar_system_layout->common_data);
}

static void GMDMapDescPostOpenFixup(GenesisMapDescription *pMapDesc)
{
	int i,j;

	if(pMapDesc->exterior_layout)
		GMDMapDescPostOpenFixupExterior(pMapDesc->exterior_layout);

	for(i=eaSize(&pMapDesc->interior_layouts)-1; i>=0; --i) {
		GMDMapDescPostOpenFixupInterior(pMapDesc->interior_layouts[i]);
	}

	for(i=eaSize(&pMapDesc->solar_system_layouts)-1; i>=0; --i) {
		GMDMapDescPostOpenFixupSolSys(pMapDesc->solar_system_layouts[i]);
	}

	for(i=eaSize(&pMapDesc->missions)-1; i>=0; --i) {
		GenesisMissionDescription *pMission = pMapDesc->missions[i];

		langMakeEditorCopy( parse_GenesisMissionDescription, pMission, true );

		for(j=eaSize(&pMission->eaChallenges)-1; j>=0; --j) {
			GenesisMissionChallenge *pChallenge = pMission->eaChallenges[j];

			if (pChallenge->eaChallengeTags && (pChallenge->eSpecifier != GenesisTagOrName_RandomByTag)) {
				pChallenge->eSpecifier = GenesisTagOrName_RandomByTag;
			}
			if (pChallenge->pcChallengeName && (pChallenge->eSpecifier != GenesisTagOrName_SpecificByName)) {
				pChallenge->eSpecifier = GenesisTagOrName_SpecificByName;
			}

			if (pChallenge->eType == GenesisChallenge_Clickie) {
				if (!pChallenge->pClickie) {
					pChallenge->pClickie = StructCreate(parse_GenesisMissionChallengeClickie);
				}
			}
			if (pChallenge->eType == GenesisChallenge_Encounter || pChallenge->eType == GenesisChallenge_Encounter2) {
				if (!pChallenge->pEncounter) {
					pChallenge->pEncounter = StructCreate(parse_GenesisMissionChallengeEncounter);
				}
			}
		}
	}
}

static void GMDMapDescPreSaveWhenFixup(GenesisWhen *pWhen)
{
	if (pWhen->type != GenesisWhen_RoomEntry && pWhen->type != GenesisWhen_RoomEntryAll) {
		eaDestroyStruct(&pWhen->eaRooms, parse_GenesisWhenRoom);
	}
	if (pWhen->type != GenesisWhen_ChallengeComplete && pWhen->type != GenesisWhen_ChallengeAdvance) {
		eaDestroyEx(&pWhen->eaChallengeNames, GMDStructFreeString);
	}
	if (pWhen->type != GenesisWhen_ChallengeComplete) {
		pWhen->iChallengeNumToComplete = 0;
	}
	if (pWhen->type != GenesisWhen_ObjectiveComplete && pWhen->type != GenesisWhen_ObjectiveCompleteAll &&
		pWhen->type != GenesisWhen_ObjectiveInProgress) {
		eaDestroyEx(&pWhen->eaObjectiveNames, GMDStructFreeString);
	}
	if (pWhen->type != GenesisWhen_PromptComplete) {
		eaDestroyEx(&pWhen->eaPromptNames, GMDStructFreeString);
	}
	if (pWhen->type != GenesisWhen_ContactComplete) {
		eaDestroyEx(&pWhen->eaContactNames, GMDStructFreeString);
	}
	if (pWhen->type != GenesisWhen_CritterKill) {
		eaDestroyEx(&pWhen->eaCritterDefNames, GMDStructFreeString);
		eaDestroyEx(&pWhen->eaCritterGroupNames, GMDStructFreeString);
		pWhen->iCritterNumToComplete = 0;
	}
	if (pWhen->type != GenesisWhen_ItemCount) {
		eaDestroyEx(&pWhen->eaItemDefNames, GMDStructFreeString);
		pWhen->iItemCount = 0;
	}
}

static void GMDChallengePreSaveFixup(GenesisMissionChallenge *pChallenge)
{
	if(pChallenge->eType != GenesisChallenge_Encounter && pChallenge->eType != GenesisChallenge_Encounter2) {
		StructDestroySafe( parse_GenesisMissionChallengeEncounter, &pChallenge->pEncounter );
	} else {
		if (  pChallenge->pEncounter->ePatrolType != GENESIS_PATROL_OtherRoom
			  && pChallenge->pEncounter->ePatrolType != GENESIS_PATROL_OtherRoom_OneWay) {
			StructFreeStringSafe( &pChallenge->pEncounter->pcPatOtherRoomName );
		}

		if (  pChallenge->pEncounter->ePatrolType != GENESIS_PATROL_OtherRoom
			  && pChallenge->pEncounter->ePatrolType != GENESIS_PATROL_OtherRoom_OneWay
			  && pChallenge->pEncounter->ePatrolType != GENESIS_PATROL_Path
			  && pChallenge->pEncounter->ePatrolType != GENESIS_PATROL_Path_OneWay ) {
			pChallenge->pEncounter->ePatPlacement = 0;
			StructFreeStringSafe( &pChallenge->pEncounter->pcPatRefChallengeName );
		} else {
			if (pChallenge->pEncounter->ePatPlacement != GenesisChallengePlace_Near_Challenge ) {
				StructFreeStringSafe( &pChallenge->pEncounter->pcPatRefChallengeName );
			}
		}
	}
	if(pChallenge->eType != GenesisChallenge_Clickie) {
		StructDestroySafe( parse_GenesisMissionChallengeClickie, &pChallenge->pClickie );
	}

	if(pChallenge->pClickie && !IS_HANDLE_ACTIVE(pChallenge->pClickie->hInteractionDef)) {
		StructDestroySafe( parse_GenesisMissionChallengeClickie, &pChallenge->pClickie );
	}

	if(pChallenge->ePlacement == GenesisChallengePlace_Prefab_Location) {
		pChallenge->eFacing = 0;
		pChallenge->fRotationIncrement = 0;
	} else {
		StructFreeStringSafe( &pChallenge->pcRefPrefabLocation );
	}
	
	if (pChallenge->eSpecifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&pChallenge->eaChallengeTags, StructFreeString);
		pChallenge->bHeterogenousObjects = false;
	}
	if (pChallenge->eSpecifier != GenesisTagOrName_SpecificByName) {
		StructFreeString(pChallenge->pcChallengeName);
		pChallenge->pcChallengeName = NULL;
	}

	GMDMapDescPreSaveWhenFixup(&pChallenge->spawnWhen);
}

static void GMDMapDescPreSaveDetailKitFixup(GenesisDetailKitLayout *pDetailKit)
{
	if (pDetailKit->detail_kit_specifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&pDetailKit->detail_tag_list, StructFreeString);
	}
	if (pDetailKit->detail_kit_specifier != GenesisTagOrName_SpecificByName) {
		REMOVE_HANDLE(pDetailKit->detail_kit);
	}
}

static void GMDMapDescPreSaveRoomDetailKitFixup(GenesisRoomDetailKitLayout *pDetailKit)
{
	if (pDetailKit->detail_specifier != GenesisTagNameDefault_RandomByTag) {
		eaDestroyEx(&pDetailKit->detail_tag_list, StructFreeString);
	}
	if (pDetailKit->detail_specifier != GenesisTagNameDefault_SpecificByName) {
		REMOVE_HANDLE(pDetailKit->detail_kit);
	}
}

static void GMDMapDescPreSaveFixupCommonData(GenesisLayoutCommonData *pCommonData)
{
	// Clean up excess optional structures in layouts
	if (pCommonData->backdrop_info.backdrop_specifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&pCommonData->backdrop_info.backdrop_tag_list, StructFreeString);
	}
	if (pCommonData->backdrop_info.backdrop_specifier != GenesisTagOrName_SpecificByName) {
		REMOVE_HANDLE(pCommonData->backdrop_info.backdrop);
	}
}

static void GMDMapDescPreSaveFixupInterior(GenesisInteriorLayout *interior_layout)
{
	int i;

	GMDMapDescPreSaveFixupCommonData(&interior_layout->common_data);

	if (interior_layout->info.room_kit_specifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&interior_layout->info.room_kit_tag_list, StructFreeString);
	}
	if (interior_layout->info.room_kit_specifier != GenesisTagOrName_SpecificByName) {
		REMOVE_HANDLE(interior_layout->info.room_kit);
	}

	if (interior_layout->info.light_kit_specifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&interior_layout->info.light_kit_tag_list, StructFreeString);
	}
	if (interior_layout->info.light_kit_specifier != GenesisTagOrName_SpecificByName) {
		REMOVE_HANDLE(interior_layout->info.light_kit);
	}

	GMDMapDescPreSaveDetailKitFixup(&interior_layout->detail_kit_1);
	GMDMapDescPreSaveDetailKitFixup(&interior_layout->detail_kit_2);

	for(i=eaSize(&interior_layout->rooms)-1; i>=0; --i) {
		GenesisLayoutRoom *pRoom = interior_layout->rooms[i];

		if (pRoom->room_specifier != GenesisTagOrName_RandomByTag) {
			eaDestroyEx(&pRoom->room_tag_list, StructFreeString);
		}
		if (pRoom->room_specifier != GenesisTagOrName_SpecificByName) {
			REMOVE_HANDLE(pRoom->room);
		}

		GMDMapDescPreSaveRoomDetailKitFixup(&pRoom->detail_kit_1);
		GMDMapDescPreSaveRoomDetailKitFixup(&pRoom->detail_kit_2);
	}
	for(i=eaSize(&interior_layout->paths)-1; i>=0; --i) {
		GenesisLayoutPath *pPath = interior_layout->paths[i];

		if (pPath->path_specifier != GenesisTagOrName_RandomByTag) {
			eaDestroyEx(&pPath->path_tag_list, StructFreeString);
		}
		if (pPath->path_specifier != GenesisTagOrName_SpecificByName) {
			REMOVE_HANDLE(pPath->path);
		}

		GMDMapDescPreSaveRoomDetailKitFixup(&pPath->detail_kit_1);
		GMDMapDescPreSaveRoomDetailKitFixup(&pPath->detail_kit_2);
	}
}

static void GMDMapDescPreSaveFixupExterior(GenesisExteriorLayout *exterior_layout)
{
	int i;

	GMDMapDescPreSaveFixupCommonData(&exterior_layout->common_data);

	if (exterior_layout->info.geotype_specifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&exterior_layout->info.geotype_tag_list, StructFreeString);
	}
	if (exterior_layout->info.geotype_specifier != GenesisTagOrName_SpecificByName) {
		REMOVE_HANDLE(exterior_layout->info.geotype);
	}

	if (exterior_layout->info.ecosystem_specifier != GenesisTagOrName_RandomByTag) {
		eaDestroyEx(&exterior_layout->info.ecosystem_tag_list, StructFreeString);
	}
	if (exterior_layout->info.ecosystem_specifier != GenesisTagOrName_SpecificByName) {
		REMOVE_HANDLE(exterior_layout->info.ecosystem);
	}

	GMDMapDescPreSaveDetailKitFixup(&exterior_layout->detail_kit_1);
	GMDMapDescPreSaveDetailKitFixup(&exterior_layout->detail_kit_2);

	for(i=eaSize(&exterior_layout->rooms)-1; i>=0; --i) {
		GenesisLayoutRoom *pRoom = exterior_layout->rooms[i];

		if (pRoom->room_specifier != GenesisTagOrName_RandomByTag) {
			eaDestroyEx(&pRoom->room_tag_list, StructFreeString);
		}
		if (pRoom->room_specifier != GenesisTagOrName_SpecificByName) {
			REMOVE_HANDLE(pRoom->room);
		}

		GMDMapDescPreSaveRoomDetailKitFixup(&pRoom->detail_kit_1);
		GMDMapDescPreSaveRoomDetailKitFixup(&pRoom->detail_kit_2);
	}
	for(i=eaSize(&exterior_layout->paths)-1; i>=0; --i) {
		GenesisLayoutPath *pPath = exterior_layout->paths[i];

		if (pPath->path_specifier != GenesisTagOrName_RandomByTag) {
			eaDestroyEx(&pPath->path_tag_list, StructFreeString);
		}
		if (pPath->path_specifier != GenesisTagOrName_SpecificByName) {
			REMOVE_HANDLE(pPath->path);
		}

		GMDMapDescPreSaveRoomDetailKitFixup(&pPath->detail_kit_1);
		GMDMapDescPreSaveRoomDetailKitFixup(&pPath->detail_kit_2);
	}
}

static void GMDMapDescPreSaveFixupSolSys(GenesisSolSysLayout *solar_system_layout)
{
	int j;
	GenesisShoeboxLayout *pShoebox = &solar_system_layout->shoebox;

	GMDMapDescPreSaveFixupCommonData(&solar_system_layout->common_data);

	for(j=eaSize(&pShoebox->point_lists)-1; j>=0; --j) {
		ShoeboxPointList *pList = pShoebox->point_lists[j];
		if (pList->list_type != SBLT_Orbit) {
			StructDestroySafe(parse_SSObjSet, &pList->orbit_object);
			free(pList->name);
			pList->name = NULL;
		}
	}
}
typedef struct GMDMapDescFixupMessageData {
	char messagePrefix[256];
	char messageScope[MAX_PATH];
	int it;
} GMDMapDescFixupMessageData;

static void GMDMapDescPreSaveFixupMessage(DisplayMessage* pDisplayMsg, GMDMapDescFixupMessageData* data)
{
	char keyBuffer[ RESOURCE_NAME_MAX_SIZE ];
	sprintf( keyBuffer, "%s.%d", data->messagePrefix, data->it++ );
	
	langMakeEditorCopy( parse_DisplayMessage, pDisplayMsg, true );
	pDisplayMsg->pEditorCopy->pcMessageKey = allocAddString( keyBuffer );
	pDisplayMsg->pEditorCopy->pcScope = allocAddString( data->messageScope );
}

static void GMDMapDescPreSaveFixup(GenesisMapDescription *pMapDesc)
{
	int i,j;

	for(i=eaSize(&pMapDesc->interior_layouts)-1; i>=0; --i)
		GMDMapDescPreSaveFixupInterior(pMapDesc->interior_layouts[i]);

	if(pMapDesc->exterior_layout)
		GMDMapDescPreSaveFixupExterior(pMapDesc->exterior_layout);

	for(i=eaSize(&pMapDesc->solar_system_layouts)-1; i>=0; --i)
		GMDMapDescPreSaveFixupSolSys(pMapDesc->solar_system_layouts[i]);

	// Clean up excess optional structures in missions
	for(i=eaSize(&pMapDesc->missions)-1; i>=0; --i) {
		GenesisMissionDescription *pMission = pMapDesc->missions[i];

		GEMissionLevelDefPreSaveFixup( &pMission->zoneDesc.levelDef );

		for(j=eaSize(&pMission->eaChallenges)-1; j>=0; --j) {
			GMDChallengePreSaveFixup(pMission->eaChallenges[j]);
		}

		for(j=eaSize(&pMission->zoneDesc.eaPrompts)-1; j>=0; --j) {
			GenesisMissionPrompt *pPrompt = pMission->zoneDesc.eaPrompts[j];

			if (pPrompt->showWhen.type == GenesisWhen_Manual) {
				pPrompt->bOptional = false;
			}

			GMDMapDescPreSaveWhenFixup(&pPrompt->showWhen);
			{
				GMDMapDescFixupMessageData data = { 0 };
				if( GMDEmbeddedMapDesc() ) {
					sprintf( data.messagePrefix, "GenesisMap_%s.%s.%s", zmapInfoGetPublicName( NULL ), pMission->zoneDesc.pcName, pPrompt->pcName );
				} else {
					sprintf( data.messagePrefix, "MapDesc_%s.%s.%s", pMapDesc->name, pMission->zoneDesc.pcName, pPrompt->pcName );
				}
				strcpy( data.messageScope, pMapDesc->scope ? pMapDesc->scope : "" );
				langForEachDisplayMessage( parse_GenesisMissionPrompt, pPrompt, GMDMapDescPreSaveFixupMessage, &data );
			}

			if (pPrompt->sPrimaryBlock.costume.eCostumeType != GenesisMissionCostumeType_Specified) {
				REMOVE_HANDLE(pPrompt->sPrimaryBlock.costume.hCostume);
			}
			if (pPrompt->sPrimaryBlock.costume.eCostumeType != GenesisMissionCostumeType_PetCostume) {
				REMOVE_HANDLE(pPrompt->sPrimaryBlock.costume.hPetCostume);
			}
			if (pPrompt->sPrimaryBlock.costume.eCostumeType != GenesisMissionCostumeType_CritterGroup) {
				pPrompt->sPrimaryBlock.costume.eCostumeCritterGroupType = 0;
				REMOVE_HANDLE(pPrompt->sPrimaryBlock.costume.hCostumeCritterGroup);
				StructFreeStringSafe(&pPrompt->sPrimaryBlock.costume.pchCostumeMapVar);
				StructFreeStringSafe(&pPrompt->sPrimaryBlock.costume.pchCostumeIdentifier);
			} else {
				if(pPrompt->sPrimaryBlock.costume.eCostumeCritterGroupType != ContactMapVarOverrideType_Specified) {
					REMOVE_HANDLE(pPrompt->sPrimaryBlock.costume.hCostumeCritterGroup);
				}
				if(pPrompt->sPrimaryBlock.costume.eCostumeCritterGroupType != ContactMapVarOverrideType_MapVar) {
					StructFreeStringSafe(&pPrompt->sPrimaryBlock.costume.pchCostumeMapVar);
				}
			}
		}

		for(j=eaSize(&pMission->zoneDesc.eaPortals)-1; j>= 0; --j) {
			GenesisMissionPortal *pPortal = pMission->zoneDesc.eaPortals[j];

			if (pPortal->eType == GenesisMissionPortal_Normal) {
				StructFreeStringSafe( &pPortal->pcEndZmap );
				eaDestroyStruct( &pPortal->eaEndVariables, parse_WorldVariableDef );
				StructFreeStringSafe( &pPortal->pcEndLayout );
			}
			if (pPortal->eType == GenesisMissionPortal_BetweenLayouts) {
				StructFreeStringSafe( &pPortal->pcEndZmap );
				eaDestroyStruct( &pPortal->eaEndVariables, parse_WorldVariableDef );
			}
			if (pPortal->eType == GenesisMissionPortal_OneWayOutOfMap) {
				StructFreeStringSafe( &pPortal->pcWarpToStartText );
				StructFreeStringSafe( &pPortal->pcEndLayout );
			}
		}

		if (pMission->zoneDesc.generationType != GenesisMissionGenerationType_OpenMission) {
			StructDestroySafe(parse_GenesisMissionOpenMissionDescription, &pMission->zoneDesc.pOpenMissionDescription);
		}

		for(j=eaSize(&pMission->zoneDesc.eaObjectives)-1; j>=0; --j) {
			GenesisMissionObjective *pObjective = pMission->zoneDesc.eaObjectives[j];
			GMDMapDescPreSaveWhenFixup(&pObjective->succeedWhen);
		}

		//TODO: sfenton If we support multiple types in a single map this needs to be changed
		if (eaSize(&pMapDesc->solar_system_layouts)) {
			// Reset start information for solar systems
			pMission->zoneDesc.startDescription.bHasEntryDoor = false;
			pMission->zoneDesc.startDescription.eExitFrom = GenesisMissionExitFrom_Anywhere;
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcExitLayout);
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcExitRoom);
		}
		
		if( pMission->zoneDesc.startDescription.exitPromptCostume.eCostumeType == GenesisMissionCostumeType_PetCostume ) {
			REMOVE_HANDLE(pMission->zoneDesc.startDescription.exitPromptCostume.hCostume);
		} else {
			REMOVE_HANDLE(pMission->zoneDesc.startDescription.exitPromptCostume.hPetCostume);
		}

		if (!pMission->zoneDesc.startDescription.bContinue) {
			pMission->zoneDesc.startDescription.eContinueFrom = 0;
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinueLayout);
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinueRoom);
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinueChallenge);
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinueMap);
			eaDestroyStruct(&pMission->zoneDesc.startDescription.eaContinueVariables, parse_WorldVariable);
			REMOVE_HANDLE(pMission->zoneDesc.startDescription.hContinueTransitionOverride);
			StructReset( parse_GenesisMissionCostume, &pMission->zoneDesc.startDescription.continuePromptCostume );
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinuePromptButtonText);
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinuePromptCategoryName);
			pMission->zoneDesc.startDescription.eContinuePromptPriority = 0;
			StructFreeStringSafe(&pMission->zoneDesc.startDescription.pcContinuePromptTitleText);
			eaDestroyEx(&pMission->zoneDesc.startDescription.eaContinuePromptBodyText, NULL);
		} else {
			if (!genesisMissionContinueIsAutogenerated(&pMission->zoneDesc, eaSize(&pMapDesc->solar_system_layouts), pMapDesc->exterior_layout != NULL)) {
				StructReset(parse_GenesisMissionCostume, &pMission->zoneDesc.startDescription.continuePromptCostume);
			}
			if (pMission->zoneDesc.startDescription.eContinueFrom != GenesisMissionExitFrom_DoorInRoom) {
				StructFreeStringSafe( &pMission->zoneDesc.startDescription.pcContinueLayout );
				StructFreeStringSafe( &pMission->zoneDesc.startDescription.pcContinueRoom );
			}
			
			if(pMission->zoneDesc.startDescription.continuePromptCostume.eCostumeType == GenesisMissionCostumeType_PetCostume) {
				REMOVE_HANDLE(pMission->zoneDesc.startDescription.continuePromptCostume.hCostume);
			} else {
				REMOVE_HANDLE(pMission->zoneDesc.startDescription.continuePromptCostume.hPetCostume);
			}
		}
	}
	
	for(i=eaSize(&pMapDesc->shared_challenges)-1; i>=0; --i) {
		GMDChallengePreSaveFixup(pMapDesc->shared_challenges[i]);
	}
}


static MapDescEditDoc *GMDInitDoc(GenesisMapDescription *pMapDesc, bool bCreated, bool bEmbedded)
{
	MapDescEditDoc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = (MapDescEditDoc*)calloc(1,sizeof(MapDescEditDoc));
	pDoc->bEmbeddedMode = bEmbedded;
	pDoc->EditingMapType = GenesisMapType_None;

	// Fill in the map description data
	if (bCreated) {
		pDoc->pMapDesc = StructCreate(parse_GenesisMapDescription);
		GMDEnsureLayout(pDoc);
		GMDUpdateSelectedLayout(pDoc);
		pDoc->pMapDesc->version = GENESIS_MAP_DESC_VERSION;
		assert(pDoc->pMapDesc);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Map_Description", "MapDescription", "MapDescription");
		pDoc->pMapDesc->name = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "genesis/mapdescriptions/%s.mapdesc", pDoc->pMapDesc->name);
		pDoc->pMapDesc->filename = allocAddString(nameBuf);
		GMDMapDescPostOpenFixup(pDoc->pMapDesc);
	} else {
		pDoc->pMapDesc = StructClone(parse_GenesisMapDescription, pMapDesc);
		assert(pDoc->pMapDesc);
		GMDMapDescPostOpenFixup(pDoc->pMapDesc);
		pDoc->pOrigMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
		GMDEnsureLayout(pDoc);
		GMDUpdateSelectedLayout(pDoc);
	}

	if (!bEmbedded) {
		// Set up the undo stack
		pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
		EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
		pDoc->pNextUndoMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
	}

	return pDoc;
}


MapDescEditDoc *GMDOpenMapDesc(EMEditor *pEditor, char *pcName)
{
	MapDescEditDoc *pDoc = NULL;
	GenesisMapDescription *pMapDesc = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_MapDescDictionary, pcName)) {
		// Simply open the object since it is in the dictionary
		pMapDesc = RefSystem_ReferentFromString(g_MapDescDictionary, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_MapDescDictionary, true);
		resSetDictionaryEditMode("PetContactList", true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_MapDescDictionary, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pMapDesc || bCreated) {
		pDoc = GMDInitDoc(pMapDesc, bCreated, false);
		GMDUpdateSelectedLayout(pDoc);
		GMDInitDisplay(pEditor, pDoc);
		resFixFilename(g_MapDescDictionary, pDoc->pMapDesc->name, pDoc->pMapDesc);
	}

	return pDoc;
}

bool GMDEmbeddedMapDescHasChanges()
{
	if (gEmbeddedDoc) {
		return !(gEmbeddedDoc->emDoc.saved);
	}	
	return false;
}

void GMDSetEmbeddedMapDescEnabled(bool bEnabled)
{
	if (gEmbeddedDoc) {
		bool bIsActive = ui_IsActive(UI_WIDGET(gEmbeddedDoc->pMainWindow));
		if(bIsActive != bEnabled)
		{
			ui_SetActive(UI_WIDGET(gEmbeddedDoc->pMainWindow), bEnabled);
			if(bEnabled)
				GMDUpdateDisplay(gEmbeddedDoc);
		}
	}
}


MapDescEditDoc *GMDOpenEmbeddedMapDesc(GenesisMapDescription *pMapDesc, GMDEmbeddedCallback callbackFunc)
{
	MapDescEditDoc *pDoc = NULL;

	if (!pMapDesc) {
		return NULL;
	}

	GMDInitData(NULL);

	if (gEmbeddedDoc) {
		ui_WindowPresent(gEmbeddedDoc->pMainWindow);
		return gEmbeddedDoc;
	}

	pDoc = GMDInitDoc(pMapDesc, false, true);
	pDoc->callbackFunc = callbackFunc;
	GMDInitDisplay(NULL, pDoc);
	gEmbeddedDoc = pDoc;

	return pDoc;
}


void GMDCloseEmbeddedMapDesc(void)
{
	if (gEmbeddedDoc) {
		GMDCancelEmbedded(NULL, gEmbeddedDoc);
	}
}


void GMDRevertMapDesc(MapDescEditDoc *pDoc)
{
	GenesisMapDescription *pMapDesc;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pMapDesc = RefSystem_ReferentFromString(g_MapDescDictionary, pDoc->emDoc.orig_doc_name);
	if (pMapDesc) {
		// Revert the map description
		StructDestroy(parse_GenesisMapDescription, pDoc->pMapDesc);
		StructDestroy(parse_GenesisMapDescription, pDoc->pOrigMapDesc);
		pDoc->pMapDesc = StructClone(parse_GenesisMapDescription, pMapDesc);
		GMDMapDescPostOpenFixup(pDoc->pMapDesc);
		pDoc->pOrigMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
		GMDUpdateSelectedLayout(pDoc);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_GenesisMapDescription, pDoc->pNextUndoMapDesc);
		pDoc->pNextUndoMapDesc = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		GMDUpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	} 
}


void GMDCloseMapDesc(MapDescEditDoc *pDoc)
{
	int i;

	// Free doc fields
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);
	
	// Free groups
	GMDFreeLayoutInfoGroup(pDoc, pDoc->pLayoutInfoGroup);

	for(i=eaSize(&pDoc->eaRoomGroups)-1; i>=0; --i) {
		GMDFreeRoomGroup(pDoc->eaRoomGroups[i]);
	}
	eaDestroy(&pDoc->eaRoomGroups);

	for(i=eaSize(&pDoc->eaPathGroups)-1; i>=0; --i) {
		GMDFreePathGroup(pDoc->eaPathGroups[i]);
	}
	eaDestroy(&pDoc->eaPathGroups);

	eaDestroyEx(&pDoc->eaPointListGroups, GMDFreePointListGroup);

	GMDFreeChallengeStartGroup(pDoc, pDoc->pChallengeStartGroup);

	for(i=eaSize(&pDoc->eaChallengeGroups)-1; i>=0; --i) {
		GMDFreeChallengeGroup(pDoc->eaChallengeGroups[i]);
	}
	eaDestroy(&pDoc->eaChallengeGroups);

	for(i=eaSize(&pDoc->eaPromptGroups)-1; i>=0; --i) {
		GMDFreePromptGroup(pDoc->eaPromptGroups[i]);
	}
	eaDestroy(&pDoc->eaPromptGroups);

	for(i=eaSize(&pDoc->eaPortalGroups)-1; i>=0; --i) {
		GMDFreePortalGroup(pDoc->eaPortalGroups[i]);
	}
	eaDestroy(&pDoc->eaPortalGroups);

	GMDFreeMissionInfoGroup(pDoc, pDoc->pMissionInfoGroup);

	GMDFreeMissionStartGroup(pDoc, pDoc->pMissionStartGroup);

	for(i=eaSize(&pDoc->eaObjectiveGroups)-1; i>=0; --i) {
		GMDFreeObjectiveGroup(pDoc->eaObjectiveGroups[i]);
	}
	eaDestroy(&pDoc->eaObjectiveGroups);

	// Free the objects
	StructDestroy(parse_GenesisMapDescription, pDoc->pMapDesc);
	if (pDoc->pOrigMapDesc) {
		StructDestroy(parse_GenesisMapDescription, pDoc->pOrigMapDesc);
	}
	StructDestroy(parse_GenesisMapDescription, pDoc->pNextUndoMapDesc);

	// Close the window
	ui_WindowHide(pDoc->pMainWindow);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
}


EMTaskStatus GMDSaveMapDesc(MapDescEditDoc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	GenesisMapDescription *pMapDescCopy;

	// Deal with state changes
	pcName = pDoc->pMapDesc->name;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pMapDescCopy = StructClone(parse_GenesisMapDescription, pDoc->pMapDesc);
	GMDMapDescPreSaveFixup(pMapDescCopy);

	// Perform validation
	//if (!mapdesc_Validate(pMapDescCopy)) {
	//	StructDestroy(parse_GenesisMapDescription, pMapDescCopy);
	//	return EM_TASK_FAILED;
	//}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pMapDescCopy, pDoc->pOrigMapDesc, bSaveAsNew);

	return status;
}

MapDescEditDoc *GMDEmbeddedMapDesc(void)
{
	return gEmbeddedDoc;
}

bool GMDQueryIfNotGenesisDirs( char** dirs, char** names )
{
	char** invalidGenesisDirs = NULL;

	{
		int it;
		for( it = 0; it != eaSize( &dirs ); ++it ) {
			char zoneFile[ MAX_PATH ];
			ZoneMapInfo* zmapInfo = StructCreate( parse_ZoneMapInfo );

			sprintf( zoneFile, "%s/%s.zone", dirs[ it ], names[ it ]);
			if( !fileExists( zoneFile )) {
				// do nothing -- no zone file 
			} else if( !ParserLoadSingleDictionaryStruct( zoneFile, "ZoneMap", zmapInfo, 0 )) {
				eaPush( &invalidGenesisDirs, dirs[ it ]);
			} else if( !zmapInfoGetGenesisData( zmapInfo ) || zmapInfoGetLayerCount( zmapInfo )) {
				eaPush( &invalidGenesisDirs, dirs[ it ]);
			}

			StructDestroy( parse_ZoneMapInfo, zmapInfo );
		}
	}

	if( invalidGenesisDirs ) {
		char* errStr = NULL;
		estrConcatStatic( &errStr, "Are you sure you want to overwrite the "
						  "following directories?  They have non-Genesis "
						  "zonemaps in them.\n" );
		{
			int it;
			for( it = 0; it != eaSize( &invalidGenesisDirs ); ++it ) {
				estrConcatf( &errStr, "\n%s", invalidGenesisDirs[ it ]);
			}
		}

		{
			UIDialogButtons result = ui_ModalDialog( "Are you sure?", errStr, ColorBlack, UIYes | UINo );
			estrDestroy( &errStr );
			eaDestroy( &invalidGenesisDirs );
			return result == UIYes;
		}
	}
	return true;
}

#include "GenesisMapDescriptionEditor_h_ast.c"

#endif

