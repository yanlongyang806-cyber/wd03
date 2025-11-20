#ifndef AIBEHAVIORDEBUG_H
#define AIBEHAVIORDEBUG_H

typedef struct AIBehavior		AIBehavior;
typedef struct AIBehaviorMod	AIBehaviorMod;
typedef struct AIBParsedString	AIBParsedString;
typedef struct AIVarsBase		AIVarsBase;
typedef struct Entity		Entity;

// debug functions
char* aiBehaviorDebugPrint(Entity* e, AIVarsBase* aibase, int stripColors);
char* aiBehaviorGetDebugString(Entity* e, AIVarsBase* aibase, AIBehavior* behavior, char* indent);
int aiBehaviorGetDataChunkSize(void);
int aiBehaviorGetTeamDataChunkSize(void);
char* aiBehaviorPrintMod(AIBehaviorMod* mod);
char* aiBehaviorDebugPrintParse(AIBParsedString* parsed);
int aiBehaviorDebugGetTableCount(void);
const char* aiBehaviorDebugGetTableEntry(int idx);
char* aiBehaviorDebugPrintPrevMods(Entity* e, AIVarsBase* ai);
char* aiBehaviorDebugPrintPendingMods(Entity* e, AIVarsBase* ai);

#endif