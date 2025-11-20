#ifndef _DOOR_TRANSITION_COMMON_H_
#define _DOOR_TRANSITION_COMMON_H_
#pragma once
GCC_SYSTEM

#include "ReferenceSystem.h"
#include "WorldLibEnums.h"

typedef struct AIAnimList AIAnimList;
typedef struct AllegianceDef AllegianceDef;
typedef struct CutsceneDef CutsceneDef;
typedef struct RegionRules RegionRules;

AUTO_STRUCT;
typedef struct DoorTransitionAnimation
{
	// The animation to play
	REF_TO(AIAnimList) hAnimList;		AST(NAME(AnimationListName) STRUCTPARAM REFDICT(AIAnimList))
	// How much time to freeze player control to let the animation play 
	// If this is on a "departure sequence" it will decide how long to wait until the map transfer starts
	//(doesn't take effect if there is also a cutscene)
	F32 fDuration;						AST(NAME(FreezeDuration, Duration) DEFAULT(-1))
	// How much time to wait before starting the animation
	F32 fPreDelay;						AST(NAME(PreDelay))
} DoorTransitionAnimation;

extern ParseTable parse_DoorTransitionAnimation[];
#define TYPE_parse_DoorTransitionAnimation DoorTransitionAnimation

AUTO_ENUM;
typedef enum DoorTransitionType
{
	kDoorTransitionType_Unspecified = 0,
	kDoorTransitionType_Arrival,
	kDoorTransitionType_Departure,
} DoorTransitionType;

AUTO_STRUCT;
typedef struct DoorTransitionSequence
{
	// The type of transition (Arrival or Departure)
	DoorTransitionType eTransitionType;		AST(NAME(TransitionType))

	// The allegiance this animation corresponds to (leaving this blank implies "all allegiances")
	REF_TO(AllegianceDef) hAllegiance;		AST(NAME(Allegiance) REFDICT(Allegiance))

	// The region that the entity came from
	WorldRegionType* piSrcRegionTypes;		AST(NAME(SourceRegionType))
	// The region that the entity is going to
	WorldRegionType* piDstRegionTypes;		AST(NAME(DestinationRegionType))

	// The animation to play (along with animation properties)
	DoorTransitionAnimation* pAnimation;	AST(NAME(PlayAnimationList))

	// The cutscene to play
	REF_TO(CutsceneDef) hCutscene;			AST(NAME(PlayCutscene) REFDICT(Cutscene))

	// The movie to play
	const char* pchMovie;					AST(NAME(PlayMovie))

	// Rotate to a clearing before running the transition sequence
	bool bOrientToMapExit;					AST(NAME(OrientToMapExit))
} DoorTransitionSequence;

AUTO_STRUCT;
typedef struct DoorTransitionSequenceDef
{
	const char* pchName;					AST(STRUCTPARAM KEY POOL_STRING)
	const char* pchFileName;				AST(CURRENTFILE)
	const char* pchScope;					AST(NAME(Scope) POOL_STRING)

	// List of transition sequences to play under specific circumstances
	DoorTransitionSequence** eaSequences;	AST(NAME(TransitionSequence))
} DoorTransitionSequenceDef;

extern ParseTable parse_DoorTransitionSequenceDef[];
#define TYPE_parse_DoorTransitionSequenceDef DoorTransitionSequenceDef

AUTO_STRUCT;
typedef struct DoorTransitionSequenceRef
{
	REF_TO(DoorTransitionSequenceDef) hTransSequence; AST(NAME(TransitionSequence) STRUCTPARAM)
} DoorTransitionSequenceRef;

extern DictionaryHandle g_hDoorTransitionDict;
extern ParseTable parse_DoorTransitionSequenceRef[];
#define TYPE_parse_DoorTransitionSequenceRef DoorTransitionSequenceRef

DoorTransitionSequenceDef* DoorTransitionSequence_DefFromName(const char* pchName);

#endif //_DOOR_TRANSITION_COMMON_H_