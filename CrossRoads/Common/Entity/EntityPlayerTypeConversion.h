/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITYPLAYERTYPECONVERSION_H
#define ENTITYPLAYERTYPECONVERSION_H

#pragma once
GCC_SYSTEM

typedef struct CharacterPath CharacterPath;
typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct PlayerTypeConversion PlayerTypeConversion;


S32 Entity_ConversionRequiresRespec(NOCONST(Entity) *pEnt, PlayerTypeConversion *pConversion, SA_PARAM_OP_VALID CharacterPath *pPath);
	//Returns true or false if this conversion requires a respec

S32 EntityUtil_MaxPlayerRespecConversions();
	//Returns the max number of player respec conversions

S32 Entity_ValidateConversion(Entity *pEnt, PlayerTypeConversion *pConversion, CharacterPath *pPath, SA_PARAM_OP_VALID int *pbRequiresRespec);
	// Returns true if the entity is allowed to do this conversions at this time
	//	the optional parameter bRequiresRespec will return the result of Entity_ConversionRequiresRespec
	//	which is called inside this function

#endif //ENTITYPLAYERTYPECONVERSION_H