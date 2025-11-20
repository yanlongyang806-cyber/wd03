#include "stdtypes.h"

typedef struct AITeam AITeam;
typedef struct Entity Entity;

AST_PREFIX(WIKI(AUTO))

AUTO_ENUM;
typedef enum AICombatTokenSetting {
	AITS_TokensNotUsed,
	AITS_TokensForAttacks,
	AITS_TokensForActive,
} AICombatTokenSetting;

AUTO_STRUCT WIKI("AIGroupCombatSettings");
typedef struct AIGroupCombatSettings 
{
	const char* filename;						AST( CURRENTFILE )

	char* name;									AST(KEY POOL_STRING STRUCTPARAM)

	// Base number of opponents who will actually fight in a combat, default=4
	S32 numCombatants;							AST(DEFAULT(3))

	// Additional combatants per teammate is only calculated when teammates > below number, default=5
	S32 baseTeammatesForAddtlCombatants;		AST(DEFAULT(5))

	// Fractional additional combatants per teammate, default=0
	F32 numAddtlCombatantsPerTeammate;

	// Additional combatants per player is only calculated when players > below number, default=2
	S32 basePlayersForAddtlCombatants;			AST(DEFAULT(2))

	// Fractional additional combatants per player, default=2
	F32 numAddtlCombatantsPerPlayer;			AST(DEFAULT(2))

	// Opponents who are not participating will use this animlist, default=none
	const char* animListForInactive;

	// Specifies how attack tokens are used (or if they aren't)
	AICombatTokenSetting tokenSetting;			AST(DEFAULT(1))

	// Specifies how long a token grants 'active' status, when tokenSetting==TokensForActive, default=10s
	F32 tokenActiveDuration;					AST(DEFAULT(10))

	// Specifies how long a critter will stay active before switching out
	F32 generalActiveDuration;					AST(DEFAULT(20))

	// Specifies how long a critter will stay inactive before switching back in
	F32 generalInactiveDuration;				AST(DEFAULT(20))

	// Specifies whether active combatants generate attack tokens or everyone on the team does, default=1
	U32 onlyCombatantsGenerateTokens : 1;		AST(DEFAULT(1))

	// Specifies whether TokensForActive can force a critter to be active (if none are available), default=1
	U32 forceInactiveForToken : 1;				AST(DEFAULT(1))
	
	// Specifies whether tokens will be spent or saved when they can't be used, default=1
	//   Only applies to TokensForActive
	U32 saveTokensWhenUnusable : 1;				AST(DEFAULT(1))
} AIGroupCombatSettings;

extern ParseTable parse_AIGroupCombatSettings[];
#define TYPE_parse_AIGroupCombatSettings AIGroupCombatSettings

// Perform team tick, which aggregates tokens, assigns tokens, sets guys as active/inactive
void aigcTick(AITeam *team, F32 elapsed);

// Get settings and clean out tokens
void aigcTeamEnterCombat(AITeam *team);
void aigcTeamExitCombat(AITeam *team);

// Clear references to ent
void aigcEntDestroyed(Entity* e);