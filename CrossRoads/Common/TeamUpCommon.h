#include "referencesystem.h"
#include "Message.h"
#ifndef __TeamUpCommon_H_
#define __TeamUpCommon_H_


typedef struct Entity Entity;
typedef struct DisplayMessage DisplayMessage;

AUTO_STRUCT;
typedef struct TeamUpMember
{
	U32 iEntID;					AST(KEY)
	REF_TO(Entity) hEnt;		AST(COPYDICT(ENTITYPLAYER))

	//Maybe some display info for UI?

}TeamUpMember;

AUTO_STRUCT;
typedef struct TeamUpGroup
{
	int iGroupIndex;				AST(KEY NAME(GroupIndex))
	TeamUpMember **ppMembers;
}TeamUpGroup;

AUTO_ENUM;
typedef enum TeamUpState
{
	kTeamUpState_Invite = 0,
	kTeamUpState_Member,
}TeamUpState;

AUTO_STRUCT;
typedef struct TeamUpRequest
{
	U32 uTeamID;
	DisplayMessage msgDisplayMessage;	AST(STRUCT(parse_DisplayMessage))
	TeamUpState eState;
	U32 iGroupIndex;
	TeamUpGroup **ppGroups;			AST(NO_NETSEND)
}TeamUpRequest;

AUTO_STRUCT;
typedef struct TeamUpInit
{
	TeamUpGroup **ppGroups;
}TeamUpInit;

int TeamUp_GetTeamListSelfFirst(Entity* pEnt, Entity*** peaTeamEnts, TeamUpMember*** peaTeamMembers, int iGroupIndex, bool bIncludeSelf, bool bIncludePets);
void TeamUp_AddTeamToEntityList(Entity* pEnt, TeamUpGroup **ppGroups, Entity*** peaEntityList, Entity* pExcludeEntity, bool bIncludeSelf);

#endif