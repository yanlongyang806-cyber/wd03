#pragma once
GCC_SYSTEM

AUTO_ENUM;
typedef enum DynMovementNumDirections
{
	eDynMovementNumDirections_None = -1,
	eDynMovementNumDirections_0 = 0,
	eDynMovementNumDirections_1 = 1,
	eDynMovementNumDirections_2 = 2,
	eDynMovementNumDirections_4 = 4,
	eDynMovementNumDirections_8 = 8,
}
DynMovementNumDirections;
extern StaticDefineInt DynMovementNumDirectionsEnum[];

AUTO_ENUM;
typedef enum DynMovementDirection
{
	// note : these have been ordered based on the number of directions, such that
	eDynMovementDirection_Default,			// 0 directions
	eDynMovementDirection_Forward,			// 1 direction  for this and below
	eDynMovementDirection_Backward,			// 2 directions for this and below
	eDynMovementDirection_Left,
	eDynMovementDirection_Right,			// 4 directions for this and below
	eDynMovementDirection_ForwardLeft,
	eDynMovementDirection_BackwardLeft,
	eDynMovementDirection_ForwardRight,
	eDynMovementDirection_BackwardRight,	// 8 directions for this and below
}
DynMovementDirection;
extern StaticDefineInt DynMovementDirectionEnum[];

#define DYNMOVEMENT_NUMDIRECTIONS (eDynMovementDirection_BackwardRight+1) // not listed as last element of the enum so it won't show up in editors

S32 dynMovementDirectionDisplayOrder(DynMovementDirection eDirection);