/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AttribCurve.h"
#include "AttribCurveImp.h"

#include "Character.h"
#include "CharacterClass.h"

// A simple wrapper around a call to class_GetAttribCurve and character_ApplyAttribCurve()
F32 character_AttribCurve(Character *pchar,
						  AttribType offAttrib,
						  AttribAspect offAspect,
						  F32 fValue,
						  S32 *pbCurvedOut)
{
	F32 fResult = fValue;
	CharacterClass *pclass = character_GetClassCurrent(pchar);
	if(pclass)
	{
		AttribCurve *pcurve = class_GetAttribCurve(pclass,offAttrib,offAspect);
		if(pcurve)
		{
			fResult = character_ApplyAttribCurve(pchar,pcurve,fValue);
			if(pbCurvedOut)
				*pbCurvedOut = true;
		}
	}
	return fResult;
}

//#include "AutoGen/AttribCurve_h_ast.c"