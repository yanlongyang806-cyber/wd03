#ifndef _GCLCOMBATADVANTAGE_H__
#define _GCLCOMBATADVANTAGE_H__

typedef struct Character Character;
typedef struct CombatTrackerNet CombatTrackerNet;
typedef struct Entity Entity;
typedef struct PowerApplication PowerApplication;
typedef struct PowerAnimFX PowerAnimFX;

void gclCombatAdvantage_ReportHitForTarget(Character *pcharTarget, PowerApplication *papp, PowerAnimFX *pafx);

void gclCombatAdvantage_ReportDamageFloat(CombatTrackerNet *pNet, Entity *pTarget, F32 fDelay);
void gclCombatAdvantage_EntityTick(F32 fElapsed, Entity *pEnt);

void gclCombatAdvantage_OncePerFrame(F32 fElapsed);




#endif