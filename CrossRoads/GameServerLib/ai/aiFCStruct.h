#ifndef AIFCSTRUCT_H
#define AIFCSTRUCT_H

#include "aiStruct.h"

typedef struct AIPowerConfigList	AIPowerConfigList;
typedef struct AIPowerInfo			AIPowerInfo;
typedef struct AIQueuedPower		AIQueuedPower;
typedef struct AttribMod			AttribMod;
typedef struct AttribModDef			AttribModDef;

typedef struct AIVars{
	AIVarsBase aibase;

}AIVars;

Entity* aiDetermineAggroEntity(Entity *dmgTarget, Entity *dmgSource, Entity *dmgOwner);

void aiFCNotify(Entity* be, AttribMod* mod, AttribModDef* moddef, F32 mag, F32 threatScale);

#endif