/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "species_common.h"
#include "NameGen.h"
#include "PlayerCostume.h"
#include "ReferenceSystem.h"
#include "rand.h"
#include "TextFilter.h"

#include "AutoGen/Message_h_ast.h"
#include "AutoGen/NameGen_h_ast.h"


//Needs the following data
// NameTemplateList called "Common"
// UIColorSet called "GenSpeciesColorSet000", "GenSpeciesColorSet001", etc.

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct SpeciesGenRules
{
	NameTemplateList *firstNameRules, *lastNameRules;
	const char *speciesName;
	Gender eGender;
	int genderIndex;
	int numMales, numFemales;
	PCSkeletonDef *pMaleSkeleton;
	PCSkeletonDef *pFemaleSkeleton;
}SpeciesGenRules;

static void speciesgen_GenerateNameRules(SpeciesDef *newSpecies, SpeciesGenRules *rules)
{
}

static SpeciesDef *speciesgen_GenerateSingleSpecies(SpeciesGenRules *rules)
{
	char text[256], num[10];
	SpeciesDef *newSpecies = StructCreate(parse_SpeciesDef);

	if (!newSpecies) return NULL;

	// Assign species passed in values
	newSpecies->pcSpeciesName = StructAllocString(rules->speciesName);
	newSpecies->eGender = rules->eGender;

	// Internal species gender specific name
	*text = '\0';
	strcat(text, "Gen_");
	strcat(text, newSpecies->pcSpeciesName);
	if (rules->eGender == Gender_Male)
	{
		strcat(text, "_Male");
		if (rules->numMales > 1)
		{
			sprintf(num, "_%d", rules->genderIndex + 1);
			strcat(text, num);
		}
	}
	else
	{
		strcat(text, "_Female");
		if (rules->numFemales > 1)
		{
			sprintf(num, "_%d", rules->genderIndex + 1);
			strcat(text, num);
		}
	}
	newSpecies->pcName = StructAllocString(text);

	// Species display name
	newSpecies->displayNameMsg.pEditorCopy->bDoNotTranslate = true;
	newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(rules->speciesName);

	// Gender display name
	if (rules->eGender == Gender_Male)
	{
		if (rules->numMales > 1)
		{
			if (rules->genderIndex == 0)
			{
				newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Primary Male");
			}
			else if (rules->genderIndex == 1)
			{
				newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Secondary Male");
			}
			else
			{
				newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Tertiary Male");
			}
		}
		else
		{
			newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Male");
		}
	}
	else
	{
		if (rules->numFemales > 1)
		{
			if (rules->genderIndex == 0)
			{
				newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Primary Female");
			}
			else if (rules->genderIndex == 1)
			{
				newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Secondary Female");
			}
			else
			{
				newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Tertiary Female");
			}
		}
		else
		{
			newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Female");
		}
	}

	//Skeleton
	if (rules->eGender == Gender_Male)
	{
		SET_HANDLE_FROM_REFERENT("CostumeSkeleton", rules->pMaleSkeleton, newSpecies->hSkeleton);
	}
	else
	{
		SET_HANDLE_FROM_REFERENT("CostumeSkeleton", rules->pFemaleSkeleton, newSpecies->hSkeleton);
	}

	//Restriction
	newSpecies->eRestriction = kPCRestriction_NPC|kPCRestriction_Player|kPCRestriction_Player_Initial;

	//Choose nameing rules
	speciesgen_GenerateNameRules(newSpecies, rules);

	//Choose skin color rules
	

	//Choose defining geos and excluded geos
	

	//Choose body shape rules
	

	//Choose costume packages
	

	return newSpecies;
}

void speciesgen_GenerateSpecies(SpeciesDef ***peaSpeciesList, PCSkeletonDef *pMaleSkeleton, PCSkeletonDef *pFemaleSkeleton)
{
	const char *temp = NULL;
	SpeciesGenRules rules;
	NameTemplateList *speciesNameRules = NULL;
	NameTemplateListRef *item = NULL;
	NameTemplateListRef **refList = NULL;
	int numMales = 0, numFemales = 0;
	int rand, i;
	SpeciesDef *tempSpecies = NULL;
	int namecount = 0;

	eaClear(peaSpeciesList);

	//Set up rules
	rules.firstNameRules = NULL;
	rules.lastNameRules = NULL;
	rules.pMaleSkeleton = pMaleSkeleton;
	rules.pFemaleSkeleton = pFemaleSkeleton;

	//Create a name gen structure
	speciesNameRules = RefSystem_ReferentFromString("NameTemplateList", "Common");
	item = StructCreate(parse_NameTemplateListRef);
	if (!item) goto cleanup;
	SET_HANDLE_FROM_REFERENT("NameTemplateList", speciesNameRules, item->hNameTemplateList);
	eaPush(&refList, item);

	//Create a new species name
	do 
	{
		temp = namegen_GenerateName(refList);
		++namecount;
		if ((!temp) || (!*temp) || strlen(temp) < 3 || IsAnyProfane(temp) || IsAnyRestricted(temp) || IsDisallowed(temp)) temp = NULL;
	} while (namecount < 10 && !temp);
	eaDestroyStruct(&refList, parse_NameTemplateListRef);
	if (!temp) goto cleanup;
	rules.speciesName = StructAllocString(temp);
	if (!rules.speciesName) goto cleanup;

	//Determine number of genders
	if (randomIntRange(0, 99) < 5)
	{
		// Unusual gender count
		switch (randomIntRange(0, 2))
		{
		case 0:
			// One gender
			if (randomIntRange(0, 1))
			{
				numMales = 1;
			}
			else
			{
				numFemales = 1;
			}
			break;
		case 1:
			// Three genders
			if (randomIntRange(0, 1))
			{
				numMales = 1;
				numFemales = 2;
			}
			else
			{
				numMales = 2;
				numFemales = 1;
			}
			break;
		case 2:
			// Four genders
			rand = randomIntRange(0, 2);
			if (rand == 0)
			{
				numMales = 1;
				numFemales = 3;
			}
			else if (rand == 1)
			{
				numMales = 2;
				numFemales = 2;
			}
			else if (rand == 2)
			{
				numMales = 3;
				numFemales = 1;
			}
			break;
		}
	}
	else
	{
		numMales = 1;
		numFemales = 1;
	}

	rules.numMales = numMales;
	rules.numFemales = numFemales;

	rules.eGender = Gender_Male;
	for (i = 0; i < numMales; ++i)
	{
		rules.genderIndex = i;
		tempSpecies = speciesgen_GenerateSingleSpecies(&rules);
		if (tempSpecies) eaPush(peaSpeciesList, tempSpecies);
	}

	rules.eGender = Gender_Female;
	for (i = 0; i < numFemales; ++i)
	{
		rules.genderIndex = i;
		tempSpecies = speciesgen_GenerateSingleSpecies(&rules);
		if (tempSpecies) eaPush(peaSpeciesList, tempSpecies);
	}

	StructFreeString((char*)rules.speciesName);

cleanup:;
}