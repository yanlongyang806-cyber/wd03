#pragma once
GCC_SYSTEM
#ifndef _WORLD_LIB_STRUCTS_H
#define _WORLD_LIB_STRUCTS_H

AUTO_STRUCT;
typedef struct GroupDefRef
{
	int name_uid;					AST( NAME(UID)	)
	char *name_str;					AST( NAME(Name) )
} GroupDefRef;

extern ParseTable parse_GroupDefRef[];
#define TYPE_parse_GroupDefRef GroupDefRef

// MJF: not sure I like this here, but otherwise there's no way
// outside of worldlib to look at a .zone file on disk
extern ParseTable parse_ZoneMapInfo[];
#define TYPE_parse_ZoneMapInfo ZoneMapInfo

#endif
