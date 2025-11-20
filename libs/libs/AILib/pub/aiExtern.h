#ifndef AIEXTERN_H
#define AIEXTERN_H

typedef struct AIVarsBase	AIVarsBase;
typedef struct Entity		Entity;
typedef struct CritterDef	CritterDef;
typedef struct Entity		Entity;
typedef struct ExprContext	ExprContext;
typedef struct AIVars		AIVars;

// Used to update the AI's cached perception radius to get around it being in game libs
void aiExternUpdatePerceptionRadii(Entity* be, AIVarsBase* aib);

// Used for adding things like health to the aggro expr context
void aiExternRegisterGameStatusExprInfo(ExprContext* context);
void aiExternAddGameStatusExprInfo(Entity* be, AIVarsBase* aib, ExprContext* context);

void aiExternGetHealth(const Entity* be, F32* health, F32* maxHealth);
void aiExternGetShields(const Entity* be, F32* pshield, F32* pmaxShield);
F32 aiExternGetStealth(Entity* be);

bool cutscene_GetNearbyCutscenes(int iPartiitonIdx, Vec3 centerPos, F32 dist);
void aiCivGiveCritterRandomName(Entity *e, const CritterDef* pCritterDef);

#endif