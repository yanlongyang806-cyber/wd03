#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterAttribsMinimal.h" // For enums

// Forward declarations
typedef struct StaticDefineInt StaticDefineInt;
typedef struct Character		Character;
extern StaticDefineInt AttribAspectEnum[];
extern StaticDefineInt AttribTypeEnum[];
extern StaticDefineInt AttribCurveTypeEnum[];


AUTO_ENUM;
typedef enum AttribCurveType
{
	kAttribCurveType_Max = 1,
		// Hard max

	kAttribCurveType_QuadraticMax,
		// Asymptotic max

} AttribCurveType;

// Provides a general framework for clamps, caps, diminishing returns
//  equations and so forth being applied to accumulated attributes on a character.


// This is the parent structure for AttribCurve.  Actual implementations
//  are polymorphic children of this structure.
AUTO_STRUCT;
typedef struct AttribCurve
{
	AttribCurveType eType;			AST(SUBTABLE(AttribCurveTypeEnum), POLYPARENTTYPE)
		// What kind of diminishing returns is being applied

	AttribType offAttrib;			AST(SUBTABLE(AttribTypeEnum), NAME(Attrib))
		// What attribute are we diminishing

	AttribAspect offAspect;			AST(SUBTABLE(AttribAspectEnum), NAME(Aspect))
		// What aspect of the attribute are we diminishing
} AttribCurve;


// A simple wrapper around a call to class_GetAttribCurve and character_ApplyAttribCurve()
F32 character_AttribCurve(SA_PARAM_NN_VALID Character *pchar,
						  AttribType offAttrib,
						  AttribAspect offAspect,
						  F32 fValue,
						  SA_PARAM_OP_VALID S32 *pbCurvedOut);

