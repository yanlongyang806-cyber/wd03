/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere
typedef struct Entity Entity;

#ifndef NO_EDITORS

void editor_RefreshAllShared(void);
void editor_FillEncounterNames(const char ***eaEncounterList);
void editor_FillEncounterActorNames(const char *pcEncounterName, const char ***eaActorList);
bool editor_GetEncounterActorEntityRef(const char *pcEncounterName, const char *pcActorName, EntityRef *pEntityRef);
void editor_FillEntityRefBoneNames(EntityRef entref, const char ***eaBoneList);
void editor_FillEntityBoneNames(Entity *pEntity, const char ***eaBoneList);

#endif
