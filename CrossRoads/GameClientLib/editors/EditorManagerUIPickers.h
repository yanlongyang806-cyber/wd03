#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef __EDITORMANAGERUIPICKERS_H__
#define __EDITORMANAGERUIPICKERS_H__

#ifndef NO_EDITORS

typedef struct EMPicker EMPicker;
typedef struct UIWidget UIWidget;
typedef struct ZoneMapEncounterInfo ZoneMapEncounterInfo;
typedef struct ZoneMapEncounterObjectInfo ZoneMapEncounterObjectInfo;

/******
* Pickers are meant to provide a method of selecting data from an organized view to edit.  The picker
* selection method will open documents via the traditional open document API.  The values of the name
* and type must be set in the appropriate AMDisplayType's selected_func, which is called when the
* type's tree node is clicked.
*
* Alternatively, a picker can be set to invoke a particular callback with the selected data as well.
* TODO: more documentation
******/

void emPickerInit(SA_PARAM_NN_VALID EMPicker *picker);

AUTO_ENUM;
typedef enum EMEncounterObjectFilterType {
	EncObj_None,		EIGNORE
	EncObj_Spawn,
	EncObj_Clickie,
	EncObj_Destructible,
	EncObj_Door,
	EncObj_Encounter,
	EncObj_Volume,
	EncObj_Contact,
	EncObj_Other,
	EncObj_WholeMap,
	EncObj_Reward_Box,
	
	EncObj_Usable_As_Warp,
	EncObj_Any,
} EMEncounterObjectFilterType;
extern StaticDefineInt EMEncounterObjectFilterTypeEnum[];

typedef void (*EMPickerZoneMapEncounterObjectCallback)( const char* zmName, const char* logicalName, const float* mapPos, const char* mapIcon, UserData userData );
typedef bool (*EMPickerZoneMapEncounterObjectFilterFn)( const char* zmName, ZoneMapEncounterObjectInfo* object, UserData userData );
typedef void (*EMPickerChangedFn)( UserData userData );

bool emShowZeniObjectPicker(ZoneMapEncounterInfo **ugcInfoOverrides,
							EMEncounterObjectFilterType forceFilterType, const char** eaOverworldIconNames,
							const char* defaultZmap, const char* defaultObj,
							float* defaultOverworldPos, const char* defaultOverworldIcon,
							EMPickerZoneMapEncounterObjectFilterFn filterFn, UserData filterData,
							EMPickerZoneMapEncounterObjectCallback cb, UserData userData );
void emShowOverworldMapPicker(const char** eaIconNames, float* defaultPos, const char* defaultIcon,
							  EMPickerZoneMapEncounterObjectCallback cb, UserData userData );

UIWidget* emZeniObjectWidgetCreate( bool* out_selectedDefault, ZoneMapEncounterInfo **ugcInfoOverrides,
									EMEncounterObjectFilterType forceFilterType,
									SA_PARAM_OP_STR const char* defaultZmap, SA_PARAM_OP_STR const char* defaultObj,
									EMPickerZoneMapEncounterObjectFilterFn filterfn, UserData filterData );
void emZeniObjectWidgetGetSelection( UIWidget* widget, const char** out_mapName, const char** out_logicalName );

UIWidget* emOverworldMapWidgetCreate( const char** iconNames, const float* defaultPos, const char* defaultIcon,
									  EMPickerChangedFn changedFn, UserData changedData );
void emOverworldMapWidgetGetSelection( UIWidget* widget, float* out_mapPos, const char** out_icon );

void emShowZeniPicker( void );

#endif // NO_EDITORS

#endif // __EDITORMANAGERUIPICKERS_H__
