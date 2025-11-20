/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "referencesystem.h"

typedef struct MissionDef MissionDef;

#define MISSIONSET_BASE_DIR "defs/missionsets"
#define MISSIONSET_EXTENSION "missionset"
#define MISSIONSET_DOTEXTENSION ".missionset"

AUTO_STRUCT;
typedef struct MissionSetEntry
{
	REF_TO(MissionDef) hMissionDef;		AST( NAME("MissionDef") )
	int iMinLevel;
	int iMaxLevel;

} MissionSetEntry;

extern ParseTable parse_MissionSetEntry[];
#define TYPE_parse_MissionSetEntry MissionSetEntry


AUTO_STRUCT;
typedef struct MissionSet
{
	const char *pchName;				AST( STRUCTPARAM KEY POOL_STRING )
	const char *pchScope;				AST( SERVER_ONLY POOL_STRING )
	const char *pchFilename;			AST( CURRENTFILE )
	char *pchNotes;						AST( SERVER_ONLY )

	MissionSetEntry** eaEntries;		AST( NAME("Entry") )

} MissionSet;

extern ParseTable parse_MissionSet[];
#define TYPE_parse_MissionSet MissionSet


bool missionset_Validate(MissionSet *pSet);


extern DictionaryHandle g_MissionSetDictionary;
