#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// This file gives function definitions for all the functions that must
// be defined in a project for EntityLib to link properly

#include "textparser.h"
#include "GlobalTypeEnum.h"

typedef struct Entity Entity;
typedef struct ClientLink ClientLink;
typedef struct CmdContext CmdContext;

// Defines basic facts of the entity system
extern ParseTable parse_Entity[];
#define TYPE_parse_Entity Entity
extern ParseTable parse_Character[];
#define TYPE_parse_Character Character
extern ParseTable parse_Player[];
#define TYPE_parse_Player Player
extern ParseTable parse_CharacterAttribs[];
#define TYPE_parse_CharacterAttribs CharacterAttribs

// This function returns the entity referred to by a CmdContext. Behavior differs from client to server
Entity *entExternGetCommandEntity(CmdContext *context);

// Initialization that is project-specific but shared between client and server
void entExternInitializeCommon(SA_PARAM_NN_VALID Entity *ent);
void entExternCleanupCommon(SA_PARAM_NN_VALID Entity *ent);