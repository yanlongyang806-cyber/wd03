/***************************************************************************



***************************************************************************/

#ifndef _WORLDGRIDLOADPRIAVTE_H_
#define _WORLDGRIDLOADPRIAVTE_H_
GCC_SYSTEM

#include "group.h"

typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct TerrainObjectEntry TerrainObjectEntry;
typedef struct TerrainBlockRange TerrainBlockRange;
typedef struct DefLoad DefLoad;

#define LIB_EXTENTIONS ".modelnames;.objlib;.rootmods"

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("") AST_IGNORE("END") AST_IGNORE_STRUCT("Rivers") AST_IGNORE_STRUCT("Roadlist") AST_IGNORE(roadparams) AST_IGNORE(TerrainEcotypeEntry) AST_IGNORE(UpsampleRes) AST_STRIP_UNDERSCORES;
typedef struct LibFileLoad
{
	const char* filename;							AST( CURRENTFILE )
	
	// GroupTree stuff
	int						version;				AST( NAME(Version) )
	GroupDef				**defs;					AST( NAME(Def) NO_INDEX )

	// Terrain stuff
	char					**material_table;		AST( NAME(TerrainMaterialEntry) )
	TerrainObjectEntry		**object_table;			AST( NAME(TerrainObjectEntry) )

	TerrainBlockRange		**blocks;				AST( NAME(TerrainBlockRange) STRUCT(parse_TerrainBlockRange) ) // in local map grid space

    bool					non_playable;			AST( NAME(NonPlayableTerrain) )
    TerrainExclusionVersion	exclusion_version;		AST( NAME(EnableExclusion) )
	F32						color_shift;			AST( NAME(ColorShift) )
} LibFileLoad;
extern ParseTable parse_LibFileLoad[];
#define TYPE_parse_LibFileLoad LibFileLoad

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#endif //_WORLDGRIDLOADPRIAVTE_H_
