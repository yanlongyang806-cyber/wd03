#ifndef GSLBULLETINS_H_
#define GSLBULLETINS_H_

typedef struct Entity Entity;
typedef struct EventDef EventDef;

void gslBulletins_Update(Entity* pEnt, bool bForceUpdate);
void gslBulletins_AddEventBulletin(EventDef* pEventDef);

#endif