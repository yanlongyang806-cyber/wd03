#pragma once
GCC_SYSTEM

#include "referencesystem.h"

typedef struct SkyInfo SkyInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n") AST_STRIP_UNDERSCORES;
typedef struct SkyInfoOverride
{
	REF_TO(SkyInfo) sky;				AST( STRUCTPARAM REFDICT(SkyInfo) )
	U32 override_time : 1;				AST( STRUCTPARAM NAME("OverrideTime") )
	F32 time;							AST( STRUCTPARAM NAME("Time") )
} SkyInfoOverride;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct SkyInfoGroup
{
	SkyInfoOverride **override_list;	AST( NAME(Sky) )			//List of overrides
} SkyInfoGroup;

extern ParseTable parse_SkyInfoOverride[];
#define TYPE_parse_SkyInfoOverride SkyInfoOverride
extern ParseTable parse_SkyInfoGroup[];
#define TYPE_parse_SkyInfoGroup SkyInfoGroup

int cmpSkyInfoGroup(const SkyInfoGroup *sky_group1, const SkyInfoGroup *sky_group2);
void createServerSkyDictionary(void);
