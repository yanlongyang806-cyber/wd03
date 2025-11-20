#include "dynMovement.h"

#include "dynMovement_h_ast.h"

S32 dynMovementDirectionDisplayOrder(DynMovementDirection eDirection)
{
	switch (eDirection)
	{
		xcase eDynMovementDirection_ForwardLeft:	return 0;
		xcase eDynMovementDirection_Left:			return 1;
		xcase eDynMovementDirection_BackwardLeft:	return 2;
		xcase eDynMovementDirection_Backward:		return 3;
		xcase eDynMovementDirection_BackwardRight:	return 4;
		xcase eDynMovementDirection_Right:			return 5;
		xcase eDynMovementDirection_ForwardRight:	return 6;
		xcase eDynMovementDirection_Forward:		return 7;
		xcase eDynMovementDirection_Default:		return 8;
	}
	return -1;
}

#include "dynMovement_h_ast.c"