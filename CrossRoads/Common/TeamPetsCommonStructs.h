#pragma once
GCC_SYSTEM


#include "GlobalTypes.h"

AUTO_STRUCT;
typedef struct PetToAddToTeam
{
	U32 iPetID;
	U32 ePetType;
}PetToAddToTeam;

AUTO_STRUCT;
typedef struct PetsToAddContainer
{
	EARRAY_OF(PetToAddToTeam) pets;
}PetsToAddContainer;

AUTO_STRUCT;
typedef struct AwayTeamMember
{
	GlobalType	eEntType;
	ContainerID	iEntID;
	U32			uiCritterPetID;
	bool		bIsReady;
} AwayTeamMember;

extern ParseTable parse_AwayTeamMember[];
#define TYPE_parse_AwayTeamMember AwayTeamMember

AUTO_STRUCT;
typedef struct AwayTeamMembers
{
	AwayTeamMember** eaMembers;
	S32 iMaxTeamSize;
} AwayTeamMembers;

extern ParseTable parse_AwayTeamMembers[];
#define TYPE_parse_AwayTeamMembers AwayTeamMembers

