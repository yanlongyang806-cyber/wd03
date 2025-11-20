#include "pyLib.h"

// Entity API
// Player info
PY_FUNC(gclScript_GetPlayerRef);

// Info on arbitrary entities
PY_FUNC(gclScript_GetEntityName);
PY_FUNC(gclScript_GetEntityID);
PY_FUNC(gclScript_GetEntityHP);
PY_FUNC(gclScript_GetEntityMaxHP);
PY_FUNC(gclScript_GetEntityLevel);
PY_FUNC(gclScript_GetEntityTarget);
PY_FUNC(gclScript_GetEntityShields);
PY_FUNC(gclScript_GetEntityPos);
PY_FUNC(gclScript_GetEntityPYR);
PY_FUNC(gclScript_GetEntityDistance);
PY_FUNC(gclScript_GetEntityNearbyFriends);
PY_FUNC(gclScript_GetEntityNearbyHostiles);
PY_FUNC(gclScript_GetEntityNearbyObjects);

// Entity state flags
PY_FUNC(gclScript_IsEntityValid);
PY_FUNC(gclScript_IsEntityDead);
PY_FUNC(gclScript_IsEntityHostile);
PY_FUNC(gclScript_IsEntityCasting);

// Entity mission info
PY_FUNC(gclScript_DoesEntityHaveMission);
PY_FUNC(gclScript_DoesEntityHaveCompletedMission);
PY_FUNC(gclScript_IsEntityImportant);