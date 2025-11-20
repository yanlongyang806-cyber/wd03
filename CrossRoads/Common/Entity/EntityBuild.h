/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#ifndef ENTITY_BUILD_H
#define ENTITY_BUILD_H
GCC_SYSTEM

#include "entEnums.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"
#include "referencesystem.h"

typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct CharacterClass CharacterClass;
typedef struct CritterFaction CritterFaction;
typedef struct Expression Expression;
typedef struct GameAccountDataExtract GameAccountDataExtract;

#define MAX_NAME_LEN_ENTITYBUILD 20

// Tracks which item belongs in which inventory slot in an EntityBuild
AUTO_STRUCT AST_CONTAINER;
typedef struct EntityBuildItem
{
	const InvBagIDs eBagID;		AST(PERSIST, SUBTABLE(InvBagIDsEnum))
		// The bag

	const int iSlot;			AST(PERSIST)
		// The slot in the bag

	const U64 ulItemID;			AST(PERSIST)
		// The item of the item (0 means empty)

} EntityBuildItem;

// Tracks the set of data that defines a build/preset/config for an entity
AUTO_STRUCT AST_CONTAINER;
typedef struct EntityBuild
{
	DirtyBit dirtyBit;								AST(SERVER_ONLY)

	const char achName[MAX_NAME_LEN_ENTITYBUILD];	AST(PERSIST USERFLAG(TOK_USEROPTIONBIT_1))
		// The name set by the player

	CONST_REF_TO(CharacterClass) hClass;			AST(PERSIST)
		// The Character's CharacterClass

	CONST_REF_TO(CritterFaction) hFaction;			AST(PERSIST)

	const U8 chCostumeType;							AST(PERSIST)
		// The Character's active costume type

	const U8 chCostume;								AST(PERSIST)
		// The Character's active costume, within the type

	CONST_EARRAY_OF(EntityBuildItem) ppItems;		AST(PERSIST)
		// The items equipped in the slots of certain bags

	U32 bSwappingOut : 1;							NO_AST
		// Flag set during the set transaction so the change to a new EntityBuild doesn't cause
		//  changes in the EntityBuild being replaced

} EntityBuild;

AUTO_STRUCT;
typedef struct EntityBuildSlotDef
{
	const char * pchBuildChangedPower;		AST(NAME(BuildChangedPower) POOL_STRING )
		// the power that runs once the build has changed to this slot

	Expression *pExprCanChangeToBuild;		AST(NAME(ExprBlockCanChangeToBuild,ExprCanChangeToBuildBlock), REDUNDANT_STRUCT(ExprCanChangeToBuild, parse_Expression_StructParam), LATEBIND)
		// expression that is run before changing the build, if returns false the build will not change
	
	U32 bIgnoreCostumeSwap;					AST(NAME(IgnoreCostumeSwap))

	const char * pchTransformationDef;		AST(NAME(TransformationDef))
} EntityBuildSlotDef;

AUTO_STRUCT;
typedef struct EntityBuildDef
{
	EntityBuildSlotDef	**eaBuildSlots;		AST(NAME(BuildSlot))
		// ordered list of build slots

} EntityBuildDef;


// EntityBuild functions

// Returns the entity build slot def by index
SA_RET_OP_VALID EntityBuildSlotDef* entity_BuildGetSlotDef(U32 index);

// Returns the entity's current build slot def
SA_RET_OP_VALID EntityBuildSlotDef* entity_BuildGetCurrentSlotDef(SA_PARAM_NN_VALID Entity *pent);

// Gets the Entity's current EntityBuild.  May return NULL if then Entity doesn't have a current EntityBuild.
SA_RET_OP_VALID EntityBuild *entity_BuildGetCurrent(SA_PARAM_NN_VALID Entity *pent);

// Gets the Entity's EntityBuild at the given index.  May return NULL if then Entity doesn't have an EntityBuild at that index.
SA_RET_OP_VALID EntityBuild *entity_BuildGet(SA_PARAM_NN_VALID Entity *pent, S32 iIndex);

// Returns the current index of the build.
// Returns -1 if no build current or on error
S32 entity_BuildGetCurrentIndex(SA_PARAM_NN_VALID Entity *pent);

// Returns true if the Entity is currently allowed to create a new EntityBuild
S32 entity_BuildCanCreate(SA_PARAM_NN_VALID NOCONST(Entity) *pent);

//Returns the maximum number of slots the entity can have
S32 entity_BuildMaxSlots(SA_PARAM_NN_VALID NOCONST(Entity) *pent);

// Returns true if the string is a legal name for an EntityBuild
S32 entity_BuildNameLegal(SA_PARAM_NN_STR const char *pchName);

// The amount of time a this entity needs to wait before setting their build
S32 entity_BuildTimeToWait(Entity *pEnt, U32 iBuild);

// true/false, can this entity set their current build? (in a transaction)
S32 entity_BuildCanSetTransacted(SA_PARAM_NN_VALID NOCONST(Entity) *pent);
// true/false, can this entity set their build (used outside of the transactions
// Same as the transacted version except this also checks the combat timer
S32 entity_BuildCanSet(Entity *pEnt, U32 iBuild);

// The amount of time a this entity needs to wait before setting their build
S32 entity_trh_BuildTimeToWaitOutOfCombat(ATH_ARG SA_PARAM_NN_VALID NOCONST(Entity) *pent);
#define entity_BuildTimeToWaitOutOfCombat(pEnt) entity_trh_BuildTimeToWaitOutOfCombat(CONTAINER_NOCONST(Entity, pEnt))

// Returns true if the Item id is used in any of the Entity's builds
S32 entity_BuildsUseItemID(SA_PARAM_NN_VALID Entity *pent, U64 ulItemID);

#endif //ENTITY_BUILD_H