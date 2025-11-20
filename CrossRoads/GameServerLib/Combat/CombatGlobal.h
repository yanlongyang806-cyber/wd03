/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef COMBATGLOBAL_H__
#define COMBATGLOBAL_H__

// Forward declarations
typedef struct AttribMod		AttribMod;
typedef struct CharacterClass	CharacterClass;
typedef struct PowerDef			PowerDef;

typedef U32 EntityRef;

// Adds an AttribMod to the global AttribMod list
void combat_GlobalAttribModAdd(SA_PARAM_NN_VALID AttribMod *pmod, int iPartitionIdx);

// Adds an Activation to the global Activation list
void combat_GlobalActivationAdd(SA_PARAM_NN_VALID PowerDef *pdef,
								SA_PRE_NN_ELEMS(3) SA_POST_NN_VALID Vec3 vecSource,
								int iPartitionIdx,
								EntityRef erTarget,
								SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
								SA_PARAM_OP_VALID CharacterClass *pclass,
								int iLevel,
								F32 fDelay);

// Runs a tick for global combat data
void combat_GlobalTick(F32 fRate);

// Cleanup necessary for global combat data when a partition goes away
void combat_GlobalPartitionUnload(int iPartitionIdx);

#endif
