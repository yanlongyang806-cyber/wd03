#define GENESIS_ALLOW_OLD_HEADERS
#include "wlGenesisInterior.h"

#include "WorldGridPrivate.h"
#include "wlGenesis.h"
#include "wlGenesisRoom.h"
#include "wlGenesisPopulate.h"
#include "wlGenesisMissions.h"
#include "wlExclusionGrid.h"
#include "wlUGC.h"

#include "ScratchStack.h"
#include "ObjectLibrary.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "wlState.h"
#include "fileutil.h"
#include "error.h"
#include "rand.h"
#include "StringUtil.h"
#include "tga.h"
#include "timing.h"
#include "ResourceInfo.h"
#include "Expression.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#ifndef NO_EDITORS

///////////////////////////////////////////////////////////////

#define INTERIOR_MAX_PLACE_TIME 10.0f // 10 seconds
#define INTERIOR_HEIGHT_INC 10.0f
#define MAX_DOOR_CONFIG_ITERATION 2
#define HALL_CHANNEL 3
#define ABSVEC3(v, r) ((r)[0] = abs((v)[0]), (r)[1] = abs((v)[1]), (r)[2] = abs((v)[2]))

#define bufferGet(buffer,x,z) genesisBufferGet(buffer, buffer##_width, buffer##_height, (x), (z), 0)
#define bufferSet(buffer,x,z,val) genesisBufferSet(buffer, buffer##_width, buffer##_height, (x), (z), 0, (val))
#define bufferGetEx(buffer,x,z,off) genesisBufferGet(buffer, buffer##_width, buffer##_height, (x), (z), (off))
#define bufferSetEx(buffer,x,z,off,val) genesisBufferSet(buffer, buffer##_width, buffer##_height, (x), (z), (off), (val))

static DictionaryHandle genesis_interiors_dict = NULL;
static genesis_interiors_dict_loaded = false;

static bool genesis_interior_debug_images = false;
static bool genesis_interior_verbose_mode = false;

// Shuts up the PS3 compiler because of these typedefs.
GCC_SYSTEM
typedef struct GenesisInteriorRoom GenesisInteriorRoom;
typedef struct GenesisInteriorHallway GenesisInteriorHallway;

//////////////////////////////////////////////////////////////////
// Common Structures
//////////////////////////////////////////////////////////////////

typedef enum GenesisDoorDirectionFlags
{
	GENESIS_DOOR_ENTERING = 1,
	GENESIS_DOOR_EXITING = 2,
} GenesisDoorDirectionFlags;

typedef struct GenesisInteriorDoor
{
	const char *name;
	int x;
	int y;
	int z;
	int rotation;
	bool random;
	bool always_open;
	int dir_flags;
	GenesisInteriorHallway *hallway;
	bool hallway_fork;
} GenesisInteriorDoor;

typedef struct GenesisInteriorMissionVolume
{
	int mission_uid;
	U8 inited : 1;
	GenesisToPlaceObject *mission_volume;
} GenesisInteriorMissionVolume;

typedef struct GenesisInteriorHallwayGroup
{
	char *src_name;
	F32 detail_density;
	GenesisZoneMapPath *src;
	GenesisInteriorRoom **rooms;				//Do not eaDestroyEx, these are freed in a different location
	GenesisInteriorHallway **hallways;

	GenesisToPlaceObject *parent_object;
	GenesisToPlaceObject *infrastructure;
	GenesisToPlaceObject *parent_group_shared;
	GenesisToPlaceObject *lights_parent;
	GenesisInteriorMissionVolume **mission_volumes;
	GenesisToPlaceObject **partitions;
	ExclusionObject **parent_volumes;

	U8 no_details_or_paths : 1;				// Don't place details or ensure walkable paths
} GenesisInteriorHallwayGroup;

typedef struct GenesisInteriorRoom
{
	GenesisZoneMapRoom *src;
	GenesisInteriorDoor **doors;
	GenesisRoomRectArea **areas;
	char *src_name;
	U32 detail_seed;
	U8 *grid;

	char *geo;
	int width;
	int depth;
	int x;
	int z;
	int rot;
	int height;
	int exits;

	U8 placed : 1;
	U8 cap : 1;
	U8 no_src : 1;
	U8 use_proj_lights : 1;					// Forces projector light placement even if this is a one-off
	U8 no_details_or_paths : 1;				// Don't place details or ensure walkable paths
	U8 volumized : 1;						// If true, then a given placement is only valid if it lands on a platform.

	GenesisInteriorHallwayGroup *parent_hallway;
	GenesisInteriorMissionVolume **mission_volumes;
	GenesisToPlaceObject *parent_group_shared;
	GenesisToPlaceObject *parent_group_room;
	GenesisToPlaceObject *parent_group_objects;
	ExclusionObject **parent_volumes;
	GenesisObject **door_caps;

	StashTable picked_patterns;
} GenesisInteriorRoom;

typedef struct GenesisInteriorHallway
{
	GenesisInteriorRoom *start_room;
	int start_door;
	bool start_room_has_door;
	GenesisInteriorRoom *end_room;
	int end_door;
	bool end_room_has_door;
	int *points;
	int min_length;
	int max_length;
	GenesisToPlaceObject *parent_group_shared;
	int max_delta_height;
	bool from_start;
	GenesisInteriorHallwayGroup *parent_hallway;
} GenesisInteriorHallway;

typedef enum GenesisInteriorHallwayGridDir
{
	genesisIntUp	= 1<<0,
	genesisIntRight = 1<<1,		
	genesisIntDown	= 1<<2,
	genesisIntLeft  = 1<<3
} GenesisInteriorHallwayGridDir;

typedef enum GenesisInteriorHallwayGridType
{
	genesisIntNotHall=0,
	genesisIntHall,		
	genesisIntRampTiltUpNorth,		
	genesisIntRampTiltUpEast,		
	genesisIntRampTiltUpSouth,		
	genesisIntRampTiltUpWest,		
} GenesisInteriorHallwayGridType;

typedef struct GenesisInteriorHallwayGridData
{
	GenesisInteriorHallwayGridType type;
	U8 dir_flags;
	int *point_idx;
	int height;
} GenesisInteriorHallwayGridData;

typedef struct GenesisInteriorGridElement
{
	char rotation;
	GenesisInteriorElement *element[3];
	GenesisInteriorRoom *room;
	GenesisInteriorHallway *hallway;
	U8 dummy : 1;
	U8 dont_place : 1;
	U8 place_cap : 1;
	U8 place_door : 1;
	int hallway_segment;
	S32 height;
	S32 edge_heights[4];
	const char *spawn_point_name;
	const char *cap_name;
} GenesisInteriorGridElement;

typedef struct GenesisInteriorGrid
{
	int width;
	int height;
	GenesisInteriorTag **tag_field;
	GenesisInteriorGridElement *data;
} GenesisInteriorGrid;

typedef struct GenesisInteriorPatternInternal
{
	const char *pattern_name; // pooled string
	int width, height;
} GenesisInteriorPatternInternal;

typedef struct GenesisInteriorPatternList
{
	GenesisInteriorPatternInternal **list;
} GenesisInteriorPatternList;

// The current running state of the algorithm
typedef struct GenesisInteriorState
{
	GenesisToPlaceState to_place;			// List of GroupDefs to create

	GenesisZoneInterior *layout;			// The interior layout
	GenesisInteriorKit *kit;				// Kit containing rules, infrastructure
	GenesisInteriorRoom **rooms;			// Rooms in original order
	GenesisInteriorHallwayGroup **hallway_groups;// Hallways in original order
	F32 spacing;							// Kit-specified grid spacing
	F32 floor_height;						// Kit-specified distance between floors
	GenesisInteriorElement **cap_elements;  // List of door cap elements
	GenesisInteriorElement **door_elements; // List of moving door elements

	GenesisBackdrop *backdrop;				// Has lighting info

	MersenneTable *random_table;			// Mersenne table to use for layout computations
	GenesisInteriorRoom **room_list;		// DAG of pointers into the "rooms" array
	int bounds[4];							// Total outside bounds of our room placement area
	GenesisInteriorGrid grid[3];			// Grid for calculating actual element placement
	bool no_sharing;						// Don't share objects between missions

	bool override_positions;				// Don't try and calculate positions for all the rooms (UGC mode)

	StashTable pattern_table;				// Hashtable of GenesisInteriorTag -> GenesisInteriorPatternList
} GenesisInteriorState;

void genesisPrintGrid(GenesisInteriorGrid *grid, char *name);

//////////////////////////////////////////////////////////////////
// Global Flags
//////////////////////////////////////////////////////////////////

AUTO_COMMAND;
void genesisInteriorEnableDebugImages(void)
{
	genesis_interior_debug_images = true;
}

AUTO_COMMAND;
void genesisInteriorEnableVerboseMode(void)
{
	genesis_interior_verbose_mode = true;
}

//////////////////////////////////////////////////////////////////
// Interior Kit Library & Utilities
//////////////////////////////////////////////////////////////////

void genesisReloadInteriorKit(const char* relpath, int when)
{
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, RefSystem_GetDictionaryHandleFromNameOrHandle(GENESIS_INTERIORS_DICTIONARY) );
}

//Fills in the filename_no_path of the interiorkit during load
AUTO_FIXUPFUNC;
TextParserResult genesisFixupInteriorKit(GenesisInteriorKit *kit, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];
			if (kit->name)
				StructFreeString(kit->name);
			getFileNameNoExt(name, kit->filename);
			kit->name = StructAllocString(name);
		}
	}
	return 1;
}

static __forceinline char genesisBufferGet( char* buffer, int buffer_width, int buffer_height, int x, int z, int offset )
{
	//assert(0 <= x && x < buffer_width);
	//assert(0 <= z && z < buffer_height);

	return buffer[(buffer_width*z + x) * 4 + offset];
	
}

static __forceinline void genesisBufferSet( char* buffer, int buffer_width, int buffer_height, int x, int z, int offset, char val )
{
	//assert(0 <= x && x < buffer_width);
	//assert(0 <= z && z < buffer_height);

	buffer[(buffer_width*z + x) * 4 + offset] = val;
	
}

// Tests if tag1 is equal to or a descendant of tag2
__forceinline bool genesisDerivesFromTag(GenesisInteriorTag *tag1, GenesisInteriorTag *tag2)
{
	return (tag1 == tag2) || (tag1 && tag1->parent && genesisDerivesFromTag(tag1->parent, tag2));
}

GenesisInteriorTag *genesisInteriorFindTag(GenesisInteriorKit *kit, const char *name)
{
	int i;
	for (i = 0; i < eaSize(&kit->interior_tags); i++)
		if (!stricmp(kit->interior_tags[i]->name, name))
			return kit->interior_tags[i];
	return NULL;
}

GenesisInteriorKit *genesisInteriorKitCreate()
{
	return StructCreate(parse_GenesisInteriorKit);
}

bool genesisInteriorKitAppend(GenesisInteriorKit *kit, const char *name)
{
	int i, j, k;
	bool ret = false;
	REF_TO(GenesisInteriorKit) add_kit;
	GenesisInteriorKit *add_kit_ptr;
	SET_HANDLE_FROM_STRING(GENESIS_INTERIORS_DICTIONARY, name, add_kit);
	if (add_kit_ptr = GET_REF(add_kit))
	{
		ret = true;
		for (i = 0; i < eaSize(&add_kit_ptr->elements); i++)
		{
			GenesisInteriorElement *el = StructClone(parse_GenesisInteriorElement, add_kit_ptr->elements[i]);
			el->primary_object.geometry_uid = 0;
			for (j = 0; j < eaSize(&el->additional_objects); j++)
				el->additional_objects[j]->geometry_uid = 0;
			el->tag = NULL;
			eaPush(&kit->elements, el);
		}
		for (i = 0; i < eaSize(&add_kit_ptr->interior_tags); i++)
		{
			GenesisInteriorTag *tag = StructClone(parse_GenesisInteriorTag, add_kit_ptr->interior_tags[i]);
			eaDestroy(&tag->details);
			tag->parent = NULL;
			tag->light = NULL;
			for (j = 0; j < eaSize(&tag->connections); j++)
				for (k = 0; k < eaSize(&tag->connections[j]->relations); k++)
					tag->connections[j]->relations[k]->tag = NULL;
			eaPush(&kit->interior_tags, tag);
		}
	}

	REMOVE_HANDLE(add_kit);
	return ret;
}

void genesisInteriorKitDestroy(GenesisInteriorKit *kit)
{
	StructDestroy(parse_GenesisInteriorKit, kit);
}

static void genesisInteriorKitFixupElementObject(GenesisInteriorKit *kit, GenesisInteriorElementObject *object)
{
	if (object->geometry_uid == 0 && object->geometry)
	{
		GroupDef *def = objectLibraryGetGroupDefByName(object->geometry, true);
		if (def)
		{
			object->geometry_uid = def->name_uid;
		}
		else
		{
			ErrorFilenamef(kit->filename, "Missing object: %s", object->geometry);
		}
	}
}

static bool genesisInteriorKitSetup(GenesisInteriorState *state, GenesisInteriorKit *kit)
{
	int i, j, k, l;
	for (i = 0; i < eaSize(&kit->interior_tags); i++)
	{
		GenesisInteriorTag *tag = kit->interior_tags[i];
		// Tag Parent
		if (!tag->parent && tag->parent_name && tag->parent_name[0])
		{
			for (j = 0; j < eaSize(&kit->interior_tags); j++)
			{
				GenesisInteriorTag *parent_tag = kit->interior_tags[j];
				if (i != j && !stricmp(tag->parent_name, parent_tag->name))
				{
					tag->parent = parent_tag;
					break;
				}
			}
			if (!tag->parent)
			{
				ErrorFilenamef(kit->filename, "Tag references missing parent tag: %s", tag->parent_name);
				return false;
			}
		}
		// Tag Light
		if (!tag->light && tag->light_name && tag->light_name[0])
		{
			for (j = 0; j < eaSize(&kit->interior_tags); j++)
			{
				GenesisInteriorTag *light_tag = kit->interior_tags[j];
				if (i != j && !stricmp(tag->light_name, light_tag->name))
				{
					tag->light = light_tag;
					break;
				}
			}
			if (!tag->light)
				ErrorFilenamef(kit->filename, "Tag references missing light tag: %s", tag->parent_name);
		}
		// Tag Detail
		eaSetSize(&tag->details, eaSize(&tag->detail_names));
		for (k = 0; k < eaSize(&tag->detail_names); k++)
		{
			if (!tag->details[k] && tag->detail_names[k] && tag->detail_names[k][0])
			{
				for (j = 0; j < eaSize(&kit->interior_tags); j++)
				{
					GenesisInteriorTag *detail_tag = kit->interior_tags[j];
					if (i != j && !stricmp(tag->detail_names[k], detail_tag->name))
					{
						tag->details[k] = detail_tag;
						break;
					}
				}
				if (!tag->details[k])
					ErrorFilenamef(kit->filename, "Tag references missing detail tag: %s", tag->detail_names[k]);
			}
		}

		// Connection relations
		for (k = 0; k < eaSize(&tag->connections); k++)
		{
			for (l = 0; l < eaSize(&tag->connections[k]->relations); l++)
			{
				GenesisInteriorTagRelation *relation = tag->connections[k]->relations[l];
				if (!relation->tag)
				{
					for (j = 0; j < eaSize(&kit->interior_tags); j++)
					{
						GenesisInteriorTag *rel_tag =  kit->interior_tags[j];
						if (!stricmp(relation->tag_name, rel_tag->name))
						{
							relation->tag = rel_tag;
							break;
						}
					}
					if (relation->tag)
					{
						for (j = 0; j < eaSize(&kit->interior_tags); j++)
						{
							GenesisInteriorTag *derives_tag =  kit->interior_tags[j];
							if (genesisDerivesFromTag(derives_tag, relation->tag))
							{
								GenesisInteriorTagConnection *new_conn = StructCreate(parse_GenesisInteriorTagConnection);
								GenesisInteriorTagRelation *new_relation = StructCreate(parse_GenesisInteriorTagRelation);
								new_conn->sides = relation->allow_flags;
								new_relation->allow_flags = tag->connections[k]->sides;
								new_relation->tag = tag;
								new_relation->tag_name = StructAllocString(new_relation->tag->name);
								eaPush(&new_conn->relations, new_relation);
								eaPush(&derives_tag->connections, new_conn);
							}
						}
					}
					else
					{
						ErrorFilenamef(kit->filename, "Tag relation references missing tag: %s", relation->tag_name);
					}
				}
			}
		}
	}
	for (i = 0; i < eaSize(&kit->elements); i++)
	{
		GenesisInteriorElement *element = kit->elements[i];
		genesisInteriorKitFixupElementObject(kit, &element->primary_object);
		for (j = 0; j < eaSize(&element->additional_objects); j++)
			genesisInteriorKitFixupElementObject(kit, element->additional_objects[j]);

		// Element Tag
		if (!element->tag)
		{
			for (j = 0; j < eaSize(&kit->interior_tags); j++)
			{
				GenesisInteriorTag *tag = kit->interior_tags[j];
				if (!stricmp(element->tag_name, tag->name))
				{
					element->tag = tag;
					break;
				}
			}
			if (!element->tag)
			{
				ErrorFilenamef(kit->filename, "Element references missing tag: %s", element->tag_name);
				return false;
			}
		}

		if (eaSize(&element->patterns) > 0)
		{
			GenesisInteriorPatternList *tag_pattern_list = NULL;
			GenesisInteriorTag *root_tag = element->tag;
			while (root_tag->parent)
				root_tag = root_tag->parent;
			if (!stashFindPointer(state->pattern_table, root_tag, &tag_pattern_list))
			{
				tag_pattern_list = calloc(1, sizeof(GenesisInteriorPatternList));
				stashAddPointer(state->pattern_table, root_tag, tag_pattern_list, true);
			}
			for (j = 0; j < eaSize(&element->patterns); j++)
			{
				GenesisInteriorPatternInternal *pattern = NULL;
				for (k = 0; k < eaSize(&tag_pattern_list->list); k++)
					if (tag_pattern_list->list[k]->pattern_name == element->patterns[j]->pattern_name)
					{
						pattern = tag_pattern_list->list[k];
						break;
					}
					if (!pattern)
					{
						pattern = calloc(1, sizeof(GenesisInteriorPatternInternal));
						pattern->pattern_name = element->patterns[j]->pattern_name;
						eaPush(&tag_pattern_list->list, pattern);
					}
					for (k = 0; k < eaSize(&element->patterns[j]->coords); k++)
					{
						if (element->patterns[j]->coords[k]->coord[0] >= pattern->width)
							pattern->width = element->patterns[j]->coords[k]->coord[0]+1;
						if (element->patterns[j]->coords[k]->coord[1] >= pattern->height)
							pattern->height = element->patterns[j]->coords[k]->coord[1]+1;
					}
					element->patterns[j]->internal = pattern;
			}
		}
	}
	return true;
}

static bool genesisInteriorKitGeoValidate(GenesisInteriorKit *interior_kit, char *geometry)
{
	GroupDef *def = NULL;
	if(!geometry)
		return true;
	
	def = objectLibraryGetGroupDefByName(geometry, false);
	if(!def)
	{
		ErrorFilenamef(interior_kit->filename, "Interior Kit references geometry that does not exist. Name: (%s)", geometry);
		return false;
	}
	return true;
}

static bool genesisInteriorKitValidate(GenesisInteriorKit *interior_kit)
{
	int i, j;
	bool ret = true;
	//Check that elements point to valid geometries
	for ( i=0; i < eaSize(&interior_kit->elements); i++ )
	{
		GenesisInteriorElement *element = interior_kit->elements[i];
		if(!genesisInteriorKitGeoValidate(interior_kit, element->primary_object.geometry))
			ret = false;
		for ( j=0; j < eaSize(&element->additional_objects); j++ )
		{
			if(!genesisInteriorKitGeoValidate(interior_kit, element->additional_objects[j]->geometry))
				ret = false;
		}
		
	}
	if(!genesisInteriorKitGeoValidate(interior_kit, interior_kit->key_light))
		ret = false;
	if(interior_kit->light_details && !genesisDetailKitValidate(interior_kit->light_details, interior_kit->filename))
		ret = false;

	//Check for sound info
	if(interior_kit->sound_info && IsClient() && wl_state.sound_event_exists_func)
	{
		for ( i=0; i < eaSize(&interior_kit->sound_info->amb_sounds); i++ )
		{
			const char *amb_sound = interior_kit->sound_info->amb_sounds[i];
			if(!wl_state.sound_event_exists_func(amb_sound))
			{
				ErrorFilenamef(interior_kit->filename, "Interior Kit references ambient sound that does not exist. Name: (%s)", amb_sound);
				ret = false;
			}
		}	
		for ( i=0; i < eaSize(&interior_kit->sound_info->amb_hallway_sounds); i++ )
		{
			const char *amb_sound = interior_kit->sound_info->amb_hallway_sounds[i];
			if(!wl_state.sound_event_exists_func(amb_sound))
			{
				ErrorFilenamef(interior_kit->filename, "Interior Kit references ambient sound that does not exist. Name: (%s)", amb_sound);
				ret = false;
			}
		}	
//		TODO sfenton: Add validation for GAELayers
// 		dictea = resDictGetEArrayStruct(g_GAEMapDict);
// 		for(i=0; i<eaSize(&dictea->ppReferents); i++)
// 		{
// 			map = (GameAudioEventMap*)dictea->ppReferents[i];
// 			if(!map->is_global)
// 			{	
	}

	return ret;
}

static int genesisInteriorKitValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisInteriorKit *pInteriorKit, U32 userID)
{
	switch(eType)
	{
	case RESVALIDATE_CHECK_REFERENCES:
		genesisInteriorKitValidate(pInteriorKit);		
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void genesisInitInteriorKitLibrary(void)
{
	genesis_interiors_dict = RefSystem_RegisterSelfDefiningDictionary(GENESIS_INTERIORS_DICTIONARY, false, parse_GenesisInteriorKit, true, false, NULL);
	resDictMaintainInfoIndex(genesis_interiors_dict, NULL, NULL, ".Tags", NULL, NULL);
	resDictManageValidation(genesis_interiors_dict, genesisInteriorKitValidateCB);
}

void genesisLoadInteriorKitLibrary()
{
	if (!areEditorsPossible() || genesis_interiors_dict_loaded)
		return;
	resLoadResourcesFromDisk(genesis_interiors_dict, "genesis/interiors/kits", ".interiorkit", "GenesisInteriorKits.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/interiors/kits/*.interiorkit", genesisReloadInteriorKit);
	genesis_interiors_dict_loaded = true;
}

//////////////////////////////////////////////////////////////////
// Element Matching & Utilities
//////////////////////////////////////////////////////////////////

static bool genesisInteriorElementHasTag(GenesisInteriorTag *element_tag, GenesisInteriorTag *tag)
{
	GenesisInteriorTag *el_tag = element_tag;
	while (el_tag)
	{
		if (el_tag == tag)
			return true;

		el_tag = el_tag->parent;
	}
	if (tag && tag->fallback_to_parent && tag->parent)
		return element_tag == tag->parent;

	return false;
}

static bool genesisMatchElement(GenesisInteriorTag *tag1, GenesisInteriorConnectionSide side1, 
						 GenesisInteriorTag *tag2, GenesisInteriorConnectionSide side2)
{
	int i, j;
	while (tag2)
	{
		bool found = false;
		for (i = eaSize(&tag2->connections)-1; i >= 0; --i)
		{
			GenesisInteriorTagConnection *connection = tag2->connections[i];
			if (connection->sides & side2)
			{
				for (j = eaSize(&connection->relations)-1; j >= 0; --j)
				{
					GenesisInteriorTagRelation *relation = connection->relations[j];
					if ((relation->allow_flags & side1) && genesisInteriorElementHasTag(tag1, relation->tag))
						return true;
				}
				found = true;
			}
		}
		if (found)
			return false;

		tag2 = tag2->parent;
	}
	return false;
}

static GenesisInteriorConnectionSide genesisInteriorRotateSide(GenesisInteriorConnectionSide side, char rotation)
{
	if (rotation == CONNECTION_ROTATION_ROTATE180)
	{
		switch (side)
		{
			xcase CONNECTION_SIDE_LEFT:
				return CONNECTION_SIDE_RIGHT;
			xcase CONNECTION_SIDE_RIGHT:
				return CONNECTION_SIDE_LEFT;
			xcase CONNECTION_SIDE_BACK:
				return CONNECTION_SIDE_FRONT;
			xcase CONNECTION_SIDE_FRONT:
				return CONNECTION_SIDE_BACK;
		};
	}
	if (rotation == CONNECTION_ROTATION_ROTATE270)
	{
		switch (side)
		{
			xcase CONNECTION_SIDE_LEFT:
				return CONNECTION_SIDE_BACK;
			xcase CONNECTION_SIDE_RIGHT:
				return CONNECTION_SIDE_FRONT;
			xcase CONNECTION_SIDE_BACK:
				return CONNECTION_SIDE_RIGHT;
			xcase CONNECTION_SIDE_FRONT:
				return CONNECTION_SIDE_LEFT;
		};
	}
	if (rotation == CONNECTION_ROTATION_ROTATE90)
	{
		switch (side)
		{
			xcase CONNECTION_SIDE_LEFT:
				return CONNECTION_SIDE_FRONT;
			xcase CONNECTION_SIDE_RIGHT:
				return CONNECTION_SIDE_BACK;
			xcase CONNECTION_SIDE_BACK:
				return CONNECTION_SIDE_LEFT;
			xcase CONNECTION_SIDE_FRONT:
				return CONNECTION_SIDE_RIGHT;
		};
	}
	return side;
}

static int genesisMatchElementRotation(GenesisInteriorStep step, int *edge_heights, int start_dir)
{
	int delta;
	int rot;
	int ret = 0;
	if (step == INTERIOR_STEP_ANY)
		return 0xf;

	delta = (int)step;
	
	for (rot = 0; rot < 4; rot++)
	{
		if (edge_heights[(rot+start_dir)%4] == delta)
			ret |= (1<<rot);
	}
	return ret;
}

static GenesisInteriorElement *genesisInteriorFindSuitableElement(GenesisInteriorState *state, GenesisInteriorPatternInternal *pattern, MersenneTable *table, GenesisInteriorGrid *grid,
								   int x, int z, char *rotation)
{
	int i, j, r, t, idx, count;
	static const int dx[] = { 0, 1, 0, -1 };
	static const int dz[] = { -1, 0, 1, 0 };
	static const GenesisInteriorConnectionSide sides[] = { CONNECTION_SIDE_BACK, CONNECTION_SIDE_LEFT, CONNECTION_SIDE_FRONT, CONNECTION_SIDE_RIGHT };
	GenesisInteriorGridElement *neighbors[4] = { 0 };
	GenesisInteriorTag *neighbor_tags[4] = { 0 };
	char *rotations;
	if (!state->kit->elements)
		return NULL;

	if (!grid->tag_field[x+z*grid->width])
		return NULL;

	rotations = ScratchAlloc(eaSize(&state->kit->elements));
	for (i = 0; i < 4; i++)
	{
		int px = x+dx[i];
		int pz = z+dz[i];
		if (px >= 0 && px < grid->width &&
			pz >= 0 && pz < grid->height)
		{
			if (grid->data[px+pz*grid->width].element[0])
			{
				neighbors[i] = &grid->data[px+pz*grid->width];
			}
			else if (grid->tag_field[px+pz*grid->width])
			{
				neighbor_tags[i] = grid->tag_field[px+pz*grid->width];
			}
		}
			
		if (!neighbors[i] && !neighbor_tags[i])
		{
			neighbor_tags[i] = genesisInteriorFindTag(state->kit, "None");
		}
	}
	for (j = 0; j < eaSize(&state->kit->elements); j++)
	{
		GenesisInteriorElement *candidate = state->kit->elements[j];
		if (!genesisInteriorElementHasTag(candidate->tag, grid->tag_field[x+z*grid->width]))
		{
			rotations[j] = 0;
			continue;
		}
		if (pattern)
		{
			int l, m;
			bool found_pattern = false;
			for (l = 0; l < eaSize(&candidate->patterns); l++)
			{
				GenesisInteriorPattern *candidate_pattern = candidate->patterns[l];
				if (candidate_pattern->internal == pattern)
				{
					if (candidate_pattern->any_coord)
					{
						found_pattern = true;
					}
					else
					{
						int cx = x%pattern->width;
						int cz = z%pattern->height;
						for (m = 0; m < eaSize(&candidate_pattern->coords); m++)
						{
							if (cx == candidate_pattern->coords[m]->coord[0] &&
								cz == candidate_pattern->coords[m]->coord[1])
							{
								found_pattern = true;
								break;
							}
						}
					}
					break;
				}
			}
			if (!found_pattern)
			{
				rotations[j] = 0;
				continue;
			}
		}
		rotations[j] = 0xf; // All 4 possible rotations
		rotations[j] &= genesisMatchElementRotation(candidate->tag->back_step, grid->data[x+z*grid->width].edge_heights, 0);
		rotations[j] &= genesisMatchElementRotation(candidate->tag->left_step, grid->data[x+z*grid->width].edge_heights, 1);
		rotations[j] &= genesisMatchElementRotation(candidate->tag->front_step, grid->data[x+z*grid->width].edge_heights, 2);
		rotations[j] &= genesisMatchElementRotation(candidate->tag->right_step, grid->data[x+z*grid->width].edge_heights, 3);
		for (i = 0; i < 4; i++)
		{
			if (neighbors[i])
			{
				GenesisInteriorConnectionSide side2 = genesisInteriorRotateSide(sides[i], neighbors[i]->rotation);
				GenesisInteriorConnectionSide side1 = genesisInteriorRotateSide(sides[i],CONNECTION_ROTATION_ROTATE180);
				for (r = 0; r < 4; r++)
				{
					if ((rotations[j] & (1<<r)) &&
						(!genesisMatchElement(candidate->tag, side1, neighbors[i]->element[0]->tag, side2) ||
						!genesisMatchElement(neighbors[i]->element[0]->tag, side2, candidate->tag, side1)))
						rotations[j] &= ~(1<<r);
					side1 = genesisInteriorRotateSide(side1,CONNECTION_ROTATION_ROTATE90);
				}
			}
			else if (neighbor_tags[i])
			{
				GenesisInteriorConnectionSide side1 = genesisInteriorRotateSide(sides[i],CONNECTION_ROTATION_ROTATE180);
				for (r = 0; r < 4; r++)
				{
					if (rotations[j] & (1<<r))
					{
						// Try all possible neighbor rotations
						bool found = false;
						for (t = 0; t < eaSize(&state->kit->interior_tags); t++)
						{
							GenesisInteriorTag *tag = state->kit->interior_tags[t];
							if (genesisDerivesFromTag(tag, neighbor_tags[i]))
							{
								int r2;
								GenesisInteriorConnectionSide side2 = sides[i];
								for (r2 = 0; r2 < 4; r2++)
								{
									if (genesisMatchElement(candidate->tag, side1, tag, side2) &&
										genesisMatchElement(tag, side2, candidate->tag, side1))
									{
										found = true;
										break;
									}
									side2 = genesisInteriorRotateSide(side2,CONNECTION_ROTATION_ROTATE90);
								}
							}
							if (found)
								break;
						}
						if (!found)
							rotations[j] &= ~(1<<r);
					}
					side1 = genesisInteriorRotateSide(side1,CONNECTION_ROTATION_ROTATE90);
				}
			}
			if (rotations[j] == 0)
			{
				break;
			}
		}
	}
	count = 0;
	for (i = 0; i < eaSize(&state->kit->elements); i++)
		if (rotations[i] > 0)
			count++;
	if (count >= 0)
	{
		idx = ((randomMersenneF32(table)*0.5f)+0.5f) * count;
		for (i = 0; i < eaSize(&state->kit->elements); i++)
			if (rotations[i] > 0)
			{
				if (idx == 0)
				{
					*rotation = 0;
					do
					{
						int rot = 1<<(int)((randomMersenneF32(table)*2)+2);
						if (rotations[i] & rot)
							*rotation = rot;
					} while (*rotation == 0);
					ScratchFree(rotations);
					return state->kit->elements[i];
				}
				idx--;
			}
	}
	ScratchFree(rotations);
	return NULL;
}

static GenesisInteriorElement *genesisInteriorFindSuitableDetailElement(GenesisInteriorState *state, MersenneTable *table, GenesisInteriorElement *element_in)
{
	int i, j, k, idx;
	GenesisInteriorTag *tag = element_in->tag;
	GenesisInteriorElement *ret;
	GenesisInteriorElement **element_list = NULL;
	F32 rand = randomMersenneF32(table);
	if (rand < 0)
		return NULL;
	while (tag)
	{
		if (eaSize(&tag->details) > 0)
		{
			for (i = 0; i < eaSize(&tag->details); i++)
			{
				for (j = 0; j < eaSize(&state->kit->elements); j++)
				{
					GenesisInteriorElement *element = state->kit->elements[j];
					if (element->tag && genesisDerivesFromTag(element->tag, tag->details[i]))
					{
						bool found = false;
						for (k = 0; k < eaSize(&element_list); k++)
							if (element_list[k] == element)
							{
								found = true;
								break;
							}
						if (!found)
							eaPush(&element_list, element);
					}
				}
			}
		}
		tag = tag->parent;
	}

	if (!element_list)
		return NULL;

	idx = rand*eaSize(&element_list);
	ret = element_list[idx];

	eaDestroy(&element_list);

	return ret;
}

static GenesisInteriorElement *genesisInteriorFindSuitableLight(GenesisInteriorState *state, MersenneTable *table, GenesisInteriorElement *element)
{
	int i, idx;
	GenesisInteriorElement *ret;
	GenesisInteriorElement **element_list = NULL;
	F32 rand = fabs(randomMersenneF32(table));
	if (!element->tag->light)
		return NULL;

	for (i = 0; i < eaSize(&state->kit->elements); i++)
	{
		GenesisInteriorElement *derives_element = state->kit->elements[i];
		if (genesisDerivesFromTag(derives_element->tag, element->tag->light))
			eaPush(&element_list, derives_element);
	}

	if (!element_list)
		return NULL;

	idx = rand*eaSize(&element_list);
	ret = element_list[idx];

	eaDestroy(&element_list);

	return ret;
}

//////////////////////////////////////////////////////////////////
// Utilities to handle room rotation
//////////////////////////////////////////////////////////////////

static void genesisInteriorRotatePoint(GenesisInteriorRoom *room, int x_in, int z_in, int *out_x, int *out_z)
{
	// clock-wise rotation
	switch (room->rot)
	{
	xcase 0:
		*out_x = x_in + room->x;
		*out_z = z_in + room->z;
	xcase 1:
		*out_x = z_in + room->x;
		*out_z = room->width-1 - x_in + room->z;
	xcase 2:
		*out_x = room->width - 1 - x_in + room->x;
		*out_z = room->depth - 1 - z_in + room->z;
	xcase 3:
		*out_x = room->depth - 1 - z_in + room->x;
		*out_z = x_in + room->z;
	};
}

static void genesisInteriorInvRotatePoint(GenesisInteriorRoom *room, int x_in, int z_in, int *out_x, int *out_z)
{
	// clock-wise rotation
	switch (room->rot)
	{
	xcase 0:
		*out_x = x_in - room->x;
		*out_z = z_in - room->z;
	xcase 1:
		*out_x = room->width-1 + room->z - z_in;
		*out_z = x_in - room->x;
	xcase 2:
		*out_x = room->width - 1 + room->x - x_in;
		*out_z = room->depth - 1 + room->z - z_in;
	xcase 3:
		*out_x = z_in - room->z;
		*out_z = room->depth - 1 + room->x - x_in;
	};
}


static void genesisInteriorGetDoorPos(GenesisInteriorRoom *room, int door_idx, int *door_x, int *door_z, int *dir, bool offset)
{
	int x_in = room->doors[door_idx]->x;
	int z_in = room->doors[door_idx]->z;

	if (dir)
		*dir = 0;

	if (offset && !room->cap)
	{
		// Offset out one space
		if (x_in == 0)
		{
			x_in--;
			if (dir)
				*dir = 1;
		}
		else if (x_in == room->width-1)
		{
			x_in++;
			if (dir)
				*dir = 1;
		}

		if (z_in == 0)
		{
			z_in--;
		}
		else if (z_in == room->depth-1)
		{
			z_in++;
		}
	}

	if (dir && room->rot % 2 == 1)
		*dir = 1-*dir;

	genesisInteriorRotatePoint(room, x_in, z_in, door_x, door_z);
}

//////////////////////////////////////////////////////////////////
// STEP 1: Initialization code
//////////////////////////////////////////////////////////////////

static GenesisInteriorRoom* genesisInteriorFindRoomByName(GenesisInteriorState *state, char *name)
{
	int i;
	for ( i=0; i < eaSize(&state->rooms); i++ )
	{
		GenesisInteriorRoom *room = state->rooms[i];
		if (stricmp(room->src_name, name) == 0)
			return room;
	}
	return NULL;
}

static bool genesisInteriorInitialize(GenesisInteriorState *state, ZoneMapInfo *zmap_info, GenesisZoneInterior *interior, U32 seed)
{
	int i, j;
	char buf[256];
	GenesisInteriorKit *ref_kit;
	GenesisInteriorTag *cap_tag, *door_tag;

	state->layout = interior;
	state->override_positions = interior->override_positions;
	state->random_table = mersenneTableCreate(seed);
	state->pattern_table = stashTableCreateAddress(10);
	state->backdrop = interior->backdrop;

	state->kit = genesisInteriorKitCreate();

	if (eaSize(&interior->rooms) <= 0)
		return false;

	// Append ruleset first
	if (!genesisInteriorKitAppend(state->kit, "DefaultTags"))
	{
		genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "Infrastructure", NULL, "Project specific DefaultTags file not found.");
		return false;
	}

	// Append infrastructure kit
	if ((ref_kit = GET_REF(interior->room_kit)) == NULL ||
		!genesisInteriorKitAppend(state->kit, ref_kit->name))
	{
		genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(interior->layout_name), "Interior kit \"%s\" not found.", REF_STRING_FROM_HANDLE(interior->room_kit));
		return false;
	}
	state->kit->name = StructAllocString(ref_kit->name);
	state->kit->room_padding = ref_kit->room_padding;
	state->kit->light_top = ref_kit->light_top;
	state->kit->floor_bottom = ref_kit->floor_bottom;
	state->kit->straight_door_only = ref_kit->straight_door_only;
	state->kit->no_occlusion = ref_kit->no_occlusion;
	state->spacing = (ref_kit->spacing > 0) ? ref_kit->spacing : 40.f;
	state->floor_height = (ref_kit->floor_height > 0) ? ref_kit->floor_height : INTERIOR_HEIGHT_INC;

	// Append light kit
	if ((ref_kit = GET_REF(interior->light_kit)) != NULL &&
		genesisInteriorKitAppend(state->kit, ref_kit->name))
	{
		state->kit->key_light = StructAllocString(ref_kit->key_light);
		state->kit->light_details = StructClone(parse_GenesisDetailKit, ref_kit->light_details);
		state->kit->light_dummy.light_details = state->kit->light_details;
		state->kit->light_dummy.detail_density = 100;
	}

	// Set up rooms
	for (i = 0; i < eaSize(&interior->rooms); i++)
	{
		GenesisZoneMapRoom *src_room = interior->rooms[i];
		GenesisInteriorRoom *dest_room = calloc(1, sizeof(GenesisInteriorRoom));
		dest_room->src = src_room;
		dest_room->src_name = src_room->room.name;
		dest_room->detail_seed = src_room->detail_seed;
		dest_room->width = src_room->room.width;
		dest_room->depth = src_room->room.depth;
		dest_room->areas = src_room->room.areas;
		dest_room->geo = src_room->room.library_piece;
		dest_room->use_proj_lights = src_room->room.use_proj_lights;
		dest_room->no_details_or_paths = src_room->room.no_details_or_paths;
		dest_room->volumized = src_room->room.volumized;
		dest_room->cap = src_room->hallway_room;
		for (j = 0; j < eaSize(&src_room->room.doors); j++)
		{
			GenesisInteriorDoor *new_door = calloc(1, sizeof(GenesisInteriorDoor));
			new_door->name = src_room->room.doors[j]->name;
			new_door->x = src_room->room.doors[j]->x;
			new_door->y = src_room->room.doors[j]->y;
			new_door->z = src_room->room.doors[j]->z;
			new_door->rotation = src_room->room.doors[j]->rotation;
			new_door->always_open = src_room->room.doors[j]->always_open;
			if (state->override_positions)
				new_door->z = src_room->room.depth-1-src_room->room.doors[j]->z;
			eaPush(&dest_room->doors, new_door);
		}
		eaPush(&state->rooms, dest_room);
	}

	// Final setup
	if (!genesisInteriorKitSetup(state, state->kit))
		return false;

	// Set up paths
	for ( j=0; j < eaSize(&interior->paths); j++ )
	{
		GenesisZoneMapPath *src_path = interior->paths[j];
		GenesisInteriorHallwayGroup *new_hallway_group = calloc(1, sizeof(GenesisInteriorHallwayGroup));
		GenesisInteriorHallway *last_hallway = NULL;
		GenesisInteriorHallway *new_hallway = NULL;
		int start_cnt = eaSize(&src_path->start_rooms);
		int end_cnt = eaSize(&src_path->end_rooms);
		int start_end_cnt = start_cnt + end_cnt;
		int hallway_cnt = (start_end_cnt-2)*2 + 1;
		int junction_cnt = start_end_cnt - 2;
		int min_length_inc=0, max_length_inc=0;
		int min_length_cur=0, max_length_cur=0;
		int min_length_end=0, max_length_end=0;

		eaPush(&state->hallway_groups, new_hallway_group);
		new_hallway_group->src = src_path;
		new_hallway_group->src_name = StructAllocString(src_path->path.name);
		new_hallway_group->no_details_or_paths = src_path->path.no_details_or_paths;

		if(eaSize(&src_path->start_rooms) <= 0 || eaSize(&src_path->end_rooms) <= 0)
		{
			genesisRaiseError(GENESIS_FATAL_ERROR, 
				genesisMakeTempErrorContextPath(src_path->path.name, interior->layout_name), 
				"Path must have at least one start and one exit room.");
			return false;
		}
		assert(junction_cnt >= 0);

		min_length_inc = (src_path->path.min_length / (F32)(junction_cnt+1)) + 0.5f;
		max_length_inc = (src_path->path.max_length / (F32)(junction_cnt+1)) + 0.5f;
		min_length_cur = min_length_inc;
		max_length_cur = max_length_inc;
		min_length_end = min_length_inc*(junction_cnt+1);
		max_length_end = max_length_inc*(junction_cnt+1);

		//Make the first hallway and set it's start room to the first start room
		new_hallway = calloc(1, sizeof(GenesisInteriorHallway));
		eaPush(&new_hallway_group->hallways, new_hallway);
		new_hallway->parent_hallway = new_hallway_group;
		new_hallway->start_door = new_hallway->end_door = -1;
		new_hallway->min_length = min_length_end;
		new_hallway->max_length = max_length_end;
		new_hallway->start_room = genesisInteriorFindRoomByName(state, src_path->start_rooms[0]);
		new_hallway->start_room_has_door = true;
		if(!new_hallway->start_room)
		{
			genesisRaiseError(GENESIS_FATAL_ERROR, 
				genesisMakeTempErrorContextPath(src_path->path.name, interior->layout_name), 
				"Could not find start room %s.", src_path->start_rooms[0]);
			return false;
		}

		last_hallway = new_hallway;

		for ( i=0; i < junction_cnt; i++, min_length_cur += min_length_inc, max_length_cur += max_length_inc)
		{
			//Make a new junction as a room
			bool start_room = (i+1) < start_cnt;
			char *room_name = (start_room ? src_path->start_rooms[i+1] : src_path->end_rooms[(i+1)-start_cnt]);
			GenesisInteriorRoom *new_junction = calloc(1, sizeof(GenesisInteriorRoom));
			GenesisInteriorRoom *room;
			sprintf(buf, "%s_junc_%02d", src_path->path.name, i);
			new_junction->src_name = StructAllocString(buf);
			new_junction->cap = true;
			new_junction->no_src = true;
			new_junction->parent_hallway = new_hallway_group;
			new_junction->width = 1;
			new_junction->depth = 1;
			eaPush(&state->rooms, new_junction);
			eaPush(&new_hallway_group->rooms, new_junction);

			//Attach the last hall
			last_hallway->end_room = new_junction;
			last_hallway->min_length = min_length_inc;//override the length
			last_hallway->max_length = max_length_inc;

			//Make a hall for the current room
			room = genesisInteriorFindRoomByName(state, room_name);
			if(!room)
			{
				genesisRaiseError(GENESIS_FATAL_ERROR, 
					genesisMakeTempErrorContextPath(src_path->path.name, interior->layout_name), 
					"Could not find %s room %s.", (start_room ? "start" : "end"), room_name);
				return false;
			}
			new_hallway = calloc(1, sizeof(GenesisInteriorHallway));
			eaPush(&new_hallway_group->hallways, new_hallway);
			new_hallway->parent_hallway = new_hallway_group;
			new_hallway->start_door = new_hallway->end_door = -1;
			if(start_room)
			{
				new_hallway->start_room = room;
				new_hallway->end_room = new_junction;
				new_hallway->start_room_has_door = true;
				new_hallway->min_length = min_length_cur;
				new_hallway->max_length = max_length_cur;
			}
			else
			{
				new_hallway->start_room = new_junction;
				new_hallway->end_room = room;
				new_hallway->end_room_has_door = true;
				new_hallway->min_length = min_length_end - min_length_cur;
				new_hallway->max_length = max_length_end - max_length_cur;
			}

			//Make a hall to the next junction
			new_hallway = calloc(1, sizeof(GenesisInteriorHallway));
			eaPush(&new_hallway_group->hallways, new_hallway);
			new_hallway->parent_hallway = new_hallway_group;
			new_hallway->start_door = new_hallway->end_door = -1;
			new_hallway->start_room = new_junction;
			new_hallway->min_length = min_length_end - min_length_cur;
			new_hallway->max_length = max_length_end - max_length_cur;

			last_hallway = new_hallway;
		}

		last_hallway->end_room = genesisInteriorFindRoomByName(state, src_path->end_rooms[end_cnt-1]);
		last_hallway->end_room_has_door = true;
		if(!last_hallway->end_room)
		{
			genesisRaiseError(GENESIS_FATAL_ERROR, 
				genesisMakeTempErrorContextPath(src_path->path.name, interior->layout_name), 
				"Could not find end room %s.", src_path->end_rooms[end_cnt-1]);
			return false;
		}
	}

	// Find cap & door elements
	cap_tag = genesisInteriorFindTag(state->kit, "Door_Cap");
	FOR_EACH_IN_EARRAY(state->kit->elements, GenesisInteriorElement, candidate)
	{
		if (genesisInteriorElementHasTag(candidate->tag, cap_tag))
		{
			eaPush(&state->cap_elements, candidate);
		}
	}
	FOR_EACH_END;

	door_tag = genesisInteriorFindTag(state->kit, "Door_Movable");
	FOR_EACH_IN_EARRAY(state->kit->elements, GenesisInteriorElement, candidate)
	{
		if (genesisInteriorElementHasTag(candidate->tag, door_tag))
		{
			eaPush(&state->door_elements, candidate);
		}
	}
	FOR_EACH_END;

	state->no_sharing = interior->no_sharing_detail;

	return true;
}

//////////////////////////////////////////////////////////////////
// STEP 2: Connect hallways & rooms
//////////////////////////////////////////////////////////////////

static GenesisInteriorRoom *genesisInteriorConnectHallways(GenesisInteriorState *state)
{
	int i, j, g, cg;
	int max_exit_count = 0;
	GenesisInteriorRoom *first_room = NULL;

	for (i = 0; i < eaSize(&state->rooms); i++)
		state->rooms[i]->exits = 0;

	if (state->override_positions)
	{
		for ( g=0; g < eaSize(&state->hallway_groups); g++ )
		{
			GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
			for (i = 0; i < eaSize(&hallway_group->hallways); i++)
			{
				GenesisInteriorHallway *hallway = hallway_group->hallways[i];
				GenesisInteriorRoom *start_room = hallway_group->hallways[i]->start_room;
				GenesisInteriorRoom *end_room = hallway_group->hallways[i]->end_room;
				GenesisInteriorDoor *door_start;
				GenesisInteriorDoor *door_end;
				hallway->start_door = hallway_group->src->start_doors[0];
				door_start = eaGet(&start_room->doors, hallway->start_door);

				hallway->end_door = hallway_group->src->end_doors[0];
				door_end = eaGet(&end_room->doors, hallway->end_door);

				if (door_start && door_end)
				{
					door_start->dir_flags |= GENESIS_DOOR_EXITING;
					door_start->hallway = hallway;

					door_end->dir_flags |= GENESIS_DOOR_ENTERING;
					door_end->hallway = hallway;
				}
				else
				{
					// Error?
				}
			}
		}
	}
	else
	{
		for ( g=0; g < eaSize(&state->hallway_groups); g++ )
		{
			GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
			for (i = 0; i < eaSize(&hallway_group->hallways); i++)
			{
				GenesisInteriorHallway *hallway = hallway_group->hallways[i];
				GenesisInteriorRoom *room = hallway_group->hallways[i]->start_room;

				// Start door
				if (hallway->start_door == -1)
				{
					bool found;
					hallway->start_door = 0;
					do
					{
						found = false;
						for ( cg=0; cg < eaSize(&state->hallway_groups) && !found; cg++ )
						{
							GenesisInteriorHallwayGroup *cmp_hallway_group = state->hallway_groups[cg];
							for (j = 0; j < eaSize(&cmp_hallway_group->hallways); j++)
							{
								GenesisInteriorHallway *cmp_hallway = cmp_hallway_group->hallways[j];
								if ( (g != cg || j != i) &&
									((cmp_hallway->start_room == room && cmp_hallway->start_door == hallway->start_door) ||
									(cmp_hallway->end_room   == room && cmp_hallway->end_door   == hallway->start_door)))
								{
									found = true;
									break;
								}
							}
						}
						if (found)
							hallway->start_door++;
					} while (found);
				}
				if (eaSize(&room->doors) <= hallway->start_door)
				{
					int orig_size = eaSize(&room->doors);
					eaSetSize(&room->doors, hallway->start_door+1);
					for (j = orig_size; j <= hallway->start_door; j++)
					{
						room->doors[j] = calloc(1, sizeof(GenesisInteriorDoor));
						room->doors[j]->x = room->doors[j]->z = -1;
					}
				}
				room->doors[hallway->start_door]->dir_flags |= GENESIS_DOOR_EXITING;
				assert(!room->doors[hallway->start_door]->hallway);
				room->doors[hallway->start_door]->hallway = hallway;
			}
		}

		for ( g=0; g < eaSize(&state->hallway_groups); g++ )
		{
			GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
			for (i = 0; i < eaSize(&hallway_group->hallways); i++)
			{
				GenesisInteriorHallway *hallway = hallway_group->hallways[i];
				GenesisInteriorRoom *room = hallway_group->hallways[i]->end_room;

				// End door
				if (hallway->end_door == -1)
				{
					bool found;
					hallway->end_door = 0;
					do
					{
						found = false;
						for ( cg=0; cg < eaSize(&state->hallway_groups) && !found; cg++ )
						{
							GenesisInteriorHallwayGroup *cmp_hallway_group = state->hallway_groups[cg];
							for (j = 0; j < eaSize(&cmp_hallway_group->hallways); j++)
							{
								GenesisInteriorHallway *cmp_hallway = cmp_hallway_group->hallways[j];
								if ( (g != cg || j != i) &&
									((cmp_hallway->start_room == room && cmp_hallway->start_door == hallway->end_door) ||
									(cmp_hallway->end_room   == room && cmp_hallway->end_door   == hallway->end_door)))
								{
									found = true;
									break;
								}
							}
						}
						if (found)
							hallway->end_door++;
					} while (found);
				}
				if (eaSize(&room->doors) <= hallway->end_door)
				{
					int orig_size = eaSize(&room->doors);
					eaSetSize(&room->doors, hallway->end_door+1);
					for (j = orig_size; j <= hallway->end_door; j++)
					{
						room->doors[j] = calloc(1, sizeof(GenesisInteriorDoor));
						room->doors[j]->x = room->doors[j]->z = -1;
					}
				}
				room->doors[hallway->end_door]->dir_flags |= GENESIS_DOOR_ENTERING;
				assert(!room->doors[hallway->end_door]->hallway);
				room->doors[hallway->end_door]->hallway = hallway;
			}
		}
	}

	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			hallway_group->hallways[i]->start_room->exits++;
			hallway_group->hallways[i]->end_room->exits++;
		}
	}
	assert(state->rooms);
	max_exit_count = state->rooms[0]->exits;
	first_room = state->rooms[0];
	for (i = 1; i < eaSize(&state->rooms); i++)
	{
		GenesisInteriorRoom *room = state->rooms[i];
		if (room->exits > max_exit_count)
		{
			max_exit_count = room->exits;
			first_room = room;
		}
	}

	return first_room;
}

static void genesisInteriorAddPortalDoors(GenesisInteriorState *state, GenesisMissionRequirements **mission_reqs)
{
	int i, j, k, d;

	for ( i=0; i < eaSize(&mission_reqs); i++ ) {
		GenesisMissionRequirements *mission_req = mission_reqs[i];
		for ( j=0; j < eaSize(&mission_req->roomRequirements); j++ ) {
			GenesisMissionRoomRequirements *room_req = mission_req->roomRequirements[j];
			if(eaSize(&room_req->doors) > 0 && stricmp_safe(state->layout->layout_name, room_req->layoutName)==0) {
				GenesisInteriorRoom *room = genesisInteriorFindRoomByName(state, room_req->roomName);
				for ( k=0; k < eaSize(&room_req->doors); k++ )
				{
					GenesisMissionDoorRequirements *door_req = room_req->doors[k];
					GenesisInteriorDoor *new_door;
					bool door_exists = false;
					if(!room) {
						genesisRaiseError(GENESIS_FATAL_ERROR, 
							genesisMakeTempErrorContextPortal(door_req->doorName, mission_req->missionName, state->layout->layout_name), 
							"Can not find room (%s) for portal", room_req->roomName);
						break;
					}

					for ( d=0; d < eaSize(&room->doors); d++ ) {
						GenesisInteriorDoor *test_door = room->doors[d];
						if(stricmp_safe(test_door->name, door_req->doorName)==0) {
							door_exists = true;
							break;
						}
					}
					
					if(!door_exists) {
						new_door = calloc(1, sizeof(GenesisInteriorDoor));
						new_door->name = StructAllocString(door_req->doorName);
						new_door->random = true;
						eaPush(&room->doors, new_door);
					}
				}
			}
		}
	}
}

// Give names to any doors that do not already have one
static void genesisInteriorEnsureDoorsHaveNames(GenesisInteriorState *state)
{
	int i, j;
	for ( i=0; i < eaSize(&state->rooms); i++ ) {
		GenesisInteriorRoom *room = state->rooms[i];
		char door_name[2] = "A";
		for ( j=0; j < eaSize(&room->doors); j++ ) {
			GenesisInteriorDoor *door = room->doors[j];
			if(!door->name) {
				door->name = StructAllocString(door_name);
				door_name[0]++;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////
// STEP 3: Sort rooms by connectivity
//////////////////////////////////////////////////////////////////

static bool genesisInteriorInsertRoomIntoList(GenesisInteriorState *state, GenesisInteriorRoom *room)
{
	int i, j, g;
	for (i = 0; i < eaSize(&state->room_list); i++)
	{
		if (state->room_list[i] == room)
			return false; // Room already in list
	}
	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];
			if (hallway->start_room == room)
			{
				for (j = 0; j < eaSize(&state->room_list); j++)
					if (state->room_list[j] == hallway->end_room)
					{
						eaPush(&state->room_list, room);
						return true;
					}
			}
			else if (hallway->end_room == room)
			{
				for (j = 0; j < eaSize(&state->room_list); j++)
					if (state->room_list[j] == hallway->start_room)
					{
						eaPush(&state->room_list, room);
						return true;
					}
			}
		}
	}
	return false;
}

static void genesisInteriorGenerateRoomList(GenesisInteriorState *state, GenesisInteriorRoom *first_room)
{
	int i;
	if (state->override_positions)
	{
		// Order doesn't matter since everything is already placed
		for (i = 0; i < eaSize(&state->rooms); i++)
			eaPush(&state->room_list, state->rooms[i]);
	}
	else
	{
		bool found;
		eaPush(&state->room_list, first_room);
		do {
			found = false;
			for (i = 0; i < eaSize(&state->rooms); i++)
			{
				found |= genesisInteriorInsertRoomIntoList(state, state->rooms[i]);
			}
		} while (found);

		// Connectivity validation is done earlier, we do not need to
		// error here.
		// add remaining rooms
		for (i = 0; i < eaSize(&state->rooms); i++)
		{
			if (eaFind(&state->room_list, state->rooms[i]) < 0) {
				eaPush(&state->room_list, state->rooms[i]);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////
// STEP 4: Figure out where walls are & other area modifiers
//////////////////////////////////////////////////////////////////

static void genesisInteriorProcessRoomAreas(GenesisInteriorState *state, GenesisInteriorRoom *room)
{
	int x, z, k;
	bool had_error=false;
	room->grid = calloc(1, room->width*room->depth);
	for (k = 0; k < eaSize(&room->areas); k++)
	{
		GenesisRoomRectArea *area = room->areas[k];
		if (area->type == GENESIS_ROOM_RECT_SUBTRACT)
		{
			for (x = area->x; x < area->x+area->width; x++)
			{
				for (z = area->z; z < area->z+area->depth; z++)
				{
					if(	x >= 0 && x < room->width &&
						z >= 0 && z < room->depth )
					{
						room->grid[x+z*room->width] = 2; // Outside
					}
					else
					{
						had_error = true;
					}
				}
			}
		}
	}
	if(had_error) {
		genesisRaiseError(GENESIS_FATAL_ERROR, 
			genesisMakeTempErrorContextRoom(room->src_name, state->layout->layout_name), 
			"RoomDef has a subtract area that is out of bounds");
	}
	for (x = 0; x < room->width; x++)
		for (z = 0; z < room->depth; z++)
		{
			bool collide = false;
			bool adjecent = false;
			for (k = 0; k < eaSize(&room->areas); k++)
			{
				GenesisRoomRectArea *area = room->areas[k];
				if (area->type == GENESIS_ROOM_RECT_SUBTRACT)
				{
				   if (x >= area->x && x < area->x+area->width &&
						z >= area->z && z < area->z+area->depth)
				   {
					   collide = true;
					   break;
				   }
				   if (((x == area->x-1) || (x == area->x+area->width)) &&
						(z >= area->z-1) && (z <= area->z+area->depth))
				   {
					   adjecent = true;
				   }
				   else if (((z == area->z-1) || (z == area->z+area->depth)) &&
						   (x >= area->x) && (x < area->x+area->width))
				   {
					   adjecent = true;
				   }
				}
			}
			if (!collide && 
				(adjecent || (x == 0) || (x == room->width-1) ||
				 (z == 0) || (z == room->depth-1)))
			{
				room->grid[x+z*room->width] = 1; // Wall
			}
		}
}

//////////////////////////////////////////////////////////////////
// STEP 5: Place the rooms
//////////////////////////////////////////////////////////////////

// Find a (random) suitable configuration of door positions for a room
static bool genesisInteriorCreateDoorConfiguration(GenesisInteriorState *state, GenesisInteriorRoom *room)
{
	int x, z, i, j, k;
	int *available_pts = NULL;
	bool found_random = false;
	F32 room_diagnal_dist;
	
	//Init all the random doors in the room and ensure that random doors exist
	for (j = 0; j < eaSize(&room->doors); j++)
	{
		GenesisInteriorDoor *door = room->doors[j];
		if (door->random || door->x < 0 || door->z < 0)
		{
			door->x = door->z = -1000;
			door->random = true;
			found_random = true;
		}
	}
	if (!found_random)
		return true;

	//Find all the valid places to put a door.
	for (x = 0; x < room->width; x++)
	{
		for (z = 0; z < room->depth; z++)
		{
			bool valid_point = false;
			// Only place doors on "straight" wall segments
			if (x > 0 && x < room->width-1 && (z==0 || z==room->depth-1) &&
				room->grid[x+z*room->width] == 1 &&
				room->grid[x-1+z*room->width] == 1 &&
				room->grid[x+1+z*room->width] == 1)
			{
				valid_point = true;
			}
			else if (z > 0 && z < room->depth-1 && (x==0 || x==room->width-1) &&
				room->grid[x+z*room->width] == 1 &&
				room->grid[x+(z-1)*room->width] == 1 &&
				room->grid[x+(z+1)*room->width] == 1)
			{
				valid_point = true;
			}
			if (valid_point || room->cap)
			{
				eaiPush(&available_pts, x);
				eaiPush(&available_pts, z);
			}
		}
	}

	if (eaiSize(&available_pts) == 0)
	{
		genesisRaiseError(GENESIS_FATAL_ERROR, 
			genesisMakeTempErrorContextRoom(room->src_name, state->layout->layout_name), 
			"RoomDef has no valid locations for a door.");
		return false;
	}

	randomMersennePermuteN(state->random_table, available_pts, eaiSize(&available_pts)/2, 2);
	room_diagnal_dist = sqrt(SQR(room->width) + SQR(room->depth));

	// Find locations for all the random doors
	for (i = 0; i < eaSize(&room->doors); i++)
	{
		GenesisInteriorDoor *new_door = room->doors[i];
		bool valid_point = true;
		int px, pz;

		if (!new_door->random)
			continue;

		//Find a point within the available points
		for ( j=0; j < eaiSize(&available_pts); j+=2 )
		{
			valid_point = true;
			px = available_pts[j];
			pz = available_pts[j+1];

			//Ensure that it is not too close to placed doors
			for ( k=0; k < eaSize(&room->doors); k++ )
			{
				GenesisInteriorDoor *placed_door = room->doors[k];
				int dx = placed_door->x;
				int dz = placed_door->z;
				//If placed
				if(!placed_door->random || k < i)
				{
					bool opposite = false; 
					F32 dist;
					//Don't place one square away to any placed doors
					if( (dz == pz && dx >= px-1 && dx <= px+1) ||
						(dx == px && dz >= pz-1 && dz <= pz+1) )
					{
						valid_point = false;
						break;
					}
					//Opposite means one is an entrance the other is an exit
					if((new_door->dir_flags & GENESIS_DOOR_ENTERING) && (placed_door->dir_flags & GENESIS_DOOR_EXITING))
						opposite = true;
					if((new_door->dir_flags & GENESIS_DOOR_EXITING) && (placed_door->dir_flags & GENESIS_DOOR_ENTERING))
						opposite = true;
					dist = sqrt(SQR(dx-px) + SQR(dz-pz));
					//We want to keep entrances away from exits, and entrances near entrances because
					//if entrances get too far apart, then it makes it impossible to place exits (and visa-versa).
					if( ((dist >  room_diagnal_dist/2.0f) && !opposite) || 
						((dist <= room_diagnal_dist/3.1f) &&  opposite) )
					{
						valid_point = false;
						break;
					}
				}
			}
			if(room->cap)
				valid_point = true;
			if(valid_point)
				break;
		}
		if(valid_point)
		{
			new_door->x = px;
			new_door->z = pz;
		}
		else
		{
			eaiDestroy(&available_pts);
			return false;
		}
	}
	eaiDestroy(&available_pts);
	return true;
}

// Flood fill outward from a door to find the shortest distance to potential new room door
static void genesisInteriorDrawHallwayField(GenesisInteriorHallway *hallway, char *buffer, int buffer_width, int buffer_height,
											int door_x, int door_z, int door_dir)
{
	int i, x, z;
	static const int dx[] = { -1, 0, 1, 0 };
	static const int dz[] = { 0, -1, 0, 1 };
	bool changed;
	for (x = 0; x < buffer_width; x++)
		for (z = 0; z < buffer_height; z++)
			bufferSetEx(buffer, x, z, HALL_CHANNEL, 0);
	bufferSetEx(buffer, door_x, door_z, HALL_CHANNEL, 1);
	do
	{
		changed = false;
		for (x = 0; x < buffer_width; x++)
			for (z = 0; z < buffer_height; z++)
			{
				if ((door_dir == 0 && z == door_z && abs(x-door_x) == 1) || 
					(door_dir == 1 && x == door_x && abs(z-door_z) == 1))
					continue;
				if ((bufferGet(buffer, x, z) & 0x80) == 0)
					for (i = 0; i < 4; i++)
					{
						int sx = x+dx[i];
						int sz = z+dz[i];
						if (sx >= 0 && sx < buffer_width &&
							sz >= 0 && sz < buffer_height)
						{
							if (bufferGetEx(buffer, sx, sz, HALL_CHANNEL) > 0 &&
								(bufferGetEx(buffer, x, z, HALL_CHANNEL) == 0 || 
								 bufferGetEx(buffer, sx, sz, HALL_CHANNEL) < bufferGetEx(buffer, x, z, HALL_CHANNEL)-1) &&
								bufferGetEx(buffer, sx, sz, HALL_CHANNEL) < hallway->max_length)
							{
								bufferSetEx(buffer, x, z, HALL_CHANNEL, bufferGetEx(buffer, sx, sz, HALL_CHANNEL)+1);
								changed = true;
							}
						}
					}
			}
	} while (changed);
}

// Use the flood fill above to mark acceptable places to put a room
static void genesisInteriorDrawHallwaySign(GenesisInteriorHallway *hallway, char *buffer, int buffer_width, int buffer_height,
										char sign, int door_x, int door_z, int door_dir)
{
	int x, z;
	genesisInteriorDrawHallwayField(hallway, buffer, buffer_width, buffer_height, door_x, door_z, door_dir);
	for (x = 0; x < buffer_width; x++)
		for (z = 0; z < buffer_height; z++)
		{
			if (bufferGetEx(buffer, x, z, HALL_CHANNEL) >= hallway->min_length)
				bufferSet(buffer, x, z, (sign | bufferGet(buffer, x, z)));
		}
}

// Either attempt or execute drawing a hallway between two given doors
static bool genesisInteriorCreateHallway(GenesisInteriorHallway *hallway, 
										 char *buffer, int buffer_width, int buffer_height, int bounds[4],
										int start_x, int start_z, int start_dir, int end_x, int end_z, int end_dir, bool include_last)
{
	int i, x, z, dirx = 0, dirz = 0, cur_index = -1;
	bool first_point = true;
	static const int dx[] = { -1, 0, 1, 0 };
	static const int dz[] = { 0, -1, 0, 1 };
	genesisInteriorDrawHallwayField(hallway, buffer, buffer_width, buffer_height, start_x, start_z, start_dir);
	x = end_x;
	z = end_z;
	eaiClear(&hallway->points);
	eaiPush(&hallway->points, x+bounds[0]);
	eaiPush(&hallway->points, 0);
	eaiPush(&hallway->points, z+bounds[1]);
	eaiPush(&hallway->points, 0);
	while (x != start_x || z != start_z)
	{
		bool found = false;
		int dist = bufferGetEx(buffer, x, z, HALL_CHANNEL);
		for (i = 0; i < 4; i++)
		{
			int sx = x+dx[i];
			int sz = z+dz[i];
			int buffer_dist;

			if (first_point && (i % 2) == end_dir)
			{
				continue;
			}

			buffer_dist = bufferGetEx(buffer, sx, sz, HALL_CHANNEL);
			if (sx >= 0 && sz >= 0 && sx < buffer_width && sz < buffer_height &&
				(((bufferGet(buffer, sx, sz) & 0x80) == 0) || (buffer_dist == 1 && !include_last)) &&
				buffer_dist > 0 &&
				buffer_dist < dist)
			{
				x = sx;
				z = sz;
				if (dx[i] != dirx || dz[i] != dirz)
					cur_index++;
				dirx = dx[i];
				dirz = dz[i];
				found = true;
				break;
			}
		}
		if (!found)
			return false;
		eaiPush(&hallway->points, x+bounds[0]);
		eaiPush(&hallway->points, 0);
		eaiPush(&hallway->points, z+bounds[1]);
		eaiPush(&hallway->points, cur_index);
		first_point = false;
	}
	return true;
}

void genesisGenerateHallwayFromControlPoints(int *in_points, int **hallway_points)
{
	if (eaiSize(&in_points) > 0)
	{
		int j, segment = -1;
		eaiClear(hallway_points);
		for (j = 0; j < eaiSize(&in_points)-2; j += 2)
		{
			int hx, hz, dir;
			dir = (in_points[j+2] > in_points[j]) ? 1 : -1;
			segment++;
			for (hx = in_points[j]; hx != in_points[j+2]; hx += dir)
			{
				eaiPush(hallway_points, hx);
				eaiPush(hallway_points, 0);
				eaiPush(hallway_points, in_points[j+1]);
				eaiPush(hallway_points, segment);
			}
			segment++;
			dir = (in_points[j+3] > in_points[j+1]) ? 1 : -1;
			for (hz = in_points[j+1]; hz != in_points[j+3]; hz += dir)
			{
				eaiPush(hallway_points, in_points[j+2]);
				eaiPush(hallway_points, 0);
				eaiPush(hallway_points, hz);
				eaiPush(hallway_points, segment);
			}
		}
		eaiPush(hallway_points, in_points[j]);
		eaiPush(hallway_points, 0);
		eaiPush(hallway_points, in_points[j+1]);
		eaiPush(hallway_points, segment);
	}
}

// Test a given room position to see if there are obstacles or if the hallways don't work out
static bool genesisTestRoomPosition(GenesisInteriorState *state,
											char *buffer, int buffer_width, int buffer_height,
											int buffer_bounds[4],
											GenesisInteriorRoom *room,
											int room_x, int room_z)
{
	int i, x, z;
	int j;
	int width = (room->rot%2) == 0 ? room->width : room->depth;
	int depth = (room->rot%2) == 0 ? room->depth : room->width;

	assert(room_x > 0 && room_z > 0 &&
		(room_x+width) < (buffer_bounds[2]-buffer_bounds[0]) &&
		(room_z+depth) < (buffer_bounds[3]-buffer_bounds[1]));

	for (x = 0; x < width; x++)
		for (z = 0; z < depth; z++)
			if (bufferGet(buffer, x + room_x, z + room_z) & 0x80)
				return false; // Intersects another room
			else if (!room->cap)
			{
				bufferSet(buffer, x + room_x, z + room_z, 0x80);
				if (genesis_interior_debug_images)
					bufferSetEx(buffer, x+room_x, z+room_z, 1, 0x80); // Just for debugging
			}

	for (i = 0; i < eaSize(&room->doors); i++)
	{
		GenesisInteriorDoor *door = room->doors[i];
		int room_bounds[] = { room_x, room_z, room_x+width-1, room_z+depth-1 };
		int door_x, door_z, start_x, start_z;
		bool include_last;
		bool from_start;
		int start_dir, door_dir;

		if (!door->hallway)
			continue;

		if (room == door->hallway->start_room)
		{
			if (!door->hallway->end_room->placed)
				continue;
			genesisInteriorGetDoorPos(door->hallway->end_room, door->hallway->end_door, &start_x, &start_z, &start_dir, true);
			include_last = !door->hallway->end_room->cap;
			from_start = true;
		}
		else
		{
			if (!door->hallway->start_room->placed)
				continue;
			genesisInteriorGetDoorPos(door->hallway->start_room, door->hallway->start_door, &start_x, &start_z, &start_dir, true);
			include_last = !door->hallway->start_room->cap;
			from_start = false;
		}

		genesisInteriorGetDoorPos(room, i, &door_x, &door_z, &door_dir, true); // Assumes room->x and room->z are still zero
		if ((bufferGet(buffer, door_x+room_x, door_z+room_z) & 0x40 >> i) == 0)
			return false; // No hallway available at this door

		if (!genesisInteriorCreateHallway(door->hallway, 
			buffer, buffer_width, buffer_height, buffer_bounds,
			start_x-buffer_bounds[0], start_z-buffer_bounds[1], state->kit->straight_door_only ? start_dir : 2,
			door_x+room_x, door_z+room_z, state->kit->straight_door_only ? door_dir : 2, include_last))
		{
			return false;
		}

		door->hallway->from_start = from_start;

		for (j = 0; j < eaiSize(&door->hallway->points); j += 4)
		{
			int px = door->hallway->points[j]-buffer_bounds[0];
			int pz = door->hallway->points[j+2]-buffer_bounds[1];
			if (px != room_x || pz != room_z)
				bufferSet(buffer, px, pz, 0x80);
			if (genesis_interior_debug_images)
				bufferSetEx(buffer, px, pz, 2, 0x80); // Just for debugging
		}
	}

	if (room->cap)
	{
		bufferSet(buffer, room_x, room_z, 0x80);
		if (genesis_interior_debug_images)
			bufferSetEx(buffer, room_x, room_z, 1, 0x80); // Just for debugging
	}
	// Success! Place room here
	room->x = room_x;
	room->z = room_z;
	room->placed = true;
	return true;
}

// Recursively test potential room positions to find a suitable spot
static void genesisFindSuitablePosRecursive(GenesisInteriorState *state,
											char *buffer, int buffer_width, int buffer_height,
											int buffer_bounds[4],
											GenesisInteriorRoom *room,
											int bounds[4],
											char *temp_buffer)
{
	if (bounds[2] < 2 && bounds[3] < 2)
	{
		memcpy(temp_buffer, buffer, buffer_width*buffer_height*4);
		if (genesisTestRoomPosition(state, temp_buffer, buffer_width, buffer_height, buffer_bounds, room, bounds[0], bounds[1]))
		{
			// Success!
			memcpy(buffer, temp_buffer, buffer_width*buffer_height*4);
		}
	}
	else
	{
		int order[4];
		int child_bounds[4][4] = { { bounds[0], bounds[1], bounds[2]/2, bounds[3]/2 },
			{ bounds[0]+bounds[2]/2, bounds[1], bounds[2]/2, bounds[3]/2 },
			{ bounds[0], bounds[1]+bounds[3]/2, bounds[2]/2, bounds[3]/2 },
			{ bounds[0]+bounds[2]/2, bounds[1]+bounds[3]/2, bounds[2]/2, bounds[3]/2 } };
		randomMersennePermutation4(state->random_table, order);
		genesisFindSuitablePosRecursive(state, buffer, buffer_width, buffer_height, buffer_bounds, room, child_bounds[order[0]], temp_buffer);
		if (room->placed)
			return;
		genesisFindSuitablePosRecursive(state, buffer, buffer_width, buffer_height, buffer_bounds, room, child_bounds[order[1]], temp_buffer);
		if (room->placed)
			return;
		genesisFindSuitablePosRecursive(state, buffer, buffer_width, buffer_height, buffer_bounds, room, child_bounds[order[2]], temp_buffer);
		if (room->placed)
			return;
		genesisFindSuitablePosRecursive(state, buffer, buffer_width, buffer_height, buffer_bounds, room, child_bounds[order[3]], temp_buffer);
		if (room->placed)
			return;
	}
}

// Sets a suitable position for the room, if one can be found anywhere on the grid
static void genesisFindSuitablePos(GenesisInteriorState *state, 
								   char *buffer, int buffer_width, int buffer_height,
								   int buffer_bounds[4],
								   GenesisInteriorRoom *room)
{
	char *temp_buffer = calloc(1, buffer_width*buffer_height*4);
	int width = (room->rot%2) == 0 ? room->width : room->depth;
	int depth = (room->rot%2) == 0 ? room->depth : room->width;
	int bounds[] = { 1, 1, buffer_width - (width+2), buffer_height - (depth+2) };
	room->placed = false;
	room->x = 0;
	room->z = 0;
	genesisFindSuitablePosRecursive(state, buffer, buffer_width, buffer_height, buffer_bounds, room, bounds, temp_buffer);
	SAFE_FREE(temp_buffer);
}

// Main workhorse routine; recursively attempts different placements of this room to satisfy the requirements of it and its children.
static bool genesisInteriorPlaceRoom(GenesisInteriorState *state, int parent_bounds[4], int idx, int rot, int *image_idx, int generate_timer_id)
{
	char *buffer;
	int buffer_width, buffer_height;
	GenesisInteriorRoom *room = state->room_list[idx];
	int bounds[4];
	int room_size = MAX(room->width, room->depth);
	int i, j, g, z, x, missing_door_count = 0, door_config;
	int max_hall_length = 0;

	// Clear hallway points for all connected hallways
	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		int hallway_group_length = 0;
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];
			if (hallway->start_room == room || hallway->end_room == room)
			{
				eaiClear(&hallway->points);
			}
			hallway_group_length += MAX(hallway->min_length, hallway->max_length) + 1;
		}
		max_hall_length = MAX(max_hall_length, hallway_group_length);
	}

	// Initialize bounds
	bounds[0] = parent_bounds[0] - (room_size + max_hall_length + 1);
	bounds[1] = parent_bounds[1] - (room_size + max_hall_length + 1);
	bounds[2] = parent_bounds[2] + (room_size + max_hall_length + 1);
	bounds[3] = parent_bounds[3] + (room_size + max_hall_length + 1);

	if (bounds[0] >= bounds[2] ||
		bounds[1] >= bounds[3])
		return false;

	// Create the buffer
	buffer_width = bounds[2]+1-bounds[0];
	buffer_height = bounds[3]+1-bounds[1];
	buffer = calloc(1, buffer_width*buffer_height*4);

	// Draw existing rooms into the buffer
	for (i = 0; i < idx; i++)
	{
		GenesisInteriorRoom *existing_room = state->room_list[i];
		int px = existing_room->x-bounds[0];
		int pz = existing_room->z-bounds[1];
		if(existing_room->cap && !state->kit->compact_junct)
		{
			// Make an X such so that hallways can't immediately turn.
			bufferSet(buffer, px, pz, 0x80);
			if(px > 0 && pz > 0)
				bufferSet(buffer, px-1, pz-1, 0x80);
			if(px > 0 && pz < buffer_height-1)
				bufferSet(buffer, px-1, pz+1, 0x80);
			if(px < buffer_width-1 && pz > 0)
				bufferSet(buffer, px+1, pz-1, 0x80);
			if(px < buffer_width-1 && pz < buffer_height-1)
				bufferSet(buffer, px+1, pz+1, 0x80);
		}
		else
		{
			int pw = (existing_room->rot%2) == 0 ? existing_room->width : existing_room->depth;
			int ph = (existing_room->rot%2) == 0 ? existing_room->depth : existing_room->width;
			for (z = pz; z < pz+ph; z++)
				for (x = px; x < px+pw; x++)
					bufferSet(buffer, x, z, 0x80);
		}
	}

	// Draw existing hallways into the buffer
	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			int door_num = -1;
			int door_x = -100, door_z = -100;
			int dir;
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];

			for (j = 0; j < eaiSize(&hallway->points); j += 4)
			{
				bufferSet(buffer, hallway->points[j]-bounds[0], hallway->points[j+2]-bounds[1], 0x80);
				if (genesis_interior_debug_images)
					bufferSetEx(buffer, hallway->points[j]-bounds[0], hallway->points[j+2]-bounds[1], 2, 0x80);
			}

			if (hallway->start_room == room &&
				hallway->end_room->placed)
			{
				genesisInteriorGetDoorPos(hallway->end_room, hallway->end_door, &door_x, &door_z, &dir, true);
				door_num = hallway->start_door;
			}
			else if (hallway->end_room == room &&
				hallway->start_room->placed)
			{
				genesisInteriorGetDoorPos(hallway->start_room, hallway->start_door, &door_x, &door_z, &dir, true);
				door_num = hallway->end_door;
			}
			if (door_num > -1)
			{
				if (room->doors[door_num]->random ||
					room->doors[door_num]->x < 0 ||
					room->doors[door_num]->z < 0)
				{
					room->doors[door_num]->random = true;
					missing_door_count++;
				}

				genesisInteriorDrawHallwaySign(hallway, buffer, buffer_width, buffer_height, (0x40>>door_num), door_x-bounds[0], door_z-bounds[1], state->kit->straight_door_only ? dir : 2);
				if (genesis_interior_debug_images)
					bufferSetEx(buffer, door_x-bounds[0], door_z-bounds[1], 2, 0x80); // Just for debugging
			}
		}
	}

	if (genesis_interior_debug_images)
	{
		char file_path[255];
		sprintf(file_path, "C:/ROOM_DEBUG/step_%03d_%d_A.tga", (*image_idx)++, idx);
		tgaSave(file_path, buffer, buffer_width, buffer_height, 3);
	}

	// Repeat for multiple door configurations
	for (door_config = 0; door_config < 1 + MAX_DOOR_CONFIG_ITERATION*missing_door_count; door_config++)
	{
		if (timerElapsed(generate_timer_id) > INTERIOR_MAX_PLACE_TIME)
		{
			printf("TIMEOUT.\n");
			SAFE_FREE(buffer);
			return false;
		}

		if (genesis_interior_verbose_mode)
		{
			printf("\n");
			for (i = 0; i < idx; i++)
				printf("  ");
			printf("Testing %s, %d/%d...", room->src_name, rot, door_config);
		}

		// Clear connected hallways at each attempt
		for ( g=0; g < eaSize(&state->hallway_groups); g++ )
		{
			GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
			for (i = 0; i < eaSize(&hallway_group->hallways); i++)
			{
				GenesisInteriorHallway *hallway = hallway_group->hallways[i];
				if (hallway->start_room == room || hallway->end_room == room)
					eaiClear(&hallway->points);
			}
		}

		// Create a new door configuration to test
		if (!genesisInteriorCreateDoorConfiguration(state, room))
		{
			if (genesis_interior_verbose_mode)
				printf("Door config failed.\n");
		}
		else
		{
			room->placed = false;
			room->rot = rot;

			// Try to place the room in this configuration
			genesisFindSuitablePos(state, buffer, buffer_width, buffer_height, bounds, room);

			if (room->placed)
			{
				int child_bounds[] = { 0, 0, 0, 0 };
				int pw = (rot%2) == 0 ? room->width : room->depth;
				int ph = (rot%2) == 0 ? room->depth : room->width;
				room->x += bounds[0];
				room->z += bounds[1];

				// Calculate new bounds for recursion
				for (i = 0; i <= idx; i++)
				{
					int cw = (state->room_list[i]->rot%2) == 0 ? state->room_list[i]->width : state->room_list[i]->depth;
					int ch = (state->room_list[i]->rot%2) == 0 ? state->room_list[i]->depth : state->room_list[i]->width;
					child_bounds[0] = MIN(child_bounds[0], state->room_list[i]->x);
					child_bounds[1] = MIN(child_bounds[1], state->room_list[i]->z);
					child_bounds[2] = MAX(child_bounds[2], state->room_list[i]->x+cw-1);
					child_bounds[3] = MAX(child_bounds[3], state->room_list[i]->z+ch-1);
				}
				for ( g=0; g < eaSize(&state->hallway_groups); g++ )
				{
					GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
					for (i = 0; i < eaSize(&hallway_group->hallways); i++)
					{
						GenesisInteriorHallway *hallway = hallway_group->hallways[i];
						for (j = 0; j < eaiSize(&hallway->points); j+=4)
						{
							child_bounds[0] = MIN(child_bounds[0], hallway->points[j+0]);
							child_bounds[1] = MIN(child_bounds[1], hallway->points[j+2]);
							child_bounds[2] = MAX(child_bounds[2], hallway->points[j+0]);
							child_bounds[3] = MAX(child_bounds[3], hallway->points[j+2]);
						}
					}
				}

				// Do we have more rooms to place?
				if (idx < eaSize(&state->rooms)-1)
				{
					int rotRoom[4], count = 1;
					randomMersennePermutation4(state->random_table, rotRoom);
					count = 4;
					// Yes; recurse with the current placement and try placing all the children.
					for (i = 0; i < count; i++)
					{
						if (genesisInteriorPlaceRoom(state, child_bounds, idx+1, rotRoom[i], image_idx, generate_timer_id))
						{
							// All our children succeeded; we are done placing and can return.
							memcpy(parent_bounds, child_bounds, 4*sizeof(int));
							SAFE_FREE(buffer);
							return true;
						}
						if (timerElapsed(generate_timer_id) > INTERIOR_MAX_PLACE_TIME)
							return false;
					}

					// One of the children failed. Try another configuration.
					room->placed = false;
				}

				if (room->placed)
				{
					// We have just placed the last room. Save the bounds and return.
					memcpy(parent_bounds, child_bounds, 4*sizeof(int));
					if (genesis_interior_verbose_mode)
						printf("Placed. Done.\n");

					if (genesis_interior_debug_images)
					{
						char file_path[255];
						sprintf(file_path, "C:/ROOM_DEBUG/step_%03d_%d_D.tga", (*image_idx)++, idx);
						tgaSave(file_path, buffer, buffer_width, buffer_height, 3);
					}
				}

				break;
			}
		}
	}

	SAFE_FREE(buffer);

	return room->placed;
}

// Find a layout that places all the rooms and hallways in valid positions.
static bool genesisInteriorPlaceRooms(GenesisInteriorState *state)
{
	bool succeeded = false;
	// Place the first room at 0,0 with no rotation
	if (state->override_positions)
	{
		FOR_EACH_IN_EARRAY(state->room_list, GenesisInteriorRoom, room)
		{
			room->x = room->src->iPosX;
			room->height = room->src->iPosY;
			room->z = -1*(room->src->iPosZ+room->src->room.depth-1);
			room->placed = true;

			state->bounds[0] = MIN(state->bounds[0], room->x-1);
			state->bounds[1] = MIN(state->bounds[1], room->z-1);
			state->bounds[2] = MAX(state->bounds[2], room->x+room->src->room.width+1);
			state->bounds[3] = MAX(state->bounds[3], room->z+room->src->room.depth+1);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(state->hallway_groups, GenesisInteriorHallwayGroup, group)
		{
			GenesisZoneMapPath *path = group->src;
			GenesisInteriorHallway *hallway;
			assert(eaSize(&group->hallways) == 1);
			hallway = group->hallways[0];
			if (eaiSize(&path->control_points) >= 4)
			{
				int j;
				genesisGenerateHallwayFromControlPoints(path->control_points, &hallway->points);
				for (j = 0; j < eaiSize(&hallway->points); j+=4)
				{
					hallway->points[j+2] *= -1;
					state->bounds[0] = MIN(state->bounds[0], hallway->points[j]-1);
					state->bounds[1] = MIN(state->bounds[1], hallway->points[j+2]-1);
					state->bounds[2] = MAX(state->bounds[2], hallway->points[j]+1);
					state->bounds[3] = MAX(state->bounds[3], hallway->points[j+2]+1);
				}
				hallway->from_start = true;
			}
		}
		FOR_EACH_END;
		succeeded = true;
	}
	else
	{
		int image_idx = 0;
		int i;
		int door_config, missing_door_count = 0;
		int generateTimerID;
		int rot[4];
		GenesisInteriorRoom *first_room = state->room_list[0];

		first_room->x = 0;
		first_room->z = 0;
		first_room->placed = true;
		state->bounds[0] = first_room->x;
		state->bounds[1] = first_room->z;
		state->bounds[2] = first_room->x + first_room->width-1;
		state->bounds[3] = first_room->z + first_room->depth-1;

		generateTimerID = timerAlloc();
		timerStart(generateTimerID);

		for (i = 0; i < eaSize(&first_room->doors); i++)
		{
			GenesisInteriorDoor *door = first_room->doors[i];
			if (door->random || door->x < 0 || door->z < 0)
			{
				door->random = true;
				missing_door_count++;
			}
		}

		randomMersennePermutation4(state->random_table, rot);
		for (i = 0; i < 4; i++)
		{
			for (door_config = 0; door_config < 1 + MAX_DOOR_CONFIG_ITERATION*missing_door_count; door_config++)
			{
				if (genesis_interior_verbose_mode)
					printf("Initial door config %d: ", door_config);

				if (!genesisInteriorCreateDoorConfiguration(state, first_room))
				{
					if (genesis_interior_verbose_mode)
						printf("Door config failed.\n");
				}
				else
				{
					if (eaSize(&state->room_list) == 1 ||
							genesisInteriorPlaceRoom(state, state->bounds, 1, rot[i], &image_idx, generateTimerID))
					{
						succeeded = true;
						break;
					}
				}
			}
			if (succeeded)
				break;
		}

		timerFree(generateTimerID);
	}

	return succeeded;
}

//////////////////////////////////////////////////////////////////
// STEP 6: Create random vertical variation
//////////////////////////////////////////////////////////////////

int genesisInteriorCalculateHallwayMaxDelta(int *points)
{
	int j, ret = 0;
	for (j = 8; j < eaiSize(&points)-8; j += 4)
	{
		if (points[j-4] == points[j+4] ||
			points[j-2] == points[j+6])
			ret++;
	}
	return ret;
}

static void genesisInteriorCalculateRoomHeights(GenesisInteriorState *state, GenesisVertDir vert_dir, bool randomize)
{
	int i, j, k, g;
	int height_dir;

	// Calculate the maximum vertical displacement this hallway supports
	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];
			hallway->max_delta_height = genesisInteriorCalculateHallwayMaxDelta(hallway->points);
		}
	}

	if (randomize)
	{
		//If it is random or not, we still want to burn a seed
		height_dir = (randomMersenneF32(state->random_table) > 0) ? 1 : -1; // Up or down?
		if (vert_dir == GENESIS_PATH_DOWNHILL)
			height_dir = -1;
		else if (vert_dir == GENESIS_PATH_UPHILL)
			height_dir = 1;

		// Move the rooms up and down randomly without exceeding the hallways' tolerances
		if (vert_dir != GENESIS_PATH_FLAT)
		{
			for (i = 0; i < eaSize(&state->room_list)*6; i++)
			{
				int idx = randomMersenneIntRange(state->random_table, 0, eaSize(&state->room_list)-1);
				GenesisInteriorRoom *room = state->room_list[idx];
				int new_height = room->height+1;
				bool passes = true;

				for ( g=0; g < eaSize(&state->hallway_groups) && passes; g++ )
				{
					GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
					for (j = 0; j < eaSize(&hallway_group->hallways); j++)
					{
						GenesisInteriorHallway *hallway = hallway_group->hallways[j];
						if (hallway->start_room == room)
						{
							int delta_height = (hallway->end_room->height - new_height) * height_dir;
							if (delta_height < 0 || delta_height > hallway->max_delta_height)
							{
								passes = false;
								break;
							}
						}
						if (hallway->end_room == room)
						{
							int delta_height = (new_height - hallway->start_room->height) * height_dir;
							if (delta_height < 0 || delta_height > hallway->max_delta_height)
							{
								passes = false;
								break;
							}
						}
					}
				}
				if (passes)
					room->height = new_height;
			}
		}
	}

	// Actually draw the elevation changes into the hallways
	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			int *indices = NULL;
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];
			GenesisInteriorDoor *start_door = eaGet(&hallway->start_room->doors, hallway->start_door);
			GenesisInteriorDoor *end_door = eaGet(&hallway->end_room->doors, hallway->end_door);
			int hall_start_height = hallway->start_room->height + (start_door?start_door->y:0);
			int hall_end_height = hallway->end_room->height + (end_door?end_door->y:0);
			int delta_height = hall_start_height - hall_end_height;
			int start_height = hall_end_height;
			if (hallway->from_start)
			{
				delta_height = -delta_height;
				start_height = hall_start_height;
			}
			if (abs(delta_height) > hallway->max_delta_height) {
				genesisRaiseError(GENESIS_ERROR, 
					genesisMakeTempErrorContextPath(hallway_group->src_name, state->layout->layout_name), 
					"Path is too short for height difference between doors. Either lengthen the path or move rooms closer together!");
			}
			if (delta_height != 0)
			{
				for (j = 8; j < eaiSize(&hallway->points)-8; j += 4)
				{
					if (hallway->points[j-4] == hallway->points[j+4] ||
						hallway->points[j-2] == hallway->points[j+6])
						eaiPush(&indices, j);
				}
				randomMersennePermuteN(state->random_table, indices, eaiSize(&indices), 1);
				eaiSetSize(&indices, abs(delta_height));
				printf("Hallway %d:\n(%d) ", i, start_height);
				for (j = 0; j < eaiSize(&hallway->points); j += 4)
				{
					hallway->points[j+1] = start_height;
					for (k = 0; k < eaiSize(&indices); k++)
					{
						if (delta_height > 0 && j > indices[k])
							hallway->points[j+1]++;
						else if (delta_height < 0 && j >= indices[k])
							hallway->points[j+1]--;
					}
					printf("%d ", hallway->points[j+1]);
				}
				printf("(%d)\n", start_height+delta_height);
			}
			else
			{
				printf("Hallway %d:\n(%d) ", i, start_height);
				for (j = 0; j < eaiSize(&hallway->points); j += 4)
				{
					hallway->points[j+1] = start_height;
					printf("%d ", hallway->points[j+1]);
				}
				printf("(%d)\n", start_height+delta_height);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////
// STEP 7: Create a grid
//////////////////////////////////////////////////////////////////

static void genesisInteriorInitializeGrid(GenesisInteriorState *state)
{
	int i;
	for (i = 0; i < 3; i++)
	{
		state->grid[i].width = state->bounds[2]+1-state->bounds[0];
		state->grid[i].height = state->bounds[3]+1-state->bounds[1];
		state->grid[i].tag_field = calloc(1, sizeof(GenesisInteriorTag*)*state->grid[i].width*state->grid[i].height);
		state->grid[i].data = calloc(1, sizeof(GenesisInteriorGridElement)*state->grid[i].width*state->grid[i].height);
	}
}

//////////////////////////////////////////////////////////////////
// STEP 8: Create all the volumes that we will be populating
//////////////////////////////////////////////////////////////////

static void genesisInteriorCalcMinMaxHeight(GenesisInteriorKit *kit, GenesisInteriorTag **tags, F32 *min, F32 *max)
{
	int i, j;
	for (i = 0; i < eaSize(&kit->elements); i++)
	{
		GenesisInteriorElement *element = kit->elements[i];
		for ( j=0; j < eaSize(&tags); j++ )
		{
			if (genesisInteriorElementHasTag(element->tag, tags[j]))
			{
				GroupDef *def = objectLibraryGetGroupDef(element->primary_object.geometry_uid, true);
				if (def)
				{
					*min = MIN(*min, def->bounds.min[1]);
					*max = MAX(*max, def->bounds.max[1]);
				}
				break;
			}
		}
	}
}

static void genesisInteriorAddTag(GenesisInteriorKit *kit, GenesisInteriorTag ***tags, const char *name)
{
	GenesisInteriorTag *new_tag;
	new_tag = genesisInteriorFindTag(kit, name);
	if(new_tag)
		eaPush(tags, new_tag);
}

static void genesisInteriorSetupProjLight(GenesisInteriorState *state, GenesisToPlaceObject *light, F32 width, F32 depth, 
										  F32 floor_height_mid, GenesisInteriorHallwayGridType hall_type)
{
	GenesisToPlaceObject *child_light;
	GroupDef *child_light_def;
	GenesisInteriorLightingProps *light_props = &state->backdrop->int_light;
	F32 light_top = (state->kit->light_top ? state->kit->light_top : 38.0f);
	F32 floor_bottom = (state->kit->floor_bottom ? state->kit->floor_bottom : 0.0f) - 0.1f;
	F32 inner_angle = (light_props->inner_angle ? light_props->inner_angle : 15.0f);
	F32 outer_angle = (light_props->outer_angle ? light_props->outer_angle : 15.0f);
	F32 incline = atan2(state->floor_height, state->spacing);
	F32 incline_div = cosf(incline);
	F32 angle, height, length;
	F32 radius;

	if(inner_angle > outer_angle)
		SWAPF32(inner_angle, outer_angle);

	assert(incline_div);
	switch(hall_type)
	{
	case genesisIntRampTiltUpNorth:
	case genesisIntRampTiltUpSouth:
		depth /= incline_div;
		break;
	case genesisIntRampTiltUpWest:
	case genesisIntRampTiltUpEast:
		width /= incline_div;
		break;
	}

	//The angles the user specifies are for the shorter of the two directions
	//This finds the height and the outer angle for the longer direction
	angle = RAD(outer_angle);
	length = (width > depth ? width : depth); 
	height = (length*0.5f)/tan(angle);
	if(height < (light_top+40))
	{
		height = light_top+40;
		angle = atan2(length*0.5f, height);
		inner_angle = DEG(angle) * (inner_angle / outer_angle);
		outer_angle = DEG(angle);
	}
	else if(height > light_top+500)
	{
		height = light_top+500;
		angle = atan2(length*0.5f, height);
		inner_angle = DEG(angle) * (inner_angle / outer_angle);
		outer_angle = DEG(angle);	
	}
	length = (width > depth ? depth : width); 
	angle = atan2(length*0.5f, height);

	//If this is a ramp, then adjust the matrix as needed
	{
		bool not_a_ramp = false;
		Vec3 up_vec = {0,state->spacing,0};
		F32 move_dist = (height+floor_bottom)*sinf(incline);
		F32 tilt_height = (height+floor_bottom)*cosf(incline);
		switch(hall_type)
		{
		case genesisIntRampTiltUpNorth:
			light->mat[3][2] -= move_dist;
			up_vec[2] = -state->floor_height;
			break;
		case genesisIntRampTiltUpEast:
			light->mat[3][0] -= move_dist;
			up_vec[0] = -state->floor_height;
			break;
		case genesisIntRampTiltUpSouth:
			light->mat[3][2] += move_dist;
			up_vec[2] = state->floor_height;
			break;
		case genesisIntRampTiltUpWest:
			light->mat[3][0] += move_dist;
			up_vec[0] = state->floor_height;
			break;
		default:
			not_a_ramp = true;
		}
		if(not_a_ramp)
		{
			light->mat[3][1] += (height + floor_bottom);
		}
		else
		{
			light->mat[3][1] += (tilt_height + floor_height_mid);
			normalVec3(up_vec);
			mat3FromUpVector(up_vec, light->mat);
		}
	}

	if (!light->params)
		light->params = StructCreate(parse_GenesisProceduralObjectParams);

	if (!light->params->light_properties)
		light->params->light_properties = StructCreate(parse_WorldLightProperties);

	//Shorter direction
	if (width > depth)
		light->params->light_properties->fConeInner = MIN(inner_angle, outer_angle-0.25f);
	else
		light->params->light_properties->fCone2Inner = MIN(inner_angle, outer_angle-0.25f);

	if (width > depth)
		light->params->light_properties->fConeOuter = outer_angle;
	else
		light->params->light_properties->fCone2Outer = outer_angle;

	//Longer direction
	inner_angle = DEG(angle) * (inner_angle / outer_angle);
	outer_angle = DEG(angle);
	if (width > depth)
		light->params->light_properties->fCone2Inner = MIN(inner_angle, outer_angle-0.25f);
	else
		light->params->light_properties->fConeInner = MIN(inner_angle, outer_angle-0.25f);

	if (width > depth)
		light->params->light_properties->fCone2Outer = outer_angle;
	else
		light->params->light_properties->fConeOuter = outer_angle;

	//Other properties
	radius = height + 1.0f;
	light->params->light_properties->fRadiusInner = radius;
	light->params->light_properties->fRadius = radius + 0.1f;
	light->params->light_properties->eLightType = WleAELightController;
	light->params->light_properties->fShadowNearDist = (height+floor_bottom) - light_top;
	light->params->physical_properties.bVisible = 0;

	//Child light that has the color values
	child_light = calloc(1, sizeof(GenesisToPlaceObject));
	child_light_def = objectLibraryGetGroupDefFromRef(&light_props->child_light, false);
	if(child_light_def)
		child_light->uid = child_light_def->name_uid;
	else
		child_light->uid = -217859803; // Name: GenesisDefaultIntProjLight
	identityMat4(child_light->mat);
	copyVec3(light->mat[3], child_light->mat[3]);
	child_light->parent = light;
	eaPush(&state->to_place.objects, child_light);	
}

static bool genesisInteriorGetHallwayBounds(GenesisInteriorState *state, GenesisInteriorHallwayGroup *hallway_group, int *offest_x, int *offest_z, int *width, int *depth)
{
	int i, j;
	bool first=true;
	IVec2 bounds_min = {0,0};
	IVec2 bounds_max = {0,0};
	for (i = 0; i < eaSize(&hallway_group->hallways); i++)
	{
		GenesisInteriorHallway *hallway = hallway_group->hallways[i];

		for (j = 0; j < eaiSize(&hallway->points); j+=4)
		{
			int x = hallway->points[j+0]-state->bounds[0];
			int z = hallway->points[j+2]-state->bounds[1];
			if(first)
			{
				bounds_min[0] = bounds_max[0] = x;
				bounds_min[1] = bounds_max[1] = z;
				first = false;
			}
			else
			{
				if(x < bounds_min[0])
					bounds_min[0] = x;
				if(x > bounds_max[0])
					bounds_max[0] = x;
				if(z < bounds_min[1])
					bounds_min[1] = z;
				if(z > bounds_max[1])
					bounds_max[1] = z;
			}
		}
	}
	if (first)
		return false;

	*offest_x = bounds_min[0];
	*offest_z = bounds_min[1];
	*width = bounds_max[0] - bounds_min[0] + 1;
	*depth = bounds_max[1] - bounds_min[1] + 1;
	return true;
}

static void genesisInteriorFindHallwayPart(GenesisInteriorState *state, GenesisInteriorHallwayGridData *grid, 
										   int width, int depth, int start_x, int start_z, int *end_x, int *end_z, 
										   F32 *min_height, F32 *max_height,
										   bool vert, int partition_idx)
{
	GenesisInteriorHallwayGridData last_data;
	int x = start_x;
	int z = start_z;
	*end_x = x;
	*end_z = z;
	last_data = grid[x + z*width];
	*min_height = *max_height = last_data.height;
	while(x < width && z < depth)
	{
		GenesisInteriorHallwayGridData next_data;
		*(grid[x + z*width].point_idx) = partition_idx;
		grid[x + z*width].type = 0;
		if(vert)
			z++;
		else
			x++;

		next_data = grid[x + z*width];

		if(last_data.type != next_data.type)
			break;
		if(vert && !(last_data.dir_flags & genesisIntUp))
			break;
		if(!vert && !(last_data.dir_flags & genesisIntRight))
			break;

		last_data = grid[x + z*width];
		*min_height = MIN(*min_height, last_data.height);
		*max_height = MAX(*max_height, last_data.height);
		*end_x = x;
		*end_z = z;
	}
	*min_height *= state->floor_height;
	*max_height *= state->floor_height;
}

static void genesisInteriorCreateHallwayPartCommonItems(GenesisInteriorState *state, GenesisInteriorHallwayGroup *hallway_group,
														int start_x, int start_z, int end_x, int end_z, F32 hmin, F32 hmax, F32 floor_mid,
														GenesisInteriorHallwayGridType hall_type)
{
	int i, j;
	int pw, ph;
	GenesisToPlaceObject *object2, *infrastructure, *light;

	infrastructure = hallway_group->infrastructure;

	object2 = calloc(1, sizeof(GenesisToPlaceObject));
	object2->object_name = allocAddString("Infrastructure");
	pw = end_x+1 - start_x;
	ph = end_z+1 - start_z;
	identityMat3(object2->mat);
	setVec3(object2->mat[3], start_x*state->spacing, 0.f, start_z*state->spacing);
	object2->uid = 0;
	object2->parent = infrastructure;
	object2->params = StructCreate(parse_GenesisProceduralObjectParams);
	object2->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
	object2->params->volume_properties->eShape = GVS_Box;
	setVec3(object2->params->volume_properties->vBoxMin, -0.5f*state->spacing, hmin, -0.5f*state->spacing);
	setVec3(object2->params->volume_properties->vBoxMax, (pw-0.5f)*state->spacing, hmax, (ph-0.5f)*state->spacing);
	object2->params->room_properties = StructCreate(parse_WorldRoomProperties);
	object2->params->room_properties->eRoomType = WorldRoomType_Partition;
	eaPush(&state->to_place.objects, object2);
	eaPush(&hallway_group->partitions, object2);

	for ( i=0; i < eaSize(&hallway_group->rooms); i++ )
	{
		GenesisInteriorRoom *room = hallway_group->rooms[i];
		int x = room->x-state->bounds[0];
		int z = room->z-state->bounds[1];
		if(	x >= start_x && z >= start_z &&
			x <= end_x && z <= end_z )
		{
			room->parent_group_objects = object2;		
		}
	}

	//Lights
	if(state->backdrop && !state->backdrop->int_light.no_lights)
	{
		light = calloc(1, sizeof(GenesisToPlaceObject));
		light->object_name = allocAddString("Light");
		light->parent = hallway_group->lights_parent;
		identityMat3(light->mat);
		setVec3(light->mat[3], start_x*state->spacing + (pw/2.0f)*state->spacing - state->spacing/2.0f, hmin, start_z*state->spacing + (ph/2.0f)*state->spacing - state->spacing/2.0f);
		eaPush(&state->to_place.objects, light);
		genesisInteriorSetupProjLight(state, light, pw*state->spacing, ph*state->spacing, floor_mid, hall_type);
	}

	// Mission volumes
	for (j = 0; j < eaSize(&hallway_group->mission_volumes); j++)
	{
		GenesisInteriorMissionVolume *mission = hallway_group->mission_volumes[j];
		GenesisToPlaceObject *volume_obj;

		if (!mission->inited)
		{
			volume_obj = mission->mission_volume;
			mission->inited = true;
		}
		else
		{
			volume_obj = calloc(1, sizeof(GenesisToPlaceObject));
			volume_obj->object_name = "Subvolume";
			identityMat3(volume_obj->mat);
			volume_obj->parent = mission->mission_volume;
			volume_obj->params = StructCreate(parse_GenesisProceduralObjectParams);
			volume_obj->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
			volume_obj->params->volume_properties->bSubVolume = 1;
			eaPush(&state->to_place.objects, volume_obj);
		}

		setVec3(volume_obj->mat[3], start_x*state->spacing, 0.f, start_z*state->spacing);

		volume_obj->params = StructCreate(parse_GenesisProceduralObjectParams);
		volume_obj->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
		volume_obj->params->volume_properties->eShape = GVS_Box;
		setVec3(volume_obj->params->volume_properties->vBoxMin, -0.5f*state->spacing, hmin, -0.5f*state->spacing);
		setVec3(volume_obj->params->volume_properties->vBoxMax, (pw-0.5f)*state->spacing, hmax, (ph-0.5f)*state->spacing);
	}
}

static GenesisInteriorHallwayGridType genesisInteriorHallwayFindTiltDir(int x, int z, int nx, int nz)
{
	if(z < nz)
		return genesisIntRampTiltUpNorth;
	if(z > nz)
		return genesisIntRampTiltUpSouth;
	if(x < nx)
		return genesisIntRampTiltUpEast;
	if(x > nx)
		return genesisIntRampTiltUpWest;
	assert(false);
	return genesisIntHall;
}

static void genesisInteriorCreateHallwayCommonItems(GenesisInteriorState *state)
{
	int g, i, j, x, z;
	F32 hallway_min_height = 0.0f, hallway_max_height = 10.0f;
	GenesisInteriorTag **tags=NULL;

	// Calculate maximum hallway height
	genesisInteriorAddTag(state->kit, &tags, "Hallway");
	genesisInteriorAddTag(state->kit, &tags, "HallywayDoorSpace");
	genesisInteriorCalcMinMaxHeight(state->kit, tags, &hallway_min_height, &hallway_max_height);
	eaDestroy(&tags);

	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		int offset_x=0, offset_z=0;
		int width=1, depth=1;
		GenesisInteriorHallwayGridData *grid;

		if (!genesisInteriorGetHallwayBounds(state, hallway_group, &offset_x, &offset_z, &width, &depth))
			continue;

		grid = ScratchAlloc(sizeof(GenesisInteriorHallwayGridData) * width * depth);
		
		//Fill the grid with the hallways
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];
			for (j = 0; j < eaiSize(&hallway->points); j+=4)
			{
				x = hallway->points[j+0]-state->bounds[0] - offset_x;
				z = hallway->points[j+2]-state->bounds[1] - offset_z;
				assert(x >= 0 && z >= 0 && x < width && z < depth);

				grid[x + z*width].type = genesisIntHall;

				//Figure out what directions you can travel to from here
				if(j > 3)
				{
					// from last point
					if(hallway->points[j-4+0] < hallway->points[j+0])
						grid[x + z*width].dir_flags |= genesisIntLeft;
					else if(hallway->points[j-4+0] > hallway->points[j+0])
						grid[x + z*width].dir_flags |= genesisIntRight;
					
					if(hallway->points[j-4+2] < hallway->points[j+2])
						grid[x + z*width].dir_flags |= genesisIntDown;
					else if(hallway->points[j-4+2] > hallway->points[j+2])
						grid[x + z*width].dir_flags |= genesisIntUp;

					//If a ramp, find tilt direction
					if(hallway->points[j-4+1] > hallway->points[j+1])
						grid[x + z*width].type = genesisInteriorHallwayFindTiltDir(	hallway->points[j+0],
																					hallway->points[j+2],
																					hallway->points[j-4+0],
																					hallway->points[j-4+2]);
				}
				if(j < eaiSize(&hallway->points) - 4)
				{
					// to next point
					if(hallway->points[j+4+0] < hallway->points[j+0])
						grid[x + z*width].dir_flags |= genesisIntLeft;
					else if(hallway->points[j+4+0] > hallway->points[j+0])
						grid[x + z*width].dir_flags |= genesisIntRight;

					if(hallway->points[j+4+2] < hallway->points[j+2])
						grid[x + z*width].dir_flags |= genesisIntDown;
					else if(hallway->points[j+4+2] > hallway->points[j+2])
						grid[x + z*width].dir_flags |= genesisIntUp;

					//If a ramp, find tilt direction
					if(hallway->points[j+4+1] > hallway->points[j+1])
						grid[x + z*width].type = genesisInteriorHallwayFindTiltDir(	hallway->points[j+0],
																					hallway->points[j+2],
																					hallway->points[j+4+0],
																					hallway->points[j+4+2]);
				}
				hallway->points[j+3] = -1;
				grid[x + z*width].point_idx = (hallway->points + j+3);
				grid[x + z*width].height = hallway->points[j+1];
			}
		}

		for ( z=0; z < depth; z++ )
		{
			for ( x=0; x < width; x++ )
			{
				GenesisInteriorHallwayGridData data = grid[x + z*width];
				if(data.type > 0)
				{
					int end_x=x, end_z=z;
					bool vert = true;
					F32 hallway_min, hallway_max;

					if(	x < width-1 && 
						grid[(x+1) + z*width].type == data.type &&
						(grid[(x+1) + z*width].dir_flags & genesisIntLeft))
						vert = false;
					genesisInteriorFindHallwayPart(state, grid, width, depth, x, z, &end_x, &end_z, &hallway_min, &hallway_max, vert, eaSize(&hallway_group->partitions));
					genesisInteriorCreateHallwayPartCommonItems(state, hallway_group, x+offset_x, z+offset_z, end_x+offset_x, end_z+offset_z, hallway_min_height+hallway_min, hallway_max_height+hallway_max, (hallway_max+hallway_min+state->floor_height)/2.0f - hallway_min, data.type);
				}
			}
		}
		ScratchFree(grid);
	}
}

static void genesisInteriorFillRoomGridForLights(GenesisInteriorRoom *room, U8 *grid, int grid_width, int grid_height)
{
	int i, j;
	for ( i=0; i < room->width; i++ )
	{
		for ( j=0; j < room->depth; j++ )
		{
			if(room->grid[i + j*room->width] != 2)
			{
				int gx, gz;
				genesisInteriorRotatePoint(room, i, j, &gx, &gz);
				gx -= room->x;
				gz -= room->z;
				assert(gx >= 0 && gz >= 0 && gx < grid_width && gz < grid_height);
				grid[gx + gz*grid_width] = 1;
			}
		}
	}
}

static void genesisInteriorFindRoomPart(U8 *grid, int grid_width, int grid_height, int start_x, int start_z, int *end_x, int *end_z)
{
	int i, j;
	(*end_x) = start_x;
	(*end_z) = start_z;
	assert(grid[start_x + start_z*grid_width] != 0);
	grid[start_x + start_z*grid_width] = 0;
	for ( i=start_x+1; i < grid_width; i++ )
	{
		if(grid[i + start_z*grid_width] == 0)
			break;
		grid[i + start_z*grid_width] = 0;
		(*end_x) = i;
	}
	for ( j=start_z+1; j < grid_height; j++ )
	{
		bool valid_row = true;
		for ( i=start_x; i <= (*end_x); i++ )
		{
			if(grid[i + j*grid_width] == 0)
			{
				valid_row = false;
				break;
			}
		}
		if(!valid_row)
			break;

		for ( i=start_x; i <= (*end_x); i++ )
			grid[i + j*grid_width] = 0;
		(*end_z) = j;
	}
}

static void genesisInteriorCreateRoomLight(GenesisInteriorState *state, GenesisInteriorRoom *room, int x, int z, int w, int h)
{
	GenesisToPlaceObject *light;
	light = calloc(1, sizeof(GenesisToPlaceObject));
	light->object_name = allocAddString("Light");
	light->parent = room->parent_group_objects;
	identityMat3(light->mat);
	setVec3(light->mat[3], x*state->spacing + (w/2.0f)*state->spacing - state->spacing/2.0f, room->height*state->floor_height, z*state->spacing + (h/2.0f)*state->spacing - state->spacing/2.0f);
	eaPush(&state->to_place.objects, light);
	genesisInteriorSetupProjLight(state, light, w*state->spacing, h*state->spacing, 0, genesisIntNotHall);
}

static void genesisInteriorCreateRoomLights(GenesisInteriorState *state, GenesisInteriorRoom *room, int room_x, int room_z, int room_w, int room_h)
{
	int x, z;
	U8 *grid;

	grid = ScratchAlloc(sizeof(U8) * room_w * room_h);
	genesisInteriorFillRoomGridForLights(room, grid, room_w, room_h);

	for ( z=0; z < room_h; z++ )
	{
		for ( x=0; x < room_w; x++ )
		{
			if(grid[x + z*room_w])
			{
				int end_x=x, end_z=z;
				genesisInteriorFindRoomPart(grid, room_w, room_h, x, z, &end_x, &end_z);
				genesisInteriorCreateRoomLight(state, room, room_x+x, room_z+z, end_x-x+1, end_z-z+1);
			}
		}
	}

	ScratchFree(grid);	
}

static GenesisToPlaceObject* genesisMakeInteriorGlobalVolume(GenesisToPlaceObject *parent_object, const char *name, Vec3 vMin, Vec3 vMax)
{
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	to_place_object->object_name = allocAddString(name);
	copyMat4(unitmat, to_place_object->mat);
	to_place_object->params = StructCreate(parse_GenesisProceduralObjectParams);
	to_place_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
	to_place_object->params->volume_properties->eShape = GVS_Box;
	copyVec3(vMin, to_place_object->params->volume_properties->vBoxMin);
	copyVec3(vMax, to_place_object->params->volume_properties->vBoxMax);
	to_place_object->uid = 0;
	to_place_object->parent = parent_object;
	return to_place_object;
}

static void genesisInteriorAddSoundProperties(GenesisInteriorState *state, GenesisToPlaceObject *object, char **amb_sounds, F32 area)
{
	int rand_sound_idx;
	if(!amb_sounds || eaSize(&amb_sounds) <= 0)
		return;
	if (!object->params)
		object->params = StructCreate(parse_GenesisProceduralObjectParams);
	object->params->sound_volume_properties = StructCreate(parse_WorldSoundVolumeProperties);
	object->params->sound_volume_properties->multiplier = 1.0f;
	if (area < 0)
		object->params->sound_volume_properties->dsp_name = allocAddString("GenesisInteriorDSP_H");
	else if (area < 15 * SQR(40.f))
		object->params->sound_volume_properties->dsp_name = allocAddString("GenesisInteriorDSP_S");
	else if (area < 22 * SQR(40.f))
		object->params->sound_volume_properties->dsp_name = allocAddString("GenesisInteriorDSP_M");
	else
		object->params->sound_volume_properties->dsp_name = allocAddString("GenesisInteriorDSP_L");
	rand_sound_idx = randomMersenneU32(state->random_table)%eaSize(&amb_sounds);
	object->params->sound_volume_properties->event_name = allocAddString(amb_sounds[rand_sound_idx]);
}

static void genesisInteriorCreateCommonItems(GenesisInteriorState *state, GenesisMissionRequirements **mission_reqs, GenesisZoneInterior *interior, Vec3 layer_min, Vec3 layer_max, Vec3 layer_center)
{
	GenesisBackdrop* backdrop = interior->backdrop;
	int i, j;
	F32 room_min_height =  0.0f;
	F32 room_max_height = 10.0f;
	GenesisToPlaceObject *object, *infrastructure, *mission_parent, *mission_object;
	GenesisInteriorTag **tags=NULL;
	char volume_size_str[256];
	Vec3 vol_min, vol_max;

	subVec3(layer_min, layer_center, vol_min);
	subVec3(layer_max, layer_center, vol_max);
	sprintf(volume_size_str, "%f %f %f %f %f %f", vol_min[0], vol_min[1], vol_min[2], vol_max[0], vol_max[1], vol_max[2]); 

	// Calculate maximum ceiling height
	genesisInteriorAddTag(state->kit, &tags, "Ceiling");
	genesisInteriorAddTag(state->kit, &tags, "FloorNormal");
	genesisInteriorAddTag(state->kit, &tags, "Wall");
	genesisInteriorAddTag(state->kit, &tags, "WallInsideSpace");
	genesisInteriorAddTag(state->kit, &tags, "WallDoor");
	genesisInteriorAddTag(state->kit, &tags, "WallEntrance");
	genesisInteriorAddTag(state->kit, &tags, "WallExit");
	genesisInteriorCalcMinMaxHeight(state->kit, tags, &room_min_height, &room_max_height);
	eaDestroy(&tags);

	// Global mission volumes
	{
		mission_parent = calloc(1, sizeof(GenesisToPlaceObject));
		mission_parent->object_name = allocAddString("Missions");
		identityMat4(mission_parent->mat);
		mission_parent->parent = NULL;
		if (eaSize(&mission_reqs) > 1)
		{
			mission_parent->params = genesisCreateMultiMissionWrapperParams();
		}
		eaPush(&state->to_place.objects, mission_parent);
		
		for (j = 0; j < eaSize(&mission_reqs); j++)
		{
			mission_object = calloc(1, sizeof(GenesisToPlaceObject));
			mission_object->object_name = genesisMissionVolumeName(interior->layout_name, mission_reqs[j]->missionName);
			mission_object->challenge_name = strdup(mission_object->object_name);
			mission_object->challenge_is_unique = true;
			identityMat3(mission_object->mat);
			mission_object->uid = 0;
			mission_object->parent = mission_parent;
			mission_object->params = StructClone(parse_GenesisProceduralObjectParams, mission_reqs[j]->params);
			mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
			copyVec3(vol_min, mission_object->params->volume_properties->vBoxMin);
			copyVec3(vol_max, mission_object->params->volume_properties->vBoxMax);
			eaPush(&state->to_place.objects, mission_object);
		}
	}

	// FX Volume
	if(backdrop && backdrop->fx_volume)
	{
		GenesisToPlaceObject *to_place_object = genesisMakeInteriorGlobalVolume(NULL, "Interior_Autogen_FX_Volume", vol_min, vol_max);
		to_place_object->params->fx_volume = StructClone(parse_WorldFXVolumeProperties, backdrop->fx_volume);
		genesisProceduralObjectEnsureType(to_place_object->params);
		eaPush(&state->to_place.objects, to_place_object);	
	}
	// Power Volume
	if(backdrop && backdrop->power_volume)
	{
		GenesisToPlaceObject *to_place_object = genesisMakeInteriorGlobalVolume(NULL, "Interior_Autogen_Power_Volume", vol_min, vol_max);
		to_place_object->params->power_volume = StructClone(parse_WorldPowerVolumeProperties, backdrop->power_volume);
		genesisProceduralObjectEnsureType(to_place_object->params);
		eaPush(&state->to_place.objects, to_place_object);	
	}

	// Make Hallways and Junction parent groups
	for ( i=0; i < eaSize(&state->hallway_groups); i++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[i];

		//Make Parent Object
		object = calloc(1, sizeof(GenesisToPlaceObject));
		object->object_name = allocAddString(hallway_group->src_name);
		object->uid = 0;
		identityMat3(object->mat);
		eaPush(&state->to_place.objects, object);
		hallway_group->parent_object = object;

		//Make Parent Light Object
		object = calloc(1, sizeof(GenesisToPlaceObject));
		object->object_name = allocAddString("Lights");
		object->parent = hallway_group->parent_object;
		object->uid = 0;
		identityMat3(object->mat);
		eaPush(&state->to_place.objects, object);
		hallway_group->lights_parent = object;

		//Make Infrastructure Object
		infrastructure = calloc(1, sizeof(GenesisToPlaceObject));
		infrastructure->object_name = allocAddString("InfrastructureParent");
		infrastructure->parent = hallway_group->parent_object;
		identityMat3(infrastructure->mat);
		infrastructure->params = StructCreate(parse_GenesisProceduralObjectParams);
		infrastructure->params->room_properties = StructCreate(parse_WorldRoomProperties);
		infrastructure->params->room_properties->eRoomType = WorldRoomType_Room;
		if(state->backdrop && !state->backdrop->int_light.no_lights)
			infrastructure->params->room_properties->bLimitLights = 1;
		if (!state->kit->no_occlusion)
			infrastructure->params->room_properties->bOccluder = 1;
		genesisInteriorAddSoundProperties(state, infrastructure, SAFE_MEMBER(backdrop, amb_hallway_sounds), -1);
		eaPush(&state->to_place.objects, infrastructure);
		hallway_group->infrastructure = infrastructure;

		// Missions all go under here
		mission_parent = calloc(1, sizeof(GenesisToPlaceObject));
		mission_parent->object_name = allocAddString("Missions");
		identityMat4(mission_parent->mat);
		mission_parent->parent = hallway_group->parent_object;
		if (eaSize(&mission_reqs) > 1)
		{
			mission_parent->params = genesisCreateMultiMissionWrapperParams();
		}
		eaPush(&state->to_place.objects, mission_parent);

		// Mission volumes
		for (j = 0; j < eaSize(&mission_reqs); j++)
		{
			GenesisInteriorMissionVolume *mission = calloc(1, sizeof(GenesisInteriorMissionVolume));
			mission_object = calloc(1, sizeof(GenesisToPlaceObject));
			mission_object->object_name = genesisMissionRoomVolumeName(interior->layout_name, hallway_group->src_name, mission_reqs[j]->missionName);
			mission_object->force_named_object = true;
			mission_object->challenge_name = strdup(mission_object->object_name);
			mission_object->challenge_is_unique = true;
			identityMat3(mission_object->mat);
			setVec3(mission_object->mat[3], 0, 0, 0);
			mission_object->uid = 0;
			mission_object->parent = mission_parent;
			mission_object->params = StructClone(parse_GenesisProceduralObjectParams, genesisFindMissionRoomProceduralParams(mission_reqs[j], interior->layout_name, hallway_group->src_name));
			eaPush(&state->to_place.objects, mission_object);
			mission->mission_volume = mission_object;
			eaPush(&hallway_group->mission_volumes, mission);
		}

		if (state->no_sharing)
		{
			hallway_group->parent_group_shared = NULL;
		}
		else
		{
			hallway_group->parent_group_shared = calloc(1, sizeof(GenesisToPlaceObject));
			hallway_group->parent_group_shared->object_name = allocAddString("Shared");
			identityMat3(hallway_group->parent_group_shared->mat);
			hallway_group->parent_group_shared->parent = hallway_group->parent_object;
			eaPush(&state->to_place.objects, hallway_group->parent_group_shared);
		}

		// Shared objects
		for ( j=0; j < eaSize(&hallway_group->hallways); j++ )
			hallway_group->hallways[j]->parent_group_shared = hallway_group->parent_group_shared;
		for ( j=0; j < eaSize(&hallway_group->rooms); j++ )
			hallway_group->rooms[j]->parent_group_shared = hallway_group->parent_group_shared;
	}

	// Hallways 
	genesisInteriorCreateHallwayCommonItems(state);

	// Rooms
	for (i = 0; i < eaSize(&state->room_list); i++)
	{
		GenesisInteriorRoom *room = state->room_list[i];
		int px = room->x-state->bounds[0];
		int pz = room->z-state->bounds[1];
		int pw = (room->rot%2) == 0 ? room->width : room->depth;
		int ph = (room->rot%2) == 0 ? room->depth : room->width;
		Vec3 vExtents[2];


		//Skip rooms that are part of hallways as they are already handled
		if(room->parent_hallway)
			continue;

		setVec3(vExtents[0], -0.5f*state->spacing, room_min_height, -0.5f*state->spacing);
		setVec3(vExtents[1], (pw-0.5f)*state->spacing, room_max_height, (ph-0.5f)*state->spacing);
		if (room->geo)
		{
			GroupDef *def = objectLibraryGetGroupDefByName(room->geo, true);
			if (def) {
				setVec3(vExtents[0], -0.5f*state->spacing, def->bounds.min[1], -0.5f*state->spacing);
				setVec3(vExtents[1], (pw-0.5f)*state->spacing, def->bounds.max[1], (ph-0.5f)*state->spacing);
			}
		}

		// Room Volume
		object = calloc(1, sizeof(GenesisToPlaceObject));
		object->object_name = allocAddString(room->src_name ? room->src_name : "ROOM");
		room->parent_group_room = object;
		identityMat3(object->mat);
		setVec3(object->mat[3], px*state->spacing, room->height*state->floor_height, pz*state->spacing);
		object->uid = 0;
		eaPush(&state->to_place.objects, object);

		// Missions all go under here
		mission_parent = calloc(1, sizeof(GenesisToPlaceObject));
		mission_parent->object_name = allocAddString("Missions");
		identityMat4(mission_parent->mat);
		mission_parent->parent = object;
		if (eaSize(&mission_reqs) > 1)
		{
			mission_parent->params = genesisCreateMultiMissionWrapperParams();
		}
		eaPush(&state->to_place.objects, mission_parent);

		// Mission Volume
		for (j = 0; j < eaSize(&mission_reqs); j++)
		{
			GenesisInteriorMissionVolume *mission = calloc(1, sizeof(GenesisInteriorMissionVolume));

			mission_object = calloc(1, sizeof(GenesisToPlaceObject));
			mission_object->object_name = genesisMissionRoomVolumeName(interior->layout_name, room->src_name, mission_reqs[j]->missionName);
			mission_object->force_named_object = true;
			mission_object->challenge_name = strdup(mission_object->object_name);
			mission_object->challenge_is_unique = true;
			identityMat3(mission_object->mat);
			setVec3(mission_object->mat[3], px*state->spacing, room->height*state->floor_height, pz*state->spacing);
			mission_object->uid = 0;
			mission_object->parent = mission_parent;
			mission_object->params = StructClone(parse_GenesisProceduralObjectParams, genesisFindMissionRoomProceduralParams(mission_reqs[j], interior->layout_name, room->src_name));
			if( !mission_object->params ) {
				mission_object->params = StructCreate(parse_GenesisProceduralObjectParams);
			}
			mission_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
			copyVec3(vExtents[0], mission_object->params->volume_properties->vBoxMin);
			copyVec3(vExtents[1], mission_object->params->volume_properties->vBoxMax);
			eaPush(&state->to_place.objects, mission_object);
			mission->mission_volume = mission_object;
			eaPush(&room->mission_volumes, mission);
		}

		// Shared objects
		if (state->no_sharing)
		{
			room->parent_group_shared = NULL;
		}
		else
		{
			room->parent_group_shared = calloc(1, sizeof(GenesisToPlaceObject));
			room->parent_group_shared->object_name = allocAddString("Shared");
			identityMat3(room->parent_group_shared->mat);
			room->parent_group_shared->parent = object;
			setVec3(room->parent_group_shared->mat[3], px*state->spacing, room->height*state->floor_height, pz*state->spacing);
			eaPush(&state->to_place.objects, room->parent_group_shared);
		}

		// Objects
		room->parent_group_objects = calloc(1, sizeof(GenesisToPlaceObject));
		room->parent_group_objects->object_name = allocAddString("Infrastructure");
		identityMat3(room->parent_group_objects->mat);
		room->parent_group_objects->params = StructCreate(parse_GenesisProceduralObjectParams);
		room->parent_group_objects->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
		copyVec3(vExtents[0], room->parent_group_objects->params->volume_properties->vBoxMin);
		copyVec3(vExtents[1], room->parent_group_objects->params->volume_properties->vBoxMax);
		room->parent_group_objects->params->room_properties = StructCreate(parse_WorldRoomProperties);
		room->parent_group_objects->params->room_properties->eRoomType = WorldRoomType_Room;
		if((!room->geo || room->use_proj_lights) && state->backdrop && !state->backdrop->int_light.no_lights)
			room->parent_group_objects->params->room_properties->bLimitLights = 1;
		if (!state->kit->no_occlusion)
			room->parent_group_objects->params->room_properties->bOccluder = 1;
		genesisInteriorAddSoundProperties(state, room->parent_group_objects, SAFE_MEMBER(backdrop, amb_sounds), pw * ph * SQR(state->kit->spacing ? state->kit->spacing : 40.0f));
		room->parent_group_objects->parent = object;
		setVec3(room->parent_group_objects->mat[3], px*state->spacing, room->height*state->floor_height, pz*state->spacing);
		eaPush(&state->to_place.objects, room->parent_group_objects);

		//Lights
		if((!room->geo || room->use_proj_lights) && state->backdrop && !state->backdrop->int_light.no_lights)
		{
			genesisInteriorCreateRoomLights(state, room, px, pz, pw, ph);
		}
	}
}

////////////////////////////////////////////////////////////////////////
// STEP 9: Create GroupDefs for all the room & hallway infrastructure
////////////////////////////////////////////////////////////////////////

static F32 genesisGetDefColRad(GroupDef *group_def)
{
	Vec3 abs_min, abs_max, true_max;
	if(!group_def)
		return 0;
	ABSVEC3(group_def->bounds.min, abs_min); 
	ABSVEC3(group_def->bounds.max, abs_max); 
	MAXVEC3(abs_min, abs_max, true_max);
	return lengthVec3(true_max);
}

static void genesisInteriorPlaceElementObject(GenesisInteriorState *state, GenesisInteriorElementObject *object,
											  int x, F32 y, int z, F32 angle, GenesisToPlaceObject *parent_room,
											  ExclusionObject ***parent_volumes, U8 flags, const char *spawn_name, const char *challenge_name)
{
	Vec3 new_offset;
	Mat3 rotation;
	GroupDef *group_def;

	// Place kit piece geometry
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	identityMat3(to_place_object->mat);
	yawMat3(angle+RAD(object->rotation), to_place_object->mat);
	identityMat3(rotation);
	yawMat3(angle, rotation);
	mulVecMat3(object->offset, rotation, new_offset);
	setVec3(to_place_object->mat[3], x*state->spacing + new_offset[0], new_offset[1] + y, z*state->spacing + new_offset[2]);
	to_place_object->uid = object->geometry_uid;
	to_place_object->parent = parent_room;
	to_place_object->spawn_name = spawn_name;
	to_place_object->challenge_name = strdup(challenge_name);
	eaPush(&state->to_place.objects, to_place_object);

	// Place kit piece volumes
	if (parent_volumes &&
		(group_def = objectLibraryGetGroupDef(object->geometry_uid, true)))
	{
		ExclusionObject *exclude_object = calloc(1, sizeof(ExclusionObject));
		ExclusionVolumeGroup *group = calloc(1, sizeof(ExclusionVolumeGroup));
		group->idx_in_parent = 0;
		group->optional = false;
		exclusionGetDefVolumes(group_def, &group->volumes, unitmat, false, -1, NULL);
		exclude_object->volume_group = group;
		copyMat4(to_place_object->mat, exclude_object->mat);
		exclude_object->max_radius = genesisGetDefColRad(group_def);
		exclude_object->volume_group_owned = true;
		exclude_object->flags = flags;

		eaPush(parent_volumes, exclude_object);
	}
}

static void genesisDoReplacements(GenesisInteriorState *state, GenesisInteriorGrid *grid, GenesisInteriorRoom *room, GenesisInteriorLayer layer)
{
	int j, k, x, z;
	if (!room || !room->src)
		return;
	for (j = 0; j < eaSize(&room->src->missions); j++)
	{
		GenesisRoomMission *mission = room->src->missions[j];
		for (k = 0; k < eaSize(&mission->replaces); k++)
		{
			bool found = false;
			GenesisInteriorReplace *replace = mission->replaces[k];
			GenesisInteriorElement *new_element[] = { NULL, NULL, NULL };
			GenesisInteriorTag *tag = genesisInteriorFindTag(state->kit, replace->old_tag);
			int *pos_array = NULL;
			if (replace->replace_layer != GENESIS_INTERIOR_ANY &&
				replace->replace_layer != GENESIS_INTERIOR_ANY_NO_ERROR &&
				replace->replace_layer != layer)
				continue;
			if (replace->final_x != -1)
				continue;
			if (!tag)
			{
				genesisRaiseErrorInternal(GENESIS_ERROR, "GenesisInteriorKit", state->kit->name, "Missing kit object with tag %s", replace->old_tag);
				continue;
			}
			// Find an acceptable element
			for (z = 0; z < eaSize(&state->kit->elements); z++)
			{
				GenesisInteriorElement *element = state->kit->elements[z];
				if (!stricmp(replace->new_tag, element->tag->name))
				{
					new_element[0] = element;
					break;
				}
			}
			for (z = 0; z < eaSize(&state->kit->elements); z++)
			{
				GenesisInteriorElement *element = state->kit->elements[z];
				if (!stricmp(replace->new_detail_tag, element->tag->name))
				{
					new_element[1] = element;
					break;
				}
			}
			for (z = 0; z < eaSize(&state->kit->elements); z++)
			{
				GenesisInteriorElement *element = state->kit->elements[z];
				if (!stricmp(replace->new_light_tag, element->tag->name))
				{
					new_element[2] = element;
					break;
				}
			}
			if (!new_element[0])
			{
				genesisRaiseErrorInternal(GENESIS_ERROR, "GenesisInteriorKit", state->kit->name, "Missing kit object with tag %s", replace->new_tag);
				continue;
			}
			// Find the candidates 
			for (x = 0; x < grid->width; x++)
			{
				for (z = 0; z < grid->height; z++)
				{
					if (grid->data[x+z*grid->width].element[0] &&
						genesisDerivesFromTag(grid->data[x+z*grid->width].element[0]->tag, tag))
					{
						eaiPush(&pos_array, x);
						eaiPush(&pos_array, z);
					}
				}
			}
			if (eaiSize(&pos_array) > 0)
			{
				if (state->override_positions)
				{
					// Choose closest candidate to target position
					S32 rep_pos[] = { replace->override_x + room->x - state->bounds[0], (room->depth-1-replace->override_z) + room->z - state->bounds[1] };
					F32 min_dist = SQR(pos_array[0]-rep_pos[0])+SQR(pos_array[1]-rep_pos[1]);
					int i, best = 0;
					for (i = 2; i < eaiSize(&pos_array); i += 2)
					{
						F32 dist = SQR(pos_array[i+0]-rep_pos[0])+SQR(pos_array[i+1]-rep_pos[1]);
						if (dist < min_dist)
						{
							best = i;
							min_dist = dist;
						}
					}
					x = pos_array[best];
					z = pos_array[best+1];
				}
				else
				{
					// Choose a random candidate
					int idx = randomMersenneIntRange(state->random_table, 0, eaiSize(&pos_array)/2-1);
					x = pos_array[idx*2];
					z = pos_array[idx*2+1];
				}
				replace->final_x = x;
				replace->final_z = z;
				grid->data[x+z*grid->width].element[0] = new_element[0];
				grid->data[x+z*grid->width].element[1] = new_element[1];
				grid->data[x+z*grid->width].element[2] = new_element[2];
				eaiDestroy(&pos_array);
			}
			else if (replace->replace_layer != GENESIS_INTERIOR_ANY_NO_ERROR)
			{
				genesisRaiseErrorInternal(GENESIS_ERROR, "GenesisInteriorKit", state->kit->name, "Cannot find acceptable position of type %s for tag %s", replace->old_tag, replace->new_tag);
			}
		}
	}
}

static void genesisBuildGrid(GenesisInteriorState *state, GenesisInteriorGrid *grid, GenesisInteriorRoom *room,
							 MersenneTable *room_table, U32 flags, GenesisInteriorLayer layer)
{
	int x, z, i, j;
	GenesisInteriorTag *none_tag = genesisInteriorFindTag(state->kit, "None");

	for (x = 0; x < grid->width; x++)
		for (z = 0; z < grid->height; z++)
		{
			GenesisInteriorPatternInternal *pattern = NULL;
			int idx = x+z*grid->width;
			// Pick a pattern if necessary
			if (grid->data[idx].room && grid->tag_field[idx])
			{
				if (!stashFindPointer(grid->data[idx].room->picked_patterns, grid->tag_field[idx], &pattern))
				{
					GenesisInteriorPatternList *pattern_list = NULL;
					if (stashFindPointer(state->pattern_table, grid->tag_field[idx], &pattern_list) &&
						eaSize(&pattern_list->list))
					{
						// Pick a pattern from the available patterns
						int pos = randomMersenneIntRange(room_table, 0, eaSize(&pattern_list->list)-1);
						pattern = pattern_list->list[pos];
					}
					if (pattern)
					{
						stashAddPointer(grid->data[idx].room->picked_patterns, grid->tag_field[idx], pattern, true);
						//printf("Set room %s pattern %s to %s.\n", grid->data[idx].room->src_name, grid->tag_field[idx]->name, pattern->pattern_name);
					}
				}
			}
			if (!grid->data[idx].dummy)
			{
				grid->data[idx].element[0] = genesisInteriorFindSuitableElement(state, pattern, room_table, grid, x, z, &grid->data[idx].rotation);
				if (!grid->data[idx].element[0] && pattern)
					grid->data[idx].element[0] = genesisInteriorFindSuitableElement(state, NULL, room_table, grid, x, z, &grid->data[idx].rotation);
				if (grid->data[idx].element[0])
				{
					grid->data[idx].element[1] = genesisInteriorFindSuitableDetailElement(state, room_table, grid->data[idx].element[0]);
					grid->data[idx].element[2] = genesisInteriorFindSuitableLight(state, room_table, grid->data[idx].element[0]);
				}
				else if (grid->tag_field[idx] && 
						grid->tag_field[idx] != none_tag)
				{
					if (room) {
						genesisRaiseError(GENESIS_ERROR, 
							genesisMakeTempErrorContextRoom(room->src_name, state->layout->layout_name), 
							"KIT ERROR: Failed to place element %s AT (%d,%d)\n", grid->tag_field[idx]->name, x, z);
					}
					genesisInteriorFindSuitableElement(state, pattern, room_table, grid, x, z, &grid->data[idx].rotation); // TomY for DEBUGGING
				}
			}
		}

	genesisDoReplacements(state, grid, room, layer);

	for (x = 0; x < grid->width; x++)
		for (z = 0; z < grid->height; z++)
		{
			int idx = x+z*grid->width;
			if (!grid->data[idx].dummy && !grid->data[idx].dont_place)
			{
				GenesisToPlaceObject *parent_room = NULL;
				ExclusionObject ***parent_volumes = NULL;
				
				if (grid->data[idx].room)
				{
					parent_room = grid->data[idx].room->parent_group_objects;
					parent_volumes = &grid->data[idx].room->parent_volumes;
				}
				else if (grid->data[x+z*grid->width].hallway)
				{
					GenesisInteriorHallway *hallway = grid->data[x+z*grid->width].hallway;
					assert(hallway->parent_hallway && eaSize(&hallway->parent_hallway->partitions) > 0);
					assert(grid->data[idx].hallway_segment >= 0 && grid->data[idx].hallway_segment < eaSize(&hallway->parent_hallway->partitions));
					parent_room = grid->data[idx].hallway->parent_hallway->partitions[grid->data[idx].hallway_segment];
					parent_volumes = &grid->data[idx].hallway->parent_hallway->parent_volumes;
				}
				
				for (i = 0; i < 3; i++)
				{
					GenesisInteriorElement *element = grid->data[idx].element[i];
					if (element)
					{
						F32 angle = 0.f;
						F32 y = grid->data[idx].height*state->floor_height;
						switch (grid->data[idx].rotation)
						{
							xcase CONNECTION_ROTATION_NOROTATE:
							angle = 0.f;
							xcase CONNECTION_ROTATION_ROTATE90:
							angle = PI/2;
							xcase CONNECTION_ROTATION_ROTATE180:
							angle = PI;
							xcase CONNECTION_ROTATION_ROTATE270:
							angle = 3*PI/2;
						}
						genesisInteriorPlaceElementObject(state, &element->primary_object, x, y, z, angle, parent_room, parent_volumes, flags, grid->data[idx].spawn_point_name, NULL);
						for (j = 0; j < eaSize(&element->additional_objects); j++)
							genesisInteriorPlaceElementObject(state, element->additional_objects[j], x, y, z, angle, parent_room, parent_volumes, flags, NULL, NULL);
						if (i == 0 && grid->data[idx].place_cap)
						{
							if(eaSize(&state->cap_elements) > 0) 
							{
								GenesisInteriorElement *cap_element = state->cap_elements[0];
								GenesisObject *cap_object = StructCreate(parse_GenesisObject);
								cap_object->obj.name_str = StructAllocString(cap_element->primary_object.geometry);
								cap_object->obj.name_uid = cap_element->primary_object.geometry_uid;
								cap_object->challenge_name = StructAllocString(grid->data[idx].cap_name);
								cap_object->challenge_count = 1;
								cap_object->force_named_object = true;
								cap_object->params.override_position[0] = ((F32)(x-(room->x-state->bounds[0])) + 0.5f)*state->spacing - state->kit->room_padding;
								cap_object->params.override_position[2] = ((F32)(room->depth-1-(z-(room->z-state->bounds[1]))) + 0.5f)*state->spacing - state->kit->room_padding;
								cap_object->params.override_rot = DEG(angle);
								cap_object->source_context = genesisMakeErrorContextRoomDoor(cap_object->challenge_name);
								eaPush(&room->door_caps, cap_object);
								//genesisInteriorPlaceElementObject(state, &cap_element->primary_object, x, y, z, angle, parent_room, parent_volumes, flags, NULL, grid->data[idx].cap_name);
							} 
							else if (!state->override_positions)
							{
								genesisRaiseErrorInternal(GENESIS_ERROR, GENESIS_INTERIORS_DICTIONARY, state->kit->name, 
									"This interior kit is missing door caps");
							}
						}
						else if (i == 0 && grid->data[idx].place_door && eaSize(&state->door_elements) > 0)
						{
							GenesisInteriorElement *door_element = state->door_elements[0];
							GenesisObject *cap_object = StructCreate(parse_GenesisObject);
							cap_object->obj.name_str = StructAllocString(door_element->primary_object.geometry);
							cap_object->obj.name_uid = door_element->primary_object.geometry_uid;
							cap_object->challenge_name = StructAllocString(grid->data[idx].cap_name);
							cap_object->challenge_count = 1;
							cap_object->force_named_object = true;
							cap_object->params.override_position[0] = ((F32)(x-(room->x-state->bounds[0])) + 0.5f)*state->spacing - state->kit->room_padding;
							cap_object->params.override_position[2] = ((F32)(room->depth-1-(z-(room->z-state->bounds[1]))) + 0.5f)*state->spacing - state->kit->room_padding;
							cap_object->params.override_rot = DEG(angle);
							cap_object->source_context = genesisMakeErrorContextRoomDoor(cap_object->challenge_name);
							eaPush(&room->door_caps, cap_object);
							//genesisInteriorPlaceElementObject(state, &door_element->primary_object, x, y, z, angle, parent_room, parent_volumes, flags, NULL, NULL);
						}
					}
				}
			}
		}
}

static void genesisInteriorDrawRooms(GenesisInteriorState *state)
{
	int room_num, i, j, k, g, x, z;
	GenesisInteriorTag *floor_tag, *floor_wall_tag, *wall_tag, *wall_inside_tag, *wall_door_tag, *ceiling_tag, *hallway_tag, *wall_enter_tag, *wall_exit_tag;

	// Find appropriate tags in kit
	floor_tag = genesisInteriorFindTag(state->kit, "FloorNormal");
	floor_wall_tag = genesisInteriorFindTag(state->kit, "FloorWallSpace");
	wall_tag = genesisInteriorFindTag(state->kit, "Wall");
	wall_door_tag = genesisInteriorFindTag(state->kit, "WallDoor");
	wall_enter_tag = genesisInteriorFindTag(state->kit, "WallEntrance");
	wall_exit_tag = genesisInteriorFindTag(state->kit, "WallExit");
	wall_inside_tag = genesisInteriorFindTag(state->kit, "WallInsideSpace");
	ceiling_tag = genesisInteriorFindTag(state->kit, "Ceiling");
	hallway_tag = genesisInteriorFindTag(state->kit, "Hallway");

	for (room_num = 0; room_num < eaSize(&state->room_list); room_num++)
	{
		GenesisInteriorRoom *room = state->room_list[room_num];
		MersenneTable *room_table = mersenneTableCreate(room->detail_seed);

		room->picked_patterns = stashTableCreateAddress(10);

		if (!room->geo && !room->cap)
		{
			int px = room->x-state->bounds[0];
			int pz = room->z-state->bounds[1];
			int pw = (room->rot%2) == 0 ? room->width : room->depth;
			int ph = (room->rot%2) == 0 ? room->depth : room->width;

			for (i = 0; i < 3; i++)
			{
				memset(state->grid[i].tag_field, 0, sizeof(GenesisInteriorTag*)*state->grid[i].width*state->grid[i].height);
				memset(state->grid[i].data, 0, sizeof(GenesisInteriorGridElement)*state->grid[i].width*state->grid[i].height);
			}

			for (z = pz; z < pz+ph; z++)
			{
				for (x = px; x < px+pw; x++)
				{
					int invx, invz;
					U8 grid_value;
					assert(x >= 0 && x < state->grid[0].width && z >= 0 && z < state->grid[0].height);
					state->grid[0].data[x+z*state->grid[0].width].room = room;
					state->grid[1].data[x+z*state->grid[1].width].room = room;
					state->grid[2].data[x+z*state->grid[2].width].room = room;
					if (room->geo)
						state->grid[2].data[x+z*state->grid[2].width].dummy = true;

					genesisInteriorInvRotatePoint(room, x+state->bounds[0], z+state->bounds[1], &invx, &invz);
					grid_value = room->grid[invx+invz*room->width];

					assert(x >= 0 && z >= 0 && x < state->grid[2].width && z < state->grid[2].height);
					state->grid[2].tag_field[x+z*state->grid[2].width] = ceiling_tag; // Ceiling
					state->grid[2].data[x+z*state->grid[2].width].height = room->height;

					if (grid_value == 2)
					{
						// Outside of room
						state->grid[0].data[x+z*state->grid[0].width].dont_place = true; // Floors
						state->grid[2].data[x+z*state->grid[2].width].dont_place = true; // Ceiling 
					}
					else if (grid_value == 1)
					{
						// In a wall
						state->grid[0].tag_field[x+z*state->grid[0].width] = floor_wall_tag; // Floors
						state->grid[0].data[x+z*state->grid[0].width].height = room->height;
						state->grid[0].data[x+z*state->grid[0].width].dummy = true;

						state->grid[1].tag_field[x+z*state->grid[1].width] = wall_tag; // Walls
						state->grid[1].data[x+z*state->grid[1].width].height = room->height;

						state->grid[2].data[x+z*state->grid[2].width].dont_place = true; // Ceiling
					}
					else
					{
						// Inside of room
						state->grid[1].tag_field[x+z*state->grid[1].width] = wall_inside_tag; // Walls
						state->grid[1].data[x+z*state->grid[1].width].height = room->height;
						state->grid[0].tag_field[x+z*state->grid[0].width] = floor_tag; // Floors
						state->grid[0].data[x+z*state->grid[0].width].height = room->height;
					}
				}
			}

			for (j = 0; j < eaSize(&room->doors); j++)
			{
				char point_name[128];
				genesisInteriorGetDoorPos(room, j, &x, &z, NULL, false);
				x -= state->bounds[0];
				z -= state->bounds[1];
				assert(x >= 0 && z >= 0 && x < state->grid[2].width && z < state->grid[2].height);
				if (room->doors[j]->dir_flags == GENESIS_DOOR_ENTERING)
					state->grid[1].tag_field[x+z*state->grid[1].width] = wall_enter_tag;	
				else if (room->doors[j]->dir_flags == GENESIS_DOOR_EXITING) 
					state->grid[1].tag_field[x+z*state->grid[1].width] = wall_exit_tag;	
				else
					state->grid[1].tag_field[x+z*state->grid[1].width] = wall_door_tag;	
				if (room->geo)
					state->grid[1].data[x+z*state->grid[1].width].dummy = true;

				if (room->doors[j]->hallway == NULL)
				{
					state->grid[1].data[x+z*state->grid[1].width].place_cap = true;
				}
				else if (state->override_positions && room->doors[j]->hallway != NULL && !room->doors[j]->always_open)
				{
					state->grid[1].data[x+z*state->grid[1].width].place_door = true;
				}

				sprintf(point_name, "DoorCap_%s_%s_%s_SPAWN", room->doors[j]->name, room->src->room.name, state->layout->layout_name);
				state->grid[1].data[x+z*state->grid[1].width].spawn_point_name = allocAddString(point_name);
				sprintf(point_name, "DoorCap_%s_%s_%s", room->doors[j]->name, room->src->room.name, state->layout->layout_name);
				state->grid[1].data[x+z*state->grid[1].width].cap_name = allocAddString(point_name);

				genesisInteriorGetDoorPos(room, j, &x, &z, NULL, true);
				x -= state->bounds[0];
				z -= state->bounds[1];
				assert(x >= 0 && z >= 0 && x < state->grid[2].width && z < state->grid[2].height);
				state->grid[1].tag_field[x+z*state->grid[1].width] = hallway_tag;	
				state->grid[1].data[x+z*state->grid[1].width].dummy = true;
			}

			printf("GENESIS INTERIOR: Floors\n");
			genesisBuildGrid(state, &state->grid[0], room, room_table, EXCLUDE_VOLUME_FLOOR, GENESIS_INTERIOR_FLOOR);
// 			{
// 				char filename[MAX_PATH];
// 				sprintf(filename, "C:\\INTERIOR_DEBUG\\%s_floor.csv", room->name);
// 				genesisPrintGrid(&state->grid[0], filename);
// 			}
// 
			printf("GENESIS INTERIOR: Walls\n");
			genesisBuildGrid(state, &state->grid[1], room, room_table, EXCLUDE_VOLUME_WALL, GENESIS_INTERIOR_WALL);
// 			{
// 				char filename[MAX_PATH];
// 				sprintf(filename, "C:\\INTERIOR_DEBUG\\%s_walls.csv", room->src->room.name);
// 				genesisPrintGrid(&state->grid[1], filename);
// 			}

			printf("GENESIS INTERIOR: Ceilings\n");
			genesisBuildGrid(state, &state->grid[2], room, room_table, EXCLUDE_VOLUME_CEILING, GENESIS_INTERIOR_CEILING);
// 			{
// 				char filename[MAX_PATH];
// 				sprintf(filename, "C:\\INTERIOR_DEBUG\\%s_ceilings.csv", room->name);
// 				genesisPrintGrid(&state->grid[2], filename);
// 			}
		}
		else if (room->geo)
		{
			for (j = 0; j < eaSize(&room->doors); j++)
			{
				genesisInteriorGetDoorPos(room, j, &x, &z, NULL, false);
				x -= state->bounds[0];
				z -= state->bounds[1];
				if (state->override_positions)
				{
					if (room->doors[j]->hallway == NULL && eaSize(&state->cap_elements) > 0)
					{
						F32 angle = room->doors[j]->rotation*PI*0.5f; 
						GenesisInteriorElement *cap_element = state->cap_elements[0];
						genesisInteriorPlaceElementObject(state, &cap_element->primary_object, x, (room->height+room->doors[j]->y)*state->floor_height, z, angle, room->parent_group_objects, &room->parent_volumes, EXCLUDE_VOLUME_WALL, NULL, NULL);
					}
					else if (room->doors[j]->hallway != NULL && eaSize(&state->door_elements) > 0 && !room->doors[j]->always_open)
					{
						F32 angle = room->doors[j]->rotation*PI*0.5f; 
						GenesisInteriorElement *door_element = state->door_elements[0];
						genesisInteriorPlaceElementObject(state, &door_element->primary_object, x, (room->height+room->doors[j]->y)*state->floor_height, z, angle, room->parent_group_objects, &room->parent_volumes, EXCLUDE_VOLUME_WALL, NULL, NULL);
					}
				}
			}
		}
		else if (room->cap)
		{
			int px = room->x-state->bounds[0];
			int pz = room->z-state->bounds[1];

			memset(state->grid[0].tag_field, 0, sizeof(GenesisInteriorTag*)*state->grid[0].width*state->grid[0].height);
			memset(state->grid[0].data, 0, sizeof(GenesisInteriorGridElement)*state->grid[0].width*state->grid[0].height);

			assert(px >= 0 && pz >= 0 && px < state->grid[2].width && pz < state->grid[2].height);
			state->grid[0].tag_field[px+pz*state->grid[0].width] = hallway_tag; // Floors
			state->grid[0].data[px+pz*state->grid[0].width].height = room->height;
			state->grid[0].data[px+pz*state->grid[0].width].room = room;

			for ( g=0; g < eaSize(&state->hallway_groups); g++ )
			{
				GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
				for (k = 0; k < eaSize(&hallway_group->hallways); k++)
				{
					GenesisInteriorHallway *hallway = hallway_group->hallways[k];
					if (hallway->end_room == room || hallway->start_room == room)
					{
						for (j = 0; j < eaiSize(&hallway->points); j+=4)
						{
							x = hallway->points[j+0]-state->bounds[0];
							z = hallway->points[j+2]-state->bounds[1];
							assert(x >= 0 && z >= 0 && x < state->grid[2].width && z < state->grid[2].height);
							state->grid[0].tag_field[x+z*state->grid[0].width] = hallway_tag;
							state->grid[0].data[x+z*state->grid[0].width].dummy = true;
							state->grid[0].data[x+z*state->grid[0].width].height = hallway->points[j+1];
						}
					}
				}
			}

			state->grid[0].data[px+pz*state->grid[0].width].dummy = false;

			printf("GENESIS INTERIOR: Cap\n");
			genesisBuildGrid(state, &state->grid[0], room, room_table, 0, GENESIS_INTERIOR_CAP);
			//sprintf(filename, "C:\\INTERIOR_DEBUG\\%s_cap.csv", room->name);
			//genesisPrintGrid(&state->grid[0], filename);
		}
		mersenneTableFree(room_table);

		if (room->parent_hallway) {
			eaPushEArray(&room->parent_hallway->parent_volumes,
						 &room->parent_volumes );
			eaDestroy( &room->parent_volumes ); //< prevent double-free
		}
	}
}

static void genesisInteriorDrawHallways(GenesisInteriorState *state)
{
	int i, j, k, g, x, z;
	GenesisInteriorTag *wall_door_tag, *hallway_tag;

	// Find appropriate tags in kit
	wall_door_tag = genesisInteriorFindTag(state->kit, "WallDoor");
	hallway_tag = genesisInteriorFindTag(state->kit, "Hallway");

	for ( g=0; g < eaSize(&state->hallway_groups); g++ )
	{
		GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
		for (i = 0; i < eaSize(&hallway_group->hallways); i++)
		{
			GenesisInteriorHallway *hallway = hallway_group->hallways[i];

			memset(state->grid[1].tag_field, 0, sizeof(GenesisInteriorTag*)*state->grid[0].width*state->grid[0].height);
			memset(state->grid[1].data, 0, sizeof(GenesisInteriorGridElement)*state->grid[0].width*state->grid[0].height);

			for (j = 0; j < eaSize(&state->room_list); j++)
			{
				GenesisInteriorRoom *room = state->room_list[j];
				if(hallway->start_room != room && hallway->end_room != room)
					continue;
				for (k = 0; k < eaSize(&room->doors); k++)
				{
					genesisInteriorGetDoorPos(room, k, &x, &z, NULL, false);
					x -= state->bounds[0];
					z -= state->bounds[1];
					assert(x >= 0 && z >= 0 && x < state->grid[2].width && z < state->grid[2].height);
					state->grid[1].tag_field[x+z*state->grid[1].width] = wall_door_tag;
					state->grid[1].data[x+z*state->grid[1].width].dummy = true;
				}
			}
			for (j = 0; j < eaiSize(&hallway->points); j+=4)
			{
				x = hallway->points[j+0]-state->bounds[0];
				z = hallway->points[j+2]-state->bounds[1];
				assert(x >= 0 && z >= 0 && x < state->grid[2].width && z < state->grid[2].height);
				state->grid[1].tag_field[x+z*state->grid[1].width] = hallway_tag;
				state->grid[1].data[x+z*state->grid[1].width].hallway = hallway;
				state->grid[1].data[x+z*state->grid[1].width].hallway_segment = hallway->points[j+3];
				state->grid[1].data[x+z*state->grid[1].width].height = hallway->points[j+1];
				if (j > 3 && j < eaiSize(&hallway->points) - 4 &&
					(hallway->points[j+5] > hallway->points[j+1] ||
					hallway->points[j-3] > hallway->points[j+1]))
				{
					// This is a ramp!
					if (hallway->points[j+5] > hallway->points[j+1])
					{
						int dir = (hallway->points[j+4] < hallway->points[j+0]) ? 1 :
							((hallway->points[j+6] > hallway->points[j+2]) ? 2 : 
							 ((hallway->points[j+6] < hallway->points[j+2]) ? 0 : 3));
						state->grid[1].data[x+z*state->grid[1].width].edge_heights[dir] = 1;
						printf("%d %d (%d) = 1\n", x, z, dir);
					}
					else
					{
						int dir = (hallway->points[j-4] < hallway->points[j+0]) ? 1 :
							((hallway->points[j-2] > hallway->points[j+2]) ? 2 : 
							((hallway->points[j-2] < hallway->points[j+2]) ? 0 : 3));
						state->grid[1].data[x+z*state->grid[1].width].edge_heights[dir] = 1;
						printf("%d %d (%d) = 1\n", x, z, dir);
					}
				}
			}

			printf("GENESIS INTERIOR: Hallway %s, Part %d\n", hallway_group->src_name, i);
			genesisBuildGrid(state, &state->grid[1], NULL, state->random_table, EXCLUDE_VOLUME_WALL, GENESIS_INTERIOR_HALLWAY);
// 			{
// 				char filename[MAX_PATH];
// 				sprintf(filename, "C:\\INTERIOR_DEBUG\\hallway_%d_%d.csv", g, i);
// 				genesisPrintGrid(&state->grid[1], filename);
// 			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// STEP 10: Place the non-infrastructure objects in the rooms & hallways
///////////////////////////////////////////////////////////////////////////

static void genesisInteriorPopulateRoom(int iPartitionIdx, GenesisInteriorState *state, GenesisMissionRequirements **mission_reqs,
										GenesisInteriorRoom *room, int debug_index, Mat4 room_origin)
{
	int j, k;
	int px = room->x-state->bounds[0];
	int pz = room->z-state->bounds[1];
	int pw = (room->rot%2) == 0 ? room->width : room->depth;
	int ph = (room->rot%2) == 0 ? room->depth : room->width;
	GenesisPopulateArea area = { 0 };
	GenesisDetailKitAndDensity *detail_kits[3] = {&room->src->detail_kit_1, &room->src->detail_kit_2, &state->kit->light_dummy};
	area.prefab_library_piece = room->src->room.library_piece;
	copyMat4(room_origin, area.room_origin);
	setVec3(area.min, (px-0.5f)*state->spacing+state->kit->room_padding, room->height*state->floor_height, (pz-0.5f)*state->spacing+state->kit->room_padding);
	setVec3(area.max, (px+pw-0.5f)*state->spacing-state->kit->room_padding, room->height*state->floor_height+25.f, (pz+ph-0.5f)*state->spacing-state->kit->room_padding); 
	area.padding_used = state->kit->room_padding;
	area.no_details_or_paths = room->no_details_or_paths;
	area.volumized = room->volumized;

	printf("GENESIS POPULATE ROOM %d / %s - (%f,%f)-(%f,%f)\n", debug_index, room->src_name,
		area.min[0],area.min[2],area.max[0],area.max[2]);

	for (j = 0; j < eaSize(&room->areas); j++)
	{
		GenesisRoomRectArea *room_area = room->areas[j];
		if (room_area->type == GENESIS_ROOM_RECT_SUBTRACT)
		{
			int x1, x2, z1, z2;
			ExclusionObject *exclusion_object = calloc(1, sizeof(ExclusionObject));
			ExclusionVolumeGroup *exclusion_volume_group = calloc(1, sizeof(ExclusionVolumeGroup));
			ExclusionVolume *volume = calloc(1, sizeof(ExclusionVolume));
			volume->type = EXCLUDE_VOLUME_BOX;
			genesisInteriorRotatePoint(room, room_area->x, room_area->z, &x1, &z1);
			genesisInteriorRotatePoint(room, room_area->x+room_area->width-1, room_area->z+room_area->depth-1, &x2, &z2);
			x1 -= state->bounds[0]; x2 -= state->bounds[0];
			z1 -= state->bounds[1]; z2 -= state->bounds[1];
			setVec3(volume->extents[0], (MIN(x1,x2)-0.5f)*state->spacing, area.min[1], (MIN(z1,z2)-0.5f)*state->spacing);
			subVec3same(volume->extents[0], state->kit->room_padding, volume->extents[0]);
			setVec3(volume->extents[1], (MAX(x1,x2)+0.5f)*state->spacing, area.max[1], (MAX(z1,z2)+0.5f)*state->spacing);
			addVec3same(volume->extents[1], state->kit->room_padding, volume->extents[1]);
			volume->begin_radius = volume->end_radius = MAX(lengthVec3(volume->extents[0]), lengthVec3(volume->extents[1]));
			identityMat4(volume->mat);
			volume->collides = true;

			eaPush(&exclusion_volume_group->volumes, volume);
			exclusion_object->volume_group = exclusion_volume_group;
			exclusion_object->flags |= EXCLUDE_VOLUME_SUBTRACT;
			identityMat4(exclusion_object->mat);
			exclusion_object->max_radius = 1e8;
			eaPush(&area.excluders, exclusion_object);
		}
	}

	for (j = 0; j < eaSize(&room->parent_volumes); j++)
		eaPush(&area.excluders, room->parent_volumes[j]);
	eaDestroy(&room->parent_volumes); // Prevent double-free

	for (j = 0; j < eaSize(&room->doors); j++)
	{
		int door_x, door_z;
		GenesisInteriorDoor *door = room->doors[j];
		GenesisPopulateDoor *point = calloc(1, sizeof(GenesisPopulateDoor));

		// clock-wise rotation
		switch (room->rot)
		{
			xcase 0:
		door_x = door->x;
		door_z = door->z;
		xcase 1:
		door_x = door->z;
		door_z = room->width - 1 - door->x;
		xcase 2:
		door_x = room->width - 1 - door->x;
		door_z = room->depth - 1 - door->z;
		xcase 3:
		door_x = room->depth - 1 - door->z;
		door_z = door->x;
		};

		setVec3(point->point, (door_x+px)*state->spacing, room->height*state->floor_height, (door_z+pz)*state->spacing);

		if (door_x == 0)
			point->point[0] = area.min[0];
		else if (door_z == 0)
			point->point[2] = area.min[2];
		else if (door_x == pw-1)
			point->point[0] = area.max[0];
		else
			point->point[2] = area.max[2];

		point->entrance = !!(door->dir_flags & GENESIS_DOOR_ENTERING);
		point->exit = !!(door->dir_flags & GENESIS_DOOR_EXITING);
		point->dest_name = SAFE_MEMBER( door->hallway, parent_hallway->src_name );

		eaPush(&area.doors, point);
		printf("POINT %f %f %f\n", point->point[0],point->point[1],point->point[2]);
	}

	if (eaSize(&mission_reqs) > 0)
	{
		GenesisRoomMission *default_mission = genesisFindRoomMission( room->src->missions, mission_reqs[0]->missionName );

		if (default_mission)
		{
			for (k = 0; k < eaSize(&default_mission->replaces); k++)
			{
				GenesisInteriorReplace *replace = default_mission->replaces[k];
				if (replace->is_door && replace->final_x > -1)
				{
					int door_x = replace->final_x - px;
					int door_z = replace->final_z - pz;
					GenesisPopulateDoor *point = calloc(1, sizeof(GenesisPopulateDoor));

					setVec3(point->point, (door_x+px)*state->spacing, room->height*state->floor_height, (door_z+pz)*state->spacing);

					if (door_x == 0)
						point->point[0] = area.min[0];
					else if (door_z == 0)
						point->point[2] = area.min[2];
					else if (door_x == pw-1)
						point->point[0] = area.max[0];
					else
						point->point[2] = area.max[2];

					eaPush(&area.doors, point);
					printf("REPLACE %f %f %f\n", point->point[0],point->point[1],point->point[2]);
				}
			}
		}
	}
	{
		const char* room_name = (room->src_name ? room->src_name : "(unknown)");
		GenesisToPlaceObject **mission_volumes = NULL;
		GenesisRuntimeErrorContext* source_context = genesisMakeErrorContextRoom( room_name, state->layout->layout_name );
		
		for (j = 0; j != eaSize( &room->mission_volumes ); ++j) {
			eaPush( &mission_volumes, room->mission_volumes[j]->mission_volume );
		}

		if (!genesisPopulateArea(iPartitionIdx, debug_index, source_context,
								 &state->to_place, &area, room->src ? room->src->detail_seed : 0,
								 room->parent_group_shared, mission_volumes, mission_reqs, room->src->missions, room->door_caps,
								 detail_kits, false, false, false, false, state->no_sharing, -1, state->override_positions ))
		{
			genesisRaiseError(GENESIS_ERROR, source_context,
				"Failed to populate room. Try a bigger room or fewer and smaller objects." );
		}
		eaDestroy(&mission_volumes);

		StructDestroy( parse_GenesisRuntimeErrorContext, source_context );
	}

	eaDestroyEx(&area.doors, NULL);
	eaDestroy(&area.excluders);//These are freed in genesisPopulateArea
}

static void genesisGroupVolumesSpatially(ExclusionObject ***exclusion_objects, ExclusionVolume **volumes, Mat4 parent_mat)
{
	int i;
	if(eaSize(&volumes)==0)
	{
		ExclusionObject *exclude_object = calloc(1, sizeof(ExclusionObject));
		ExclusionVolumeGroup *group = calloc(1, sizeof(ExclusionVolumeGroup));
		group->idx_in_parent = 0;
		group->optional = false;
		exclude_object->volume_group = group;
		copyMat4(parent_mat, exclude_object->mat);
		exclude_object->max_radius = 1e8;
		eaPush(exclusion_objects, exclude_object);
	}
	for ( i=0; i < eaSize(&volumes); i++ )
	{
		Vec3 abs_min, abs_max, true_max;
		ExclusionVolume *volume = volumes[i];
		ExclusionObject *exclude_object = calloc(1, sizeof(ExclusionObject));
		ExclusionVolumeGroup *group = calloc(1, sizeof(ExclusionVolumeGroup));
		group->idx_in_parent = 0;
		group->optional = false;
		eaPush(&group->volumes, volume);
		mulMat4(parent_mat, volume->mat, exclude_object->mat);
		identityMat4(volume->mat);
		exclude_object->volume_group = group;
		exclude_object->flags |= (EXCLUDE_VOLUME_FLOOR|EXCLUDE_VOLUME_WALL|EXCLUDE_VOLUME_CEILING);
		if(volume->type == EXCLUDE_VOLUME_BOX)
		{		
			ABSVEC3(volume->extents[0], abs_min); 
			ABSVEC3(volume->extents[1], abs_max); 
			MAXVEC3(abs_min, abs_max, true_max);
			exclude_object->max_radius = lengthVec3(true_max);
		}
		else
		{
			exclude_object->max_radius = volume->end_radius;
		}

		eaPush(exclusion_objects, exclude_object);
	}
}

static void genesisInteriorFinishRooms(int iPartitionIdx, GenesisInteriorState *state, GenesisMissionRequirements **mission_reqs, bool detail)
{
	int i;

	// Key Lights & Drop-In Objects
	for (i = 0; i < eaSize(&state->room_list); i++)
	{
		GenesisInteriorRoom *room = state->room_list[i];
		int px = room->x-state->bounds[0];
		int pz = room->z-state->bounds[1];
		int pw = (room->rot%2) == 0 ? room->width : room->depth;
		int ph = (room->rot%2) == 0 ? room->depth : room->width;
		Mat4 placement_mat;
		
		identityMat3(placement_mat);

		if (room->geo)
		{
			GroupDef *def = objectLibraryGetGroupDefByName(room->geo, true);
			if (def)
			{
				ExclusionVolume **volumes=NULL;
				Mat4 offset_mat;
				GenesisToPlaceObject *dropin = calloc(1, sizeof(GenesisToPlaceObject));

				// Offset the object so the pivot is at the center of the bottom-left tile
				identityMat4(offset_mat);

				// Place the object's bottom-left corner correctly
				yawMat3(1.5707963f * room->rot, placement_mat);
				setVec3(placement_mat[3], px*state->spacing, room->height*state->floor_height, pz*state->spacing);
				switch (room->rot)
				{
				xcase 0:
					// Do nothing (keep at 0,0 corner)
				xcase 1:
					// Rotate to 0,+w corner
					placement_mat[3][2] += (room->width-1)*state->spacing;
				xcase 2:
					// Rotate to +w,+h corner
					placement_mat[3][0] += (room->width-1)*state->spacing;
					placement_mat[3][2] += (room->depth-1)*state->spacing;
				xcase 3:
					// Rotate to +h,0 corner
					placement_mat[3][0] += (room->depth-1)*state->spacing;
				}

				// Calculate final transform
				mulMat4(placement_mat, offset_mat, dropin->mat);

				dropin->uid = def->name_uid;
				dropin->seed = randomMersenneU32(state->random_table);
				dropin->parent = room->parent_group_objects;
				eaPush(&state->to_place.objects, dropin);

				exclusionGetDefVolumes(def, &volumes, unitmat, false, -1, NULL);
				genesisGroupVolumesSpatially(&room->parent_volumes, volumes, dropin->mat);
				eaDestroy(&volumes);
			}
		}
		else if (!room->cap && state->kit->key_light)
		{
 			GenesisToPlaceObject *light = calloc(1, sizeof(GenesisToPlaceObject));
			char *key_light = state->kit->key_light;
			identityMat3(light->mat);
			setVec3(light->mat[3], (px+(float)pw*0.5f)*state->spacing, room->height*10, (pz+(float)ph*0.5f)*state->spacing);
			light->uid = objectLibraryUIDFromObjName(key_light);
			light->parent = room->parent_group_room;
			eaPush(&state->to_place.objects, light);
		}
		
		// Room population
		if (detail)
		{
			if (!room->cap && room->src)
			{
				genesisInteriorPopulateRoom(iPartitionIdx, state, mission_reqs, room, i, placement_mat);
			}
		}
	}
}

static bool genesisHallwayPointShouldBeExcluded(GenesisInteriorState *state, GenesisInteriorHallwayGroup *hallway_group, int x, int z)
{
	int it;
	int pointIt;

	for( it = 0; it != eaSize( &hallway_group->hallways ); ++it ) {
		GenesisInteriorHallway* hallway = hallway_group->hallways[ it ];
		
		assert( eaiSize( &hallway->points ) % 4 == 0 );
		for( pointIt = 0; pointIt < eaiSize( &hallway->points ); pointIt += 4 ) {
			int pointX = hallway->points[ pointIt + 0 ] - state->bounds[0];
			int pointZ = hallway->points[ pointIt + 2 ] - state->bounds[1];

			if( pointX == x && pointZ == z ) {
				return false;
			}
		}
	}
   
	return true;
}

static void genesisCalculateHallwayDoorPosition(Vec3 outDoorPos, GenesisInteriorHallway *hallway, GenesisInteriorRoom* room, bool isStart )
{
	int width = (room->rot%2) == 0 ? room->width : room->depth;
	int depth = (room->rot%2) == 0 ? room->depth : room->width;
	
	int px;
	int py;
	int pz;

	assert( eaiSize( &hallway->points ) >= 4 );
	if( isStart == hallway->from_start ) {
		px = hallway->points[0];
		py = hallway->points[1];
		pz = hallway->points[2];
	} else {
		int endIdx = eaiSize( &hallway->points ) - 4;
		px = hallway->points[endIdx + 0];
		py = hallway->points[endIdx + 1];
		pz = hallway->points[endIdx + 2];

	}
			
	if (px == room->x-1)
	{
		setVec3(outDoorPos, px + 0.5, py, pz );
	}
	else if (px == room->x+width)
	{
		setVec3(outDoorPos, px - 0.5, py, pz );
	}
	else if (pz == room->z-1)
	{
		setVec3(outDoorPos, px, py, pz + 0.5 );
	}
	else if (pz == room->z+depth)
	{
		setVec3(outDoorPos, px, py, pz - 0.5 );
	}
	else
	{
		setVec3(outDoorPos, px, py, pz );
	}
}

static void genesisInteriorPopulateHallway(int iPartitionIdx, GenesisInteriorState *state, GenesisMissionRequirements **mission_reqs,
										   GenesisInteriorHallwayGroup *hallway_group, int debug_index, F32 detail_density)
{
	GenesisDetailKitAndDensity *detail_kit_and_density[3] = { &hallway_group->src->detail_kit_1, &hallway_group->src->detail_kit_2, &state->kit->light_dummy };
	GenesisPopulateArea area = { 0 };
	bool is_welded = true;
	
	if (!hallway_group->src)
		return;

	// figure out the maximal area
	setVec3(area.min,  1e8,  1e8,  1e8);
	setVec3(area.max, -1e8, -1e8, -1e8);
	{
		int it;
		int pointIt;

		for( it = 0; it != eaSize( &hallway_group->hallways ); ++it ) {
			GenesisInteriorHallway* hallway = hallway_group->hallways[it];
			for( pointIt = 0; pointIt != eaiSize(&hallway->points); pointIt += 4 ) {
				int x = hallway->points[pointIt+0]-state->bounds[0];
				int y = hallway->points[pointIt+1];
				int z = hallway->points[pointIt+2]-state->bounds[1];
				
				area.min[0] = MIN(area.min[0], (x-0.5f)*state->spacing);
				area.min[2] = MIN(area.min[2], (z-0.5f)*state->spacing);
				area.max[0] = MAX(area.max[0], (x+0.5f)*state->spacing);
				area.max[2] = MAX(area.max[2], (z+0.5f)*state->spacing);
				
				area.min[1] = MIN(area.min[1], y * state->floor_height);
				area.max[1] = MAX(area.max[1], y * state->floor_height + 40);
			}
		}
	}
	area.no_details_or_paths = hallway_group->no_details_or_paths;
	
	printf("GENESIS POPULATE PATH %d/%s - (%f,%f)-(%f,%f)\n",
		   debug_index, hallway_group->src_name,
		   area.min[0],area.min[2],area.max[0],area.max[2]);

	// Exclude all the non-straight-hallway areas
	{
		ExclusionObject *exclusion_object = calloc(1, sizeof(ExclusionObject));
		ExclusionVolumeGroup *excluders = calloc(1, sizeof(ExclusionVolumeGroup));
		int x;
		int z;

		for (x = 0; x <= state->bounds[2]-state->bounds[0]; x++) {
			for (z = 0; z <= state->bounds[3]-state->bounds[1]; z++) {
				if (genesisHallwayPointShouldBeExcluded(state, hallway_group, x, z)) {
					ExclusionVolume *volume = calloc(1, sizeof(ExclusionVolume));
					volume->type = EXCLUDE_VOLUME_BOX;
					setVec3(volume->extents[0], (x-0.5f)*state->spacing, -1e8, (z-0.5f)*state->spacing);
					setVec3(volume->extents[1], (x+0.5f)*state->spacing, 1e8, (z+0.5f)*state->spacing);
					volume->begin_radius = volume->end_radius = MAX(lengthVec3(volume->extents[0]), lengthVec3(volume->extents[1]));
					identityMat4(volume->mat);
					volume->collides = true;
					eaPush(&excluders->volumes, volume);
				}
			}
		}
		exclusion_object->volume_group = excluders;
		identityMat4(exclusion_object->mat);
		exclusion_object->max_radius = 1e8;
		eaPush(&area.excluders, exclusion_object);
	}
	
	eaPushEArray(&area.excluders, &hallway_group->parent_volumes);
	eaDestroy(&hallway_group->parent_volumes); // Prevent double-free

	// Add doors for the hallway start/end
	{
		int it;
		
		for( it = 0; it != eaSize( &hallway_group->hallways ); ++it ) {
			GenesisInteriorHallway *hallway = hallway_group->hallways[it];

			if (eaiSize(&hallway->points) == 0)
				continue;

			is_welded = false;

			if( hallway->start_room_has_door ) {
				Vec3 doorPos;
				GenesisPopulateDoor* door;

				genesisCalculateHallwayDoorPosition( doorPos, hallway, hallway->start_room, true );

				door = calloc(1, sizeof(GenesisPopulateDoor));
				door->entrance = true;
				door->point[0] = (doorPos[0] - state->bounds[0]) * state->spacing;
				door->point[1] = doorPos[1] * state->floor_height;
				door->point[2] = (doorPos[2] - state->bounds[1]) * state->spacing;
				door->dest_name = hallway->start_room->src_name;
				eaPush( &area.doors, door );
			}
			if( hallway->end_room_has_door ) {
				Vec3 doorPos;
				GenesisPopulateDoor* door;

				genesisCalculateHallwayDoorPosition( doorPos, hallway, hallway->end_room, false );

				door = calloc(1, sizeof(GenesisPopulateDoor));
				door->exit = true;
				door->point[0] = (doorPos[0] - state->bounds[0]) * state->spacing;
				door->point[1] = doorPos[1] * state->floor_height;
				door->point[2] = (doorPos[2] - state->bounds[1]) * state->spacing;
				door->dest_name = hallway->end_room->src_name;
				eaPush( &area.doors, door );
			}
		}
	}
	
	if (!is_welded)
	{
		GenesisRuntimeErrorContext* source_context = genesisMakeErrorContextPath( hallway_group->src_name, state->layout->layout_name );		
		GenesisToPlaceObject **mission_volumes = NULL;
		{
			int it;
			for( it = 0; it != eaSize( &hallway_group->mission_volumes ); ++it ) {
				eaPush( &mission_volumes, hallway_group->mission_volumes[it]->mission_volume);
			}
		}

		if (!genesisPopulateArea(iPartitionIdx, debug_index, source_context,
								 &state->to_place, &area,
								 hallway_group->src ? hallway_group->src->detail_seed : 0,
								 hallway_group->parent_group_shared, mission_volumes, mission_reqs,
								 hallway_group->src ? hallway_group->src->missions : NULL,
								 NULL,
								 detail_kit_and_density, false, false, true, false, state->no_sharing, state->spacing, state->override_positions ))
		{
			genesisRaiseError(GENESIS_ERROR, source_context,
				"Failed to populate path. Try fewer or smaller objects.");
		}
		
		eaDestroy(&mission_volumes);
		StructDestroy( parse_GenesisRuntimeErrorContext, source_context );
	}

	eaDestroyEx(&area.doors, NULL);
	eaDestroy(&area.excluders);//These are freed in genesisPopulateArea
}

static void genesisInteriorFinishHallways(int iPartitionIdx, GenesisInteriorState *state, GenesisMissionRequirements **mission_reqs, bool detail)
{
	int g;
	// Hallway population
	if (detail)
	{
		for ( g=0; g < eaSize(&state->hallway_groups); g++ )
		{
			GenesisInteriorHallwayGroup *hallway_group = state->hallway_groups[g];
			genesisInteriorPopulateHallway(iPartitionIdx, state, mission_reqs, hallway_group, 100+g, hallway_group->detail_density);
		}
	}
}

//////////////////////////////////////////////////////////////////
// De-initialization code
//////////////////////////////////////////////////////////////////

void genesisInteriorFreeRoom(GenesisInteriorRoom *room)
{
	eaDestroyEx(&room->parent_volumes, exclusionObjectFree);
	eaDestroyEx(&room->doors, NULL);
	if(room->no_src)
		StructFreeString(room->src_name);
	eaDestroyStruct(&room->door_caps, parse_GenesisObject);
	SAFE_FREE(room->grid);
	SAFE_FREE(room);
}

void genesisInteriorFreeHallway(GenesisInteriorHallway *hallway)
{
	eaiDestroy(&hallway->points);
	SAFE_FREE(hallway);
}

void genesisInteriorFreeHallwayGroup(GenesisInteriorHallwayGroup *hallway_group)
{
	eaDestroyEx(&hallway_group->hallways, genesisInteriorFreeHallway);
	eaDestroyEx(&hallway_group->parent_volumes, exclusionObjectFree);
	eaDestroy(&hallway_group->rooms);
	StructFreeString(hallway_group->src_name);
	SAFE_FREE(hallway_group);
}

void genesisInteriorFreeState(GenesisInteriorState *state)
{
	int i;
	eaDestroy(&state->cap_elements);
	eaDestroy(&state->door_elements);
	if (state->kit)
	{
		StructDestroy(parse_GenesisInteriorKit, state->kit);
	}
	eaDestroyEx(&state->rooms, genesisInteriorFreeRoom);
	eaDestroy(&state->room_list);

	eaDestroyEx(&state->hallway_groups, genesisInteriorFreeHallwayGroup);
	for (i = 0; i < 3; i++)
	{
		SAFE_FREE(state->grid[i].data);
		SAFE_FREE(state->grid[i].tag_field);
	}

	FOR_EACH_IN_STASHTABLE(state->pattern_table, GenesisInteriorPatternList, list)
	{
		eaDestroyEx(&list->list, NULL);
		SAFE_FREE(list);
	}
	FOR_EACH_END
	mersenneTableFree(state->random_table);
}

//////////////////////////////////////////////////////////////////
// Debug function - Prints the current grid requirements
//////////////////////////////////////////////////////////////////

void genesisPrintGrid(GenesisInteriorGrid *grid, char *name)
{
	int x, y;
	FILE *fOut;
	
	fOut = fopen(name, "w");
	if (!fOut)
		return;
	for (y = 0; y < grid->height; y++)
	{
		for (x = 0; x < grid->width; x++)
		{
			if (grid->data[x+y*grid->width].element[0])
				fprintf(fOut, "%s [%d],",  grid->data[x+y*grid->width].element[0]->name, grid->data[x+y*grid->width].height);
			else if (grid->tag_field[x+y*grid->width])
				fprintf(fOut, "(%s) [%d],",  grid->tag_field[x+y*grid->width]->name, grid->data[x+y*grid->width].height);
			else
				fprintf(fOut, ",");
		}
		fprintf(fOut, "\n");
	}
	fclose(fOut);
}

GenesisZoneInterior *genesisGetInteriorByIndex(GenesisZoneMapData *gen_data, int idx)
{
	int cnt;
	if(!gen_data)
		return NULL;
	cnt = eaSize(&gen_data->genesis_interiors);
	if(idx < 0 || idx >= cnt)
		return NULL;
	return gen_data->genesis_interiors[idx];
}

//////////////////////////////////////////////////////////////////
// MAIN ENTRY POINT
//////////////////////////////////////////////////////////////////

void genesisInteriorPopulateLayer(int iPartitionIdx, ZoneMapInfo *zmap_info, GenesisViewType view_type, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer, GenesisZoneInterior *interior, int layer_idx)
{
	int i, j, k;
	GenesisInteriorState state = { 0 };
	GenesisInteriorRoom *first_room = NULL;
	U32 seed = (interior->layout_seed ? interior->layout_seed : zmap_info->genesis_data->seed + layer_idx);
	Vec3 layer_min, layer_max;
	Vec3 layer_center;

	genesisGetBoundsForLayer(zmap_info->genesis_data, GenesisMapType_Interior, layer_idx, layer_min, layer_max);
	addVec3(layer_min, layer_max, layer_center);
	scaleVec3(layer_center, 0.5f, layer_center);

	// Initialize our kit
	if (!genesisInteriorInitialize(&state, zmap_info, interior, seed))
	{
		genesisInteriorFreeState(&state);
		return;
	}

	printf("Generating Genesis interior map... (seed %X)\n", seed);

	// Connect up all the hallways. Return the room with the most connections.
	first_room = genesisInteriorConnectHallways(&state);
	assert(first_room);

	// Add doors for portals
	genesisInteriorAddPortalDoors(&state, mission_reqs);

	// Give names to any doors that do not already have one
	genesisInteriorEnsureDoorsHaveNames(&state);

	// Generate a room list, starting with the first room and working outward.
	genesisInteriorGenerateRoomList(&state, first_room);

	// Paint room areas into a buffer on each room.
	for (i = 0; i < eaSize(&state.rooms); i++)
	{
		genesisInteriorProcessRoomAreas(&state, state.rooms[i]);
	}
	
	// Place all the rooms
	if (!genesisInteriorPlaceRooms(&state))
	{
		genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(interior->layout_name), "Failed to place rooms in layout.");
		genesisInteriorFreeState(&state);
		return;
	}

	// Move the rooms up or down randomly to create variation
	genesisInteriorCalculateRoomHeights(&state, interior->vert_dir, !state.override_positions);

	// Clear the Replacement objects
	for (i = 0; i < eaSize(&state.room_list); i++)
	{
		GenesisInteriorRoom *room = state.room_list[i];
		if(room->src)
		{
			for (j = 0; j < eaSize(&room->src->missions); j++)
			{
				GenesisRoomMission *mission = room->src->missions[j];
				for (k = 0; k < eaSize(&mission->replaces); k++)
				{
					mission->replaces[k]->final_x = -1;
					mission->replaces[k]->final_z = -1;
				}
			}
		}
	}

	// Initialize the grid
	state.bounds[0]--;
	state.bounds[1]--;
	state.bounds[2]++;
	state.bounds[3]++;
	assert(state.bounds[2] > state.bounds[0] && state.bounds[3] > state.bounds[1]);
	genesisInteriorInitializeGrid(&state);

	// Create the mission, room & hallway volumes, lights, etc.
	genesisInteriorCreateCommonItems(&state, mission_reqs, interior, layer_min, layer_max, layer_center);

	// Place the infrastructure for the rooms
	genesisInteriorDrawRooms(&state);

	// Place the infrastructure for the hallways
	genesisInteriorDrawHallways(&state);

	// Place one-off room geometry, and populate with objects
	genesisInteriorFinishRooms(iPartitionIdx, &state, mission_reqs, (view_type > GENESIS_VIEW_NODETAIL));

	// Populate hallways
	genesisInteriorFinishHallways(iPartitionIdx, &state, mission_reqs, (view_type > GENESIS_VIEW_NODETAIL));

	// Waypoint volumes may be a mistake
	genesisPopulateWaypointVolumesAsError(&state.to_place, mission_reqs, GENESIS_WARNING);
	genesisPopulateWaypointVolumes(&state.to_place, mission_reqs);

	if (layer)
	{

		// Actually create the GroupDefs we've queued up in the object list
		genesisPlaceObjects(zmap_info, &state.to_place, layer->grouptree.root_def);

		//Add layer center to children of the root def
		for ( i=0; i < eaSize(&layer->grouptree.root_def->children); i++ )
		{
			GroupChild *pGroupChild = layer->grouptree.root_def->children[i];
			addVec3(pGroupChild->mat[3], layer_center, pGroupChild->mat[3]);
		}
	}

	genesisInteriorFreeState(&state);
}

#endif

/// Fixup function for GenesisInteriorLayout
TextParserResult fixupGenesisInteriorLayout(GenesisInteriorLayout *pInterior, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pInterior->info.old_room_kit_tags ) {
					eaDestroyEx( &pInterior->info.room_kit_tag_list, StructFreeString );
					DivideString( pInterior->info.old_room_kit_tags, ",", &pInterior->info.room_kit_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pInterior->info.old_room_kit_tags );
				}
				if( pInterior->info.old_light_kit_tags ) {
					eaDestroyEx( &pInterior->info.light_kit_tag_list, StructFreeString );
					DivideString( pInterior->info.old_light_kit_tags, ",", &pInterior->info.light_kit_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pInterior->info.old_light_kit_tags );
				}

				fixupGenesisDetailKitLayout(&pInterior->detail_kit_1, eType, pExtraData);
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

#include "wlGenesisInterior_h_ast.c"
