/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#ifndef GSLCONTROLSCHEME_H__
#define GSLCONTROLSCHEME_H__

typedef struct ControlScheme ControlScheme;
typedef struct Entity Entity;
typedef struct RegionRules RegionRules;

void entity_SetCurrentScheme(Entity *e, const char *pch);
	// Sets the entity's current scheme to the named scheme.

void entity_UpdateForCurrentControlScheme(SA_PARAM_NN_VALID Entity *e);
	// Sets any transitory gameserver state based on the entity's control scheme.
	//   Will use the default scheme if the entity doesn't have current scheme.

void entity_SetSchemeDetails(Entity *e, ControlScheme *pScheme);

void entity_FixupControlSchemes(Entity *e);

void entity_SetValidControlSchemeForRegion(Entity* pEnt, RegionRules* pRegionRules);

#endif /* #ifndef GSLCONTROLSCHEME_H__ */

/* End of File */

