/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatSensitivity.h"
#include "AutoGen/CombatSensitivity_h_ast.h"

#include "error.h"
#include "estring.h"
#include "file.h"
#include "ResourceManager.h"

#include "AttribMod.h" 

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DefineContext *s_pDefineSensitivityMods = NULL;

SensitivityMods g_SensitivityMods = {0};

// StaticDefineInt into which all the SensitivityMods are dynamically loaded
StaticDefineInt SensitivityModsEnum[] =
{
	DEFINE_INT
	DEFINE_EMBEDDYNAMIC_INT(s_pDefineSensitivityMods)
	DEFINE_END
};


// Performs a general validation of a set of sensitivities.  Does not do any detailed validation, such
//  as is this type of sensitivity appropriate for the structure it is on.  Returns true for valid sets.
int sensitivity_ValidateSet(S32 *piSensitivities, const char *pchFile)
{
	int bRtn = true;
	int i,s=eaiSize(&piSensitivities);
	int *iTypes = NULL;

	for(i=0;i<s;i++)
	{
		SensitivityMod *pMod = g_SensitivityMods.ppSensitivities[eaiGet(&piSensitivities,i)];
		
		if(eaiFind(&iTypes,pMod->eType) == -1)
		{
			eaiPush(&iTypes,pMod->eType);
		}
		else
		{
			ErrorFilenamef(pchFile,"Two or more SensitivityMods referencing same SensitivityType (%s)",StaticDefineIntRevLookup(SensitivityTypeEnum,pMod->eType));
			bRtn = false;
		}

		if (pMod->eAltType != -1)
		{
			if(eaiFind(&iTypes,pMod->eAltType) == -1)
			{
				eaiPush(&iTypes,pMod->eAltType);
			}
			else
			{
				ErrorFilenamef(pchFile,"Two or more SensitivityMods referencing same SensitivityType (%s)",StaticDefineIntRevLookup(SensitivityTypeEnum,pMod->eAltType));
				bRtn = false;
			}
		}
	}

	eaiDestroy(&iTypes);

	return bRtn;
}

static int SensitivityModTypeValueValidate(SensitivityMod *pMod, SensitivityType eType, F32 fValue)
{
	int bRtn = true;
	if(fValue == 1.0f)
	{
		ErrorFilenamef(pMod->cpchFile,"%s: Invalid value for %s Sensitivity %f (must not be 1.0)",pMod->pchName,StaticDefineIntRevLookup(SensitivityTypeEnum,eType),fValue);
		bRtn = false;
	}

	switch(eType)
	{
	case kSensitivityType_AttribCurve:
	case kSensitivityType_Shield:
	case kSensitivityType_Immune:
		if(fValue<0 || fValue>1.f)
		{
			ErrorFilenamef(pMod->cpchFile,"%s: Invalid value for %s Sensitivity %f (must be [0 .. 1))",pMod->pchName,StaticDefineIntRevLookup(SensitivityTypeEnum,eType),fValue);
			bRtn = false;
		}
		break;
	case kSensitivityType_Resistance:
	case kSensitivityType_Avoidance:
	case kSensitivityType_Strength:
	case kSensitivityType_CombatMod:
		if(fValue<0.0)
		{
			ErrorFilenamef(pMod->cpchFile,"%s: Invalid value for %s Sensitivity %f (must be >= 0.0)\n",pMod->pchName,StaticDefineIntRevLookup(SensitivityTypeEnum,eType),fValue);
			bRtn = false;
		}
		if(fValue>2.0f)
		{
			ErrorFilenamef(pMod->cpchFile,"%s: Unexpectedly high %s Sensitivity %f\n",pMod->pchName,StaticDefineIntRevLookup(SensitivityTypeEnum,eType),fValue);
			bRtn = false;
		}
		break;
	}

	return bRtn;	
}

static int SensitivityModValidate(SensitivityMod *pMod)
{
	int bRtn = true, bAltRtn = true;

	bRtn = SensitivityModTypeValueValidate(pMod, pMod->eType, pMod->fValue);

	if (pMod->eAltType != -1)
		bAltRtn = SensitivityModTypeValueValidate(pMod, pMod->eAltType, pMod->fAltValue);

	return bRtn && bAltRtn;
}

static void SensitivityModValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	SensitivityMod *pmod = pResource;
	switch (eType)
	{	
		xcase RESVALIDATE_POST_TEXT_READING:
			SensitivityModValidate(pmod);
	}
}

//Loads all the Sensitivity tables into the dictionary
AUTO_STARTUP(SensitivityMods);
void SensitivityModsLoad(void)
{
	int i,s;
	char *pchTemp = NULL;

	estrStackCreateSize(&pchTemp,20);

	loadstart_printf("Loading SensitivityMods...");
	ParserLoadFiles(NULL, "defs/config/SensitivityMods.def", "SensitivityMods.bin", PARSER_OPTIONALFLAG, parse_SensitivityMods, &g_SensitivityMods);
	s_pDefineSensitivityMods = DefineCreate();
	s = eaSize(&g_SensitivityMods.ppSensitivities);
	for(i=0; i<s; i++)
	{
		estrPrintf(&pchTemp,"%d", i);	// This must be the index, they are indexed directly
		DefineAdd(s_pDefineSensitivityMods,g_SensitivityMods.ppSensitivities[i]->pchName,pchTemp);
	}
	loadend_printf(" done (%d SensitivityMods).", s);

	estrDestroy(&pchTemp);

}

#include "AutoGen/CombatSensitivity_h_ast.c"