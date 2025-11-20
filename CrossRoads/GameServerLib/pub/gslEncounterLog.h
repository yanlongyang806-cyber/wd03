/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLENCOUNTERLOG_H__
#define GSLENCOUNTERLOG_H__

typedef struct PowerDef PowerDef;
typedef struct Entity Entity;


// Actions of an individual member
AUTO_STRUCT;
typedef struct EncounterLogMember
{
	S32 encounterID; // ID of who owns us
	EntityRef eRef;
	U32 containerType;
	U32 containerID;
	char debugName[MAX_NAME_LEN];
	int *piPowerUses;

	int iLevel;

	bool bEnemy;
} EncounterLogMember;

// Actions of both sides of an encounter
AUTO_STRUCT;
typedef struct EncounterLog
{
	S32 encounterID; // Opaque unique ID
	EncounterLogMember **ppMembers;

	int iSpawnLevel;
	int iSpawnTeamSize;
	const char *pTemplateName; AST(POOL_STRING)

	int *piEnemyPowerUses;
	int *piFriendlyPowerUses;

	bool bInvalid; // This is set when something weird happens, like pulling two encounters

} EncounterLog;

// Sets up a new encounter log holder
void gslEncounterLog_Register(S32 encounterID, int iSpawnLevel, int iSpawnTeamSize, const char *pTemplateName);

// Flags an entity as being "attached" to an encounter
void gslEncounterLog_AddEntity(S32 encounterID, Entity *ent, bool bEnemy);

// Get encounter from ID
EncounterLog *gslEncounterLog_LogFromID(S32 encounterID);

// Get log corresponding to an entity
EncounterLogMember *gslEncounterLog_MemberFromEntity(EntityRef eRef);

// Finish off an encounter log, which collates and prints the data, and then frees the structure
void gslEncounterLog_Finish(S32 encounterID);

// Logs a power activation
void gslEncounterLog_AddPowerActivation(EntityRef eRef, PowerDef *pDef);

#endif /* #ifndef GSLENCOUNTERLOG_H__ */

/* End of File */

