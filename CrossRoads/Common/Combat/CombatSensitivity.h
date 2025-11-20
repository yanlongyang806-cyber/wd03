#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"

#include "PowersEnums.h" // The SensitivityType enum is here

AUTO_STRUCT;
typedef struct SensitivityMod
{
	char *pchName;				AST(KEY STRUCTPARAM POOL_STRING)
		// Internal name of this particular modification.  Used to key the enum.

	SensitivityType eType;		AST(DEFAULT(-1), SUBTABLE(SensitivityTypeEnum))
		// The particular type of sensitivity being adjusted.  Defaulted to -1 so it must show up in text.

	F32 fValue;					AST(DEFAULT(1.0f))
		// How sensitive.  Defaulted to 1, which is generally considered 'normal' sensitivity, so 0 shows up in text.

	SensitivityType eAltType;	AST(DEFAULT(-1), SUBTABLE(SensitivityTypeEnum))
		// A second sensitivity type, so that a sensitivity mod can contain multiple values. I doubt we will ever
		// need more than this second one, but if we ever do look into remaking SensitivityMods into being an array
		// of types and values.

	F32 fAltValue;				AST(DEFAULT(1.0f))
		// Second sensitivity value

	const char *cpchFile;		AST(NAME(File), CURRENTFILE)
}SensitivityMod;

// Array of the SensitivityMods, loaded and indexed directly
AUTO_STRUCT;
typedef struct SensitivityMods
{
	SensitivityMod **ppSensitivities;	AST(NAME(Sensitivity))
} SensitivityMods;

// Globally accessibly SensitivityMods structure
extern SensitivityMods g_SensitivityMods;

extern StaticDefineInt SensitivityModsEnum[];

// Performs a general validation of a set of Sensitivities.  Does not do any detailed validation, such
//  as is this type of Sensitivity appropriate for the data it's associated with.  Returns true for valid sets.
int sensitivity_ValidateSet(SA_PARAM_OP_VALID S32 *piSensitivities, SA_PARAM_NN_STR const char *pchFile);
