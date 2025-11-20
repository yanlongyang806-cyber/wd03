#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AttribCurve.h"	// For AttribCurve struct

// Hard cap
AUTO_STRUCT;
typedef struct AttribCurveMax
{
	AttribCurve parent;	AST(POLYCHILDTYPE(kAttribCurveType_Max))
		// Must be first.  Parent parameter struct.

	F32 fMax;
		// The actual max
} AttribCurveMax;

// Asymptotic cap
AUTO_STRUCT;
typedef struct AttribCurveQuadraticMax
{
	AttribCurve parent;	AST(POLYCHILDTYPE(kAttribCurveType_QuadraticMax))
		// Must be first.  Parent parameter struct.

	F32 fMax;
		// The actual max
} AttribCurveQuadraticMax;

// Actually applies the curve, and returns the value
F32 character_ApplyAttribCurve(SA_PARAM_NN_VALID Character *pchar,
							   SA_PARAM_NN_VALID AttribCurve *pcurve,
							   F32 fVal);

