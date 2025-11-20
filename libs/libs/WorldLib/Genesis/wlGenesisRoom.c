#define GENESIS_ALLOW_OLD_HEADERS
#include "wlGenesisRoom.h"

#include "wlGenesis.h"
#include "wlGenesisInterior.h"
#include "wlGenesisExterior.h"
#include "wlGenesisExteriorNode.h"
#include "referencesystem.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "fileutil.h"
#include "error.h"
#include "ResourceInfo.h"
#include "ResourceManager.h"
#include "group.h"
#include "wlUGC.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

static DictionaryHandle genesis_roomdef_dict = NULL;
static DictionaryHandle genesis_pathdef_dict = NULL;
static genesis_roomdef_dict_loaded = false;
static genesis_pathdef_dict_loaded = false;

void genesisReloadRoomDef(const char* relpath, int when)
{
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(GENESIS_ROOM_DEF_DICTIONARY) );
}

void genesisReloadPathDef(const char* relpath, int when)
{
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(GENESIS_PATH_DEF_DICTIONARY) );
}

AUTO_RUN;
void genesisInitRoomDefLibrary(void)
{
	genesis_roomdef_dict = RefSystem_RegisterSelfDefiningDictionary(GENESIS_ROOM_DEF_DICTIONARY, false, parse_GenesisRoomDef, true, false, NULL);
	genesis_pathdef_dict = RefSystem_RegisterSelfDefiningDictionary(GENESIS_PATH_DEF_DICTIONARY, false, parse_GenesisPathDef, true, false, NULL);

	resDictMaintainInfoIndex(genesis_roomdef_dict, NULL, NULL, ".Tags", NULL, NULL);
	resDictMaintainInfoIndex(genesis_pathdef_dict, NULL, NULL, ".Tags", NULL, NULL);
}

void genesisLoadRoomDefLibrary()
{
	if (areEditorsPossible() && !genesis_roomdef_dict_loaded)
	{
		resLoadResourcesFromDisk(genesis_roomdef_dict, "genesis/rooms", ".roomdef", "GenesisRooms.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
		FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/rooms/*.roomdef", genesisReloadRoomDef);
		genesis_roomdef_dict_loaded = true;
	}
	if (areEditorsPossible() && !genesis_pathdef_dict_loaded)
	{
		resLoadResourcesFromDisk(genesis_pathdef_dict, "genesis/paths", ".pathdef", "GenesisPaths.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
		FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/paths/*.pathdef", genesisReloadPathDef);
		genesis_pathdef_dict_loaded = true;
	}
}

GenesisLayoutRoom *genesisFindRoom( GenesisMapDescription* mapDesc, const char* room_name, const char* layout_name )
{
	int layout_it;
	for( layout_it = 0; layout_it != eaSize( &mapDesc->interior_layouts ); ++layout_it ) {
		GenesisInteriorLayout *interior_layout = mapDesc->interior_layouts[ layout_it ];
		int it;
		if( stricmp( interior_layout->name, layout_name ) != 0 ) {
			continue;
		}
		for( it = 0; it != eaSize( &interior_layout->rooms ); ++it ) {
			GenesisLayoutRoom* room = interior_layout->rooms[ it ];
			if( stricmp( room->name, room_name ) == 0 ) {
				return room;
			}
		}
	}
	if( mapDesc->exterior_layout ) {
		int it;
		for( it = 0; it != eaSize( &mapDesc->exterior_layout->rooms ); ++it ) {
			GenesisLayoutRoom* room = mapDesc->exterior_layout->rooms[ it ];
			if( stricmp( room->name, room_name ) == 0 ) {
				return room;
			}
		}
	}

	return NULL;
}

GenesisRoomMission *genesisFindRoomMission( GenesisRoomMission **room_missions, char* mission_name )
{
	int it;
	for( it = 0; it != eaSize( &room_missions ); ++it ) {
		GenesisRoomMission *room_mission = room_missions[ it ];
		if( stricmp( room_mission->mission_name, mission_name ) == 0 ) {
			return room_mission;
		}
	}

	return NULL;
}

/// Fixup function for GenesisLayoutRoom
TextParserResult fixupGenesisLayoutRoom(GenesisLayoutRoom *pRoom, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pRoom->old_room_tags ) {
					eaDestroyEx( &pRoom->room_tag_list, StructFreeString );
					DivideString( pRoom->old_room_tags, ",", &pRoom->room_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pRoom->old_room_tags );
				}
				
				fixupGenesisRoomDetailKitLayout( &pRoom->detail_kit_1, eType, pExtraData );
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

/// Fixup function for GenesisLayoutPath
TextParserResult fixupGenesisLayoutPath(GenesisLayoutPath *pPath, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pPath->old_path_tags ) {
					eaDestroyEx( &pPath->path_tag_list, StructFreeString );
					DivideString( pPath->old_path_tags, ",", &pPath->path_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pPath->old_path_tags );
				}

				fixupGenesisRoomDetailKitLayout( &pPath->detail_kit_1, eType, pExtraData );
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

#include "wlGenesisRoom_h_ast.c"
