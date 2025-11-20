/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "species_common.h"
#include "NameGen.h"
#include "CostumeCommon.h"
#include "CostumeCommonTailor.h"
#include "ReferenceSystem.h"
#include "ResourceInfo.h"
#include "ResourceManager.h"
#include "rand.h"
#include "TextFilter.h"
#include "StringCache.h"
#include "UIColor.h"
#include "entCritter.h"
#include "textparserinheritance.h"
#include "ChoiceTable_common.h"
#include "fileutil2.h"

#include "AutoGen/species_common_h_ast.h"
#include "AutoGen/speciesGen_c_ast.h"
#include "AutoGen/Message_h_ast.h"
#include "AutoGen/NameGen_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/textparserinheritance_h_ast.h"

extern ParseTable parse_PCStanceInfo[];
#define TYPE_parse_PCStanceInfo PCStanceInfo

//Needs the following data
// NameTemplateList called "Common"
// UIColorSet called "GenSpeciesColorSet000", "GenSpeciesColorSet001", etc.

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_STRUCT;
typedef struct SpeciesUsedFeatures
{
	PCBoneDef *Bone;
	PCGeometryDef *Geo;
	PCMaterialDef *Mat;
	PCTextureDef *tex_pattern;
	PCTextureDef *tex_detail;
	PCTextureDef *tex_specular;
	PCTextureDef *tex_diffuse;
	PCTextureDef *tex_movable;
}SpeciesUsedFeatures;

typedef struct SpeciesGenRules
{
	SpeciesGenData *pSpeciesGenData;

	const char *speciesName;
	Gender eGender;
	int genderIndex;
	int numMales, numFemales;
	PCSkeletonDef *pMaleSkeleton;
	PCSkeletonDef *pFemaleSkeleton;
	SpeciesDefiningFeature *pSpeciesFeatureDefMale;
	SpeciesDefiningFeature *pSpeciesFeatureDefFemale;
	SpeciesDefiningFeature *pSpeciesFeatureAllM[5];
	SpeciesDefiningFeature *pSpeciesFeatureAllF[5];
	int iSpeciesFeatureAllM[5];
	int iSpeciesFeatureAllF[5];
	int iNumSpeciesFeaturesAll;
	SpeciesDefiningFeature *pSpeciesFeatureMale;
	int iSpeciesFeatureMale;
	SpeciesDefiningFeature *pSpeciesFeatureFemale;
	int iSpeciesFeatureFemale;
	bool bSpeciesFeatureEachDifferent;

	int iNumDefiningFeatures;
	UIColorSet *pCommonColor;
	UIColorSet *pCommonMaleColor;
	UIColorSet *pCommonFemaleColor;
	int iUniformSet;

	bool bDiffPhonemeByGender;
	bool bDiffPhonemeByAll;
	bool bMaleHasLastName;
	bool bFemaleHasLastName;
	bool bSize3FirstNamesMale;
	bool bSize4FirstNamesMale;
	bool bSize5FirstNamesMale;
	bool bSize6FirstNamesMale;
	bool bSize3FirstNamesFemale;
	bool bSize4FirstNamesFemale;
	bool bSize5FirstNamesFemale;
	bool bSize6FirstNamesFemale;
	bool bSize3LastNamesMale;
	bool bSize4LastNamesMale;
	bool bSize5LastNamesMale;
	bool bSize6LastNamesMale;
	bool bSize3LastNamesFemale;
	bool bSize4LastNamesFemale;
	bool bSize5LastNamesFemale;
	bool bSize6LastNamesFemale;
	bool bHasApostropheFirstNamesMale;
	bool bHasApostropheLastNamesMale;
	bool bHasApostropheFirstNamesFemale;
	bool bHasApostropheLastNamesFemale;
	bool bHasDashFirstNamesMale;
	bool bHasDashLastNamesMale;
	bool bHasDashFirstNamesFemale;
	bool bHasDashLastNamesFemale;
	bool bAllApostropheFirstNamesMale;
	bool bAllApostropheLastNamesMale;
	bool bAllApostropheFirstNamesFemale;
	bool bAllApostropheLastNamesFemale;
	bool bAllDashFirstNamesMale;
	bool bAllDashLastNamesMale;
	bool bAllDashFirstNamesFemale;
	bool bAllDashLastNamesFemale;
	bool bPosMarkFirstNamesMale[3];
	bool bPosMarkLastNamesMale[3];
	bool bPosMarkFirstNamesFemale[3];
	bool bPosMarkLastNamesFemale[3];
	bool bDashIsASpace;

	NOCONST(PhonemeSet)* c1Many1ConsLastNamesMale[3];
	NOCONST(PhonemeSet)* c1Many2ConsLastNamesMale[3];
	NOCONST(PhonemeSet)* c1Many3ConsLastNamesMale[3];
	bool b1AllConsLastNamesMale;
	NOCONST(PhonemeSet)* c1Many1VowelLastNamesMale[3];
	NOCONST(PhonemeSet)* c1Many2VowelLastNamesMale[3];
	NOCONST(PhonemeSet)* c1Many3VowelLastNamesMale[3];
	bool b1AllVowelLastNamesMale;
	NOCONST(PhonemeSet)* c2Many1ConsLastNamesMale[3];
	NOCONST(PhonemeSet)* c2Many2ConsLastNamesMale[3];
	NOCONST(PhonemeSet)* c2Many3ConsLastNamesMale[3];
	bool b2AllConsLastNamesMale;
	NOCONST(PhonemeSet)* c2Many1VowelLastNamesMale[3];
	NOCONST(PhonemeSet)* c2Many2VowelLastNamesMale[3];
	NOCONST(PhonemeSet)* c2Many3VowelLastNamesMale[3];
	bool b2AllVowelLastNamesMale;
	NOCONST(PhonemeSet)* c3Many1ConsLastNamesMale[3];
	NOCONST(PhonemeSet)* c3Many2ConsLastNamesMale[3];
	NOCONST(PhonemeSet)* c3Many3ConsLastNamesMale[3];
	bool b3AllConsLastNamesMale;
	NOCONST(PhonemeSet)* c3Many1VowelLastNamesMale[3];
	NOCONST(PhonemeSet)* c3Many2VowelLastNamesMale[3];
	NOCONST(PhonemeSet)* c3Many3VowelLastNamesMale[3];
	bool b3AllVowelLastNamesMale;
	NOCONST(PhonemeSet)* c1Many1ConsFirstNamesMale[3];
	NOCONST(PhonemeSet)* c1Many2ConsFirstNamesMale[3];
	NOCONST(PhonemeSet)* c1Many3ConsFirstNamesMale[3];
	bool b1AllConsFirstNamesMale;
	NOCONST(PhonemeSet)* c1Many1VowelFirstNamesMale[3];
	NOCONST(PhonemeSet)* c1Many2VowelFirstNamesMale[3];
	NOCONST(PhonemeSet)* c1Many3VowelFirstNamesMale[3];
	bool b1AllVowelFirstNamesMale;
	NOCONST(PhonemeSet)* c2Many1ConsFirstNamesMale[3];
	NOCONST(PhonemeSet)* c2Many2ConsFirstNamesMale[3];
	NOCONST(PhonemeSet)* c2Many3ConsFirstNamesMale[3];
	bool b2AllConsFirstNamesMale;
	NOCONST(PhonemeSet)* c2Many1VowelFirstNamesMale[3];
	NOCONST(PhonemeSet)* c2Many2VowelFirstNamesMale[3];
	NOCONST(PhonemeSet)* c2Many3VowelFirstNamesMale[3];
	bool b2AllVowelFirstNamesMale;
	NOCONST(PhonemeSet)* c3Many1ConsFirstNamesMale[3];
	NOCONST(PhonemeSet)* c3Many2ConsFirstNamesMale[3];
	NOCONST(PhonemeSet)* c3Many3ConsFirstNamesMale[3];
	bool b3AllConsFirstNamesMale;
	NOCONST(PhonemeSet)* c3Many1VowelFirstNamesMale[3];
	NOCONST(PhonemeSet)* c3Many2VowelFirstNamesMale[3];
	NOCONST(PhonemeSet)* c3Many3VowelFirstNamesMale[3];
	bool b3AllVowelFirstNamesMale;

	NOCONST(PhonemeSet)* c1Many1ConsLastNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many2ConsLastNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many3ConsLastNamesFemale[3];
	bool b1AllConsLastNamesFemale;
	NOCONST(PhonemeSet)* c1Many1VowelLastNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many2VowelLastNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many3VowelLastNamesFemale[3];
	bool b1AllVowelLastNamesFemale;
	NOCONST(PhonemeSet)* c2Many1ConsLastNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many2ConsLastNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many3ConsLastNamesFemale[3];
	bool b2AllConsLastNamesFemale;
	NOCONST(PhonemeSet)* c2Many1VowelLastNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many2VowelLastNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many3VowelLastNamesFemale[3];
	bool b2AllVowelLastNamesFemale;
	NOCONST(PhonemeSet)* c3Many1ConsLastNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many2ConsLastNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many3ConsLastNamesFemale[3];
	bool b3AllConsLastNamesFemale;
	NOCONST(PhonemeSet)* c3Many1VowelLastNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many2VowelLastNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many3VowelLastNamesFemale[3];
	bool b3AllVowelLastNamesFemale;
	NOCONST(PhonemeSet)* c1Many1ConsFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many2ConsFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many3ConsFirstNamesFemale[3];
	bool b1AllConsFirstNamesFemale;
	NOCONST(PhonemeSet)* c1Many1VowelFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many2VowelFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c1Many3VowelFirstNamesFemale[3];
	bool b1AllVowelFirstNamesFemale;
	NOCONST(PhonemeSet)* c2Many1ConsFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many2ConsFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many3ConsFirstNamesFemale[3];
	bool b2AllConsFirstNamesFemale;
	NOCONST(PhonemeSet)* c2Many1VowelFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many2VowelFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c2Many3VowelFirstNamesFemale[3];
	bool b2AllVowelFirstNamesFemale;
	NOCONST(PhonemeSet)* c3Many1ConsFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many2ConsFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many3ConsFirstNamesFemale[3];
	bool b3AllConsFirstNamesFemale;
	NOCONST(PhonemeSet)* c3Many1VowelFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many2VowelFirstNamesFemale[3];
	NOCONST(PhonemeSet)* c3Many3VowelFirstNamesFemale[3];
	bool b3AllVowelFirstNamesFemale;

	NOCONST(PhonemeSet) ***peaPhonemeVowels;
	NOCONST(PhonemeSet) ***peaPhonemeCons;
	NOCONST(PhonemeSet) **eaGender1;
	NOCONST(PhonemeSet) **eaGender2;
	NOCONST(PhonemeSet) **eaGender3;
	NOCONST(PhonemeSet) **eaGender4;
	NOCONST(PhonemeSet) **eaGenderOwner;
	NameTemplateListNoRef *firstNameRules1, *lastNameRules1;
	NameTemplateListNoRef *firstNameRules2, *lastNameRules2;
	NameTemplateListNoRef *firstNameRules3, *lastNameRules3;
	NameTemplateListNoRef *firstNameRules4, *lastNameRules4;
}SpeciesGenRules;

typedef struct PhonemeConsList
{
	const char *name;
	const char *phonemes;
	int removeCount;
}PhonemeConsList;

static PhonemeConsList gPhonemeConsList[11] =
{
	{"_Stop", "b,d,g,k,p,t", 2},
	{"_Fricative", "ch,f,ph,s,sh,th,v,z", 4},
	{"_Affricate", "ch,dg,j", 1},
	{"_Nasal", "m,n,ng", 1},
	{"_Liquid", "l,r", 0},
	{"_Glide", "w,y", 0},
	{"_Alveolar", "d,l,n,r,s,t", 2},
	{"_Bilabial", "b,m,p", 1},
	{"_Glottal", "h", 0},
	{"_Palatal", "ch,dg,s,j,sh,y", 2},
	{"_Velar", "g,k,ng,w", 1}
};

static NOCONST(PhonemeSet) **geaFirstVowels = NULL;
static NOCONST(PhonemeSet) **geaVowels = NULL;
static NOCONST(PhonemeSet) **geaCons = NULL;

static UIColorSet *speciesgen_GetRandomColorSet(void)
{
	int count, i, iRand;
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("CostumeColors");
	UIColorSet **pColorSets = (UIColorSet**)pStruct->ppReferents;
	count = 0;
	for (i = eaSize(&pColorSets)-1; i >= 0; --i)
	{
		if (!strnicmp("GenSpeciesColorSet",pColorSets[i]->pcName,18))
		{
			++count;
		}
	}
	iRand = randomIntRange(0, count-1);
	for (i = eaSize(&pColorSets)-1; i >= 0; --i)
	{
		if (!strnicmp("GenSpeciesColorSet",pColorSets[i]->pcName,18))
		{
			if (iRand-- <= 0)
			{
				return pColorSets[i];
			}
		}
	}
	return NULL;
}

static NOCONST(PhonemeSet) *speciesgen_CreatePhonemeSet(const char *name, const char *speciesname, const char *phonemes, int removeCount, int notThisMany)
{
	char text[256];
	int i, count, iRand;
	NOCONST(PhonemeSet) *tempset = NULL;
	char *pchTempList = NULL;
	char *pchContext = NULL;
	char *pchToken = NULL;

	tempset = StructCreateNoConst(parse_PhonemeSet);

	*text = '\0';
	strcat(text, speciesname);
	strcat(text, name);
	tempset->pcName = allocAddString(text);
	tempset->bIsNotInDict = true;

	strdup_alloca(pchTempList, phonemes);
	if ((pchToken = strtok_r(pchTempList, ",", &pchContext)))
	{
		do
		{
			eaPush(&tempset->pcPhonemes, StructAllocString(pchToken));
		}
		while ((pchToken = strtok_r(NULL, ",", &pchContext)));
	}

	if (removeCount > 0)
	{
		do
		{
			count = randomIntRange(0, removeCount);
		} while ((eaSize(&tempset->pcPhonemes) - count) == notThisMany);
	}
	else
	{
		count = 0;
	}
	for (i = 0; i < count; ++i)
	{
		iRand = randomIntRange(0, eaSize(&tempset->pcPhonemes)-1);
		if (tempset->pcPhonemes && tempset->pcPhonemes[iRand]) StructFreeString((char*)tempset->pcPhonemes[iRand]);
		eaRemove(&tempset->pcPhonemes, iRand);
	}

	//Duplicate some letters to make them more common
	if (randomIntRange(0, 1) && eaSize(&tempset->pcPhonemes) > 1)
	{
		count = randomIntRange(1, 5);
		for (i = 0; i < count; ++i)
		{
			iRand = randomIntRange(0, eaSize(&tempset->pcPhonemes)-1);
			eaPush(&tempset->pcPhonemes, StructAllocString(tempset->pcPhonemes[iRand]));
		}
	}

	return tempset;
}

static int speciesgen_GetConsTypeByName(const char *name)
{
	int len = (int)strlen(name);

	if (len >= 5 && !stricmp(name + len - 5, "_Stop"))
	{
		return 0;
	}
	if (len >= 10 && !stricmp(name + len - 10, "_Fricative"))
	{
		return 1;
	}
	if (len >= 10 && !stricmp(name + len - 10, "_Affricate"))
	{
		return 2;
	}
	if (len >= 6 && !stricmp(name + len - 6, "_Nasal"))
	{
		return 3;
	}
	if (len >= 7 && !stricmp(name + len - 7, "_Liquid"))
	{
		return 4;
	}
	if (len >= 6 && !stricmp(name + len - 6, "_Glide"))
	{
		return 5;
	}
	if (len >= 9 && !stricmp(name + len - 9, "_Alveolar"))
	{
		return 6;
	}
	if (len >= 9 && !stricmp(name + len - 9, "_Bilabial"))
	{
		return 7;
	}
	if (len >= 8 && !stricmp(name + len - 8, "_Glottal"))
	{
		return 8;
	}
	if (len >= 8 && !stricmp(name + len - 8, "_Palatal"))
	{
		return 9;
	}
	if (len >= 6 && !stricmp(name + len - 6, "_Velar"))
	{
		return 10;
	}

	return -1;
}

static void speciesgen_GenerateNameRulesToggles(SpeciesGenRules *rules)
{
	int i;
	NOCONST(PhonemeSet) *ps = NULL;

	rules->bDiffPhonemeByGender = randomIntRange(0, 99) < 90;
	rules->bDiffPhonemeByAll = randomIntRange(0, 99) < 90;

	if (randomIntRange(0, 99) < 90)
	{
		rules->bMaleHasLastName = false;
		rules->bFemaleHasLastName = false;
	}
	else if (randomIntRange(0, 99) < 90)
	{
		rules->bMaleHasLastName = true;
		rules->bFemaleHasLastName = true;
	}
	else if (randomIntRange(0, 1))
	{
		rules->bMaleHasLastName = false;
		rules->bFemaleHasLastName = true;
	}
	else
	{
		rules->bMaleHasLastName = true;
		rules->bFemaleHasLastName = false;
	}

	rules->bHasApostropheFirstNamesMale = false;
	rules->bHasApostropheLastNamesMale = false;
	rules->bHasApostropheFirstNamesFemale = false;
	rules->bHasApostropheLastNamesFemale = false;
	rules->bAllApostropheFirstNamesMale = false;
	rules->bAllApostropheLastNamesMale = false;
	rules->bAllApostropheFirstNamesFemale = false;
	rules->bAllApostropheLastNamesFemale = false;

	rules->bHasDashFirstNamesMale = false;
	rules->bHasDashLastNamesMale = false;
	rules->bHasDashFirstNamesFemale = false;
	rules->bHasDashLastNamesFemale = false;
	rules->bAllDashFirstNamesMale = false;
	rules->bAllDashLastNamesMale = false;
	rules->bAllDashFirstNamesFemale = false;
	rules->bAllDashLastNamesFemale = false;

	if (randomIntRange(0, 99) < 5)
	{
		rules->bDashIsASpace = randomIntRange(0, 1) == 1;
		do
		{
			rules->bPosMarkFirstNamesMale[0] = randomIntRange(0, 1) == 1;
			rules->bPosMarkFirstNamesMale[1] = randomIntRange(0, 1) == 1;
			rules->bPosMarkFirstNamesMale[2] = randomIntRange(0, 1) == 1;
		} while (!(rules->bPosMarkFirstNamesMale[0] || rules->bPosMarkFirstNamesMale[1] || rules->bPosMarkFirstNamesMale[2]));
		do
		{
			rules->bPosMarkLastNamesMale[0] = randomIntRange(0, 1) == 1;
			rules->bPosMarkLastNamesMale[1] = randomIntRange(0, 1) == 1;
			rules->bPosMarkLastNamesMale[2] = randomIntRange(0, 1) == 1;
		} while (!(rules->bPosMarkLastNamesMale[0] || rules->bPosMarkLastNamesMale[1] || rules->bPosMarkLastNamesMale[2]));
		do
		{
			rules->bPosMarkFirstNamesFemale[0] = randomIntRange(0, 1) == 1;
			rules->bPosMarkFirstNamesFemale[1] = randomIntRange(0, 1) == 1;
			rules->bPosMarkFirstNamesFemale[2] = randomIntRange(0, 1) == 1;
		} while (!(rules->bPosMarkFirstNamesFemale[0] || rules->bPosMarkFirstNamesFemale[1] || rules->bPosMarkFirstNamesFemale[2]));
		do
		{
			rules->bPosMarkLastNamesFemale[0] = randomIntRange(0, 1) == 1;
			rules->bPosMarkLastNamesFemale[1] = randomIntRange(0, 1) == 1;
			rules->bPosMarkLastNamesFemale[2] = randomIntRange(0, 1) == 1;
		} while (!(rules->bPosMarkLastNamesFemale[0] || rules->bPosMarkLastNamesFemale[1] || rules->bPosMarkLastNamesFemale[2]));

		if (randomIntRange(0, 1))
		{
			//Apostrophe
			if (randomIntRange(0, 99) >= 5)
			{
				rules->bHasApostropheFirstNamesMale = true;
				rules->bHasApostropheLastNamesMale = true;
				rules->bHasApostropheFirstNamesFemale = true;
				rules->bHasApostropheLastNamesFemale = true;
				if (randomIntRange(0, 99) < 5)
				{
					rules->bAllApostropheFirstNamesMale = true;
					rules->bAllApostropheLastNamesMale = true;
					rules->bAllApostropheFirstNamesFemale = true;
					rules->bAllApostropheLastNamesFemale = true;
				}
			}
			else
			{
				int lastFirstDiff = randomIntRange(0, 1);
				int maleFemaleDiff = randomIntRange(0, 1);
				if (lastFirstDiff && maleFemaleDiff)
				{
					rules->bHasApostropheFirstNamesMale = randomIntRange(0, 1) == 1;
					rules->bHasApostropheLastNamesMale = randomIntRange(0, 1) == 1;
					rules->bHasApostropheFirstNamesFemale = randomIntRange(0, 1) == 1;
					rules->bHasApostropheLastNamesFemale = randomIntRange(0, 1) == 1;
					if (randomIntRange(0, 99) < 5)
					{
						rules->bAllApostropheFirstNamesMale = randomIntRange(0, 1) == 1;
						rules->bAllApostropheLastNamesMale = randomIntRange(0, 1) == 1;
						rules->bAllApostropheFirstNamesFemale = randomIntRange(0, 1) == 1;
						rules->bAllApostropheLastNamesFemale = randomIntRange(0, 1) == 1;
					}
				}
				else if (maleFemaleDiff)
				{
					if (randomIntRange(0, 1))
					{
						rules->bHasApostropheFirstNamesMale = true;
						rules->bHasApostropheLastNamesMale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllApostropheFirstNamesMale = true;
							rules->bAllApostropheLastNamesMale = true;
						}
					}
					if (randomIntRange(0, 1))
					{
						rules->bHasApostropheFirstNamesFemale = true;
						rules->bHasApostropheLastNamesFemale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllApostropheFirstNamesFemale = true;
							rules->bAllApostropheLastNamesFemale = true;
						}
					}
				}
				else if (lastFirstDiff)
				{
					if (randomIntRange(0, 1))
					{
						rules->bHasApostropheFirstNamesMale = true;
						rules->bHasApostropheFirstNamesFemale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllApostropheFirstNamesMale = true;
							rules->bAllApostropheFirstNamesFemale = true;
						}
					}
					if (randomIntRange(0, 1))
					{
						rules->bHasApostropheLastNamesMale = true;
						rules->bHasApostropheLastNamesFemale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllApostropheLastNamesMale = true;
							rules->bAllApostropheLastNamesFemale = true;
						}
					}
				}
			}
		}
		else
		{
			//Dash
			if (randomIntRange(0, 99) >= 5)
			{
				rules->bHasDashFirstNamesMale = true;
				rules->bHasDashLastNamesMale = true;
				rules->bHasDashFirstNamesFemale = true;
				rules->bHasDashLastNamesFemale = true;
				if (randomIntRange(0, 99) < 5)
				{
					rules->bAllDashFirstNamesMale = true;
					rules->bAllDashLastNamesMale = true;
					rules->bAllDashFirstNamesFemale = true;
					rules->bAllDashLastNamesFemale = true;
				}
			}
			else
			{
				int lastFirstDiff = randomIntRange(0, 1);
				int maleFemaleDiff = randomIntRange(0, 1);
				if (lastFirstDiff && maleFemaleDiff)
				{
					rules->bHasDashFirstNamesMale = randomIntRange(0, 1) == 1;
					rules->bHasDashLastNamesMale = randomIntRange(0, 1) == 1;
					rules->bHasDashFirstNamesFemale = randomIntRange(0, 1) == 1;
					rules->bHasDashLastNamesFemale = randomIntRange(0, 1) == 1;
					if (randomIntRange(0, 99) < 5)
					{
						rules->bAllDashFirstNamesMale = randomIntRange(0, 1) == 1;
						rules->bAllDashLastNamesMale = randomIntRange(0, 1) == 1;
						rules->bAllDashFirstNamesFemale = randomIntRange(0, 1) == 1;
						rules->bAllDashLastNamesFemale = randomIntRange(0, 1) == 1;
					}
				}
				else if (maleFemaleDiff)
				{
					if (randomIntRange(0, 1))
					{
						rules->bHasDashFirstNamesMale = true;
						rules->bHasDashLastNamesMale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllDashFirstNamesMale = true;
							rules->bAllDashLastNamesMale = true;
						}
					}
					if (randomIntRange(0, 1))
					{
						rules->bHasDashFirstNamesFemale = true;
						rules->bHasDashLastNamesFemale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllDashFirstNamesFemale = true;
							rules->bAllDashLastNamesFemale = true;
						}
					}
				}
				else if (lastFirstDiff)
				{
					if (randomIntRange(0, 1))
					{
						rules->bHasDashFirstNamesMale = true;
						rules->bHasDashFirstNamesFemale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllDashFirstNamesMale = true;
							rules->bAllDashFirstNamesFemale = true;
						}
					}
					if (randomIntRange(0, 1))
					{
						rules->bHasDashLastNamesMale = true;
						rules->bHasDashLastNamesFemale = true;
						if (randomIntRange(0, 99) < 5)
						{
							rules->bAllDashLastNamesMale = true;
							rules->bAllDashLastNamesFemale = true;
						}
					}
				}
			}
		}
	}

	rules->c1Many1ConsLastNamesMale[0] = rules->c1Many1ConsLastNamesMale[1] = rules->c1Many1ConsLastNamesMale[2] = NULL;
	rules->c1Many2ConsLastNamesMale[0] = rules->c1Many2ConsLastNamesMale[1] = rules->c1Many2ConsLastNamesMale[2] = NULL;
	rules->c1Many3ConsLastNamesMale[0] = rules->c1Many3ConsLastNamesMale[1] = rules->c1Many3ConsLastNamesMale[2] = NULL;
	rules->b1AllConsLastNamesMale = false;
	rules->c1Many1VowelLastNamesMale[0] = rules->c1Many1VowelLastNamesMale[1] = rules->c1Many1VowelLastNamesMale[2] = NULL;
	rules->c1Many2VowelLastNamesMale[0] = rules->c1Many2VowelLastNamesMale[1] = rules->c1Many2VowelLastNamesMale[2] = NULL;
	rules->c1Many3VowelLastNamesMale[0] = rules->c1Many3VowelLastNamesMale[1] = rules->c1Many3VowelLastNamesMale[2] = NULL;
	rules->b1AllVowelLastNamesMale = false;

	rules->c2Many1ConsLastNamesMale[0] = rules->c2Many1ConsLastNamesMale[1] = rules->c2Many1ConsLastNamesMale[2] = NULL;
	rules->c2Many2ConsLastNamesMale[0] = rules->c2Many2ConsLastNamesMale[1] = rules->c2Many2ConsLastNamesMale[2] = NULL;
	rules->c2Many3ConsLastNamesMale[0] = rules->c2Many3ConsLastNamesMale[1] = rules->c2Many3ConsLastNamesMale[2] = NULL;
	rules->b2AllConsLastNamesMale = false;
	rules->c2Many1VowelLastNamesMale[0] = rules->c2Many1VowelLastNamesMale[1] = rules->c2Many1VowelLastNamesMale[2] = NULL;
	rules->c2Many2VowelLastNamesMale[0] = rules->c2Many2VowelLastNamesMale[1] = rules->c2Many2VowelLastNamesMale[2] = NULL;
	rules->c2Many3VowelLastNamesMale[0] = rules->c2Many3VowelLastNamesMale[1] = rules->c2Many3VowelLastNamesMale[2] = NULL;
	rules->b2AllVowelLastNamesMale = false;

	rules->c3Many1ConsLastNamesMale[0] = rules->c3Many1ConsLastNamesMale[1] = rules->c3Many1ConsLastNamesMale[2] = NULL;
	rules->c3Many2ConsLastNamesMale[0] = rules->c3Many2ConsLastNamesMale[1] = rules->c3Many2ConsLastNamesMale[2] = NULL;
	rules->c3Many3ConsLastNamesMale[0] = rules->c3Many3ConsLastNamesMale[1] = rules->c3Many3ConsLastNamesMale[2] = NULL;
	rules->b3AllConsLastNamesMale = false;
	rules->c3Many1VowelLastNamesMale[0] = rules->c3Many1VowelLastNamesMale[1] = rules->c3Many1VowelLastNamesMale[2] = NULL;
	rules->c3Many2VowelLastNamesMale[0] = rules->c3Many2VowelLastNamesMale[1] = rules->c3Many2VowelLastNamesMale[2] = NULL;
	rules->c3Many3VowelLastNamesMale[0] = rules->c3Many3VowelLastNamesMale[1] = rules->c3Many3VowelLastNamesMale[2] = NULL;
	rules->b3AllVowelLastNamesMale = false;

	rules->c1Many1ConsFirstNamesMale[0] = rules->c1Many1ConsFirstNamesMale[1] = rules->c1Many1ConsFirstNamesMale[2] = NULL;
	rules->c1Many2ConsFirstNamesMale[0] = rules->c1Many2ConsFirstNamesMale[1] = rules->c1Many2ConsFirstNamesMale[2] = NULL;
	rules->c1Many3ConsFirstNamesMale[0] = rules->c1Many3ConsFirstNamesMale[1] = rules->c1Many3ConsFirstNamesMale[2] = NULL;
	rules->b1AllConsFirstNamesMale = false;
	rules->c1Many1VowelFirstNamesMale[0] = rules->c1Many1VowelFirstNamesMale[1] = rules->c1Many1VowelFirstNamesMale[2] = NULL;
	rules->c1Many2VowelFirstNamesMale[0] = rules->c1Many2VowelFirstNamesMale[1] = rules->c1Many2VowelFirstNamesMale[2] = NULL;
	rules->c1Many3VowelFirstNamesMale[0] = rules->c1Many3VowelFirstNamesMale[1] = rules->c1Many3VowelFirstNamesMale[2] = NULL;
	rules->b1AllVowelFirstNamesMale = false;

	rules->c2Many1ConsFirstNamesMale[0] = rules->c2Many1ConsFirstNamesMale[1] = rules->c2Many1ConsFirstNamesMale[2] = NULL;
	rules->c2Many2ConsFirstNamesMale[0] = rules->c2Many2ConsFirstNamesMale[1] = rules->c2Many2ConsFirstNamesMale[2] = NULL;
	rules->c2Many3ConsFirstNamesMale[0] = rules->c2Many3ConsFirstNamesMale[1] = rules->c2Many3ConsFirstNamesMale[2] = NULL;
	rules->b2AllConsFirstNamesMale = false;
	rules->c2Many1VowelFirstNamesMale[0] = rules->c2Many1VowelFirstNamesMale[1] = rules->c2Many1VowelFirstNamesMale[2] = NULL;
	rules->c2Many2VowelFirstNamesMale[0] = rules->c2Many2VowelFirstNamesMale[1] = rules->c2Many2VowelFirstNamesMale[2] = NULL;
	rules->c2Many3VowelFirstNamesMale[0] = rules->c2Many3VowelFirstNamesMale[1] = rules->c2Many3VowelFirstNamesMale[2] = NULL;
	rules->b2AllVowelFirstNamesMale = false;

	rules->c3Many1ConsFirstNamesMale[0] = rules->c3Many1ConsFirstNamesMale[1] = rules->c3Many1ConsFirstNamesMale[2] = NULL;
	rules->c3Many2ConsFirstNamesMale[0] = rules->c3Many2ConsFirstNamesMale[1] = rules->c3Many2ConsFirstNamesMale[2] = NULL;
	rules->c3Many3ConsFirstNamesMale[0] = rules->c3Many3ConsFirstNamesMale[1] = rules->c3Many3ConsFirstNamesMale[2] = NULL;
	rules->b3AllConsFirstNamesMale = false;
	rules->c3Many1VowelFirstNamesMale[0] = rules->c3Many1VowelFirstNamesMale[1] = rules->c3Many1VowelFirstNamesMale[2] = NULL;
	rules->c3Many2VowelFirstNamesMale[0] = rules->c3Many2VowelFirstNamesMale[1] = rules->c3Many2VowelFirstNamesMale[2] = NULL;
	rules->c3Many3VowelFirstNamesMale[0] = rules->c3Many3VowelFirstNamesMale[1] = rules->c3Many3VowelFirstNamesMale[2] = NULL;
	rules->b3AllVowelFirstNamesMale = false;

	rules->c1Many1ConsLastNamesFemale[0] = rules->c1Many1ConsLastNamesFemale[1] = rules->c1Many1ConsLastNamesFemale[2] = NULL;
	rules->c1Many2ConsLastNamesFemale[0] = rules->c1Many2ConsLastNamesFemale[1] = rules->c1Many2ConsLastNamesFemale[2] = NULL;
	rules->c1Many3ConsLastNamesFemale[0] = rules->c1Many3ConsLastNamesFemale[1] = rules->c1Many3ConsLastNamesFemale[2] = NULL;
	rules->b1AllConsLastNamesFemale = false;
	rules->c1Many1VowelLastNamesFemale[0] = rules->c1Many1VowelLastNamesFemale[1] = rules->c1Many1VowelLastNamesFemale[2] = NULL;
	rules->c1Many2VowelLastNamesFemale[0] = rules->c1Many2VowelLastNamesFemale[1] = rules->c1Many2VowelLastNamesFemale[2] = NULL;
	rules->c1Many3VowelLastNamesFemale[0] = rules->c1Many3VowelLastNamesFemale[1] = rules->c1Many3VowelLastNamesFemale[2] = NULL;
	rules->b1AllVowelLastNamesFemale = false;

	rules->c2Many1ConsLastNamesFemale[0] = rules->c2Many1ConsLastNamesFemale[1] = rules->c2Many1ConsLastNamesFemale[2] = NULL;
	rules->c2Many2ConsLastNamesFemale[0] = rules->c2Many2ConsLastNamesFemale[1] = rules->c2Many2ConsLastNamesFemale[2] = NULL;
	rules->c2Many3ConsLastNamesFemale[0] = rules->c2Many3ConsLastNamesFemale[1] = rules->c2Many3ConsLastNamesFemale[2] = NULL;
	rules->b2AllConsLastNamesFemale = false;
	rules->c2Many1VowelLastNamesFemale[0] = rules->c2Many1VowelLastNamesFemale[1] = rules->c2Many1VowelLastNamesFemale[2] = NULL;
	rules->c2Many2VowelLastNamesFemale[0] = rules->c2Many2VowelLastNamesFemale[1] = rules->c2Many2VowelLastNamesFemale[2] = NULL;
	rules->c2Many3VowelLastNamesFemale[0] = rules->c2Many3VowelLastNamesFemale[1] = rules->c2Many3VowelLastNamesFemale[2] = NULL;
	rules->b2AllVowelLastNamesFemale = false;

	rules->c3Many1ConsLastNamesFemale[0] = rules->c3Many1ConsLastNamesFemale[1] = rules->c3Many1ConsLastNamesFemale[2] = NULL;
	rules->c3Many2ConsLastNamesFemale[0] = rules->c3Many2ConsLastNamesFemale[1] = rules->c3Many2ConsLastNamesFemale[2] = NULL;
	rules->c3Many3ConsLastNamesFemale[0] = rules->c3Many3ConsLastNamesFemale[1] = rules->c3Many3ConsLastNamesFemale[2] = NULL;
	rules->b3AllConsLastNamesFemale = false;
	rules->c3Many1VowelLastNamesFemale[0] = rules->c3Many1VowelLastNamesFemale[1] = rules->c3Many1VowelLastNamesFemale[2] = NULL;
	rules->c3Many2VowelLastNamesFemale[0] = rules->c3Many2VowelLastNamesFemale[1] = rules->c3Many2VowelLastNamesFemale[2] = NULL;
	rules->c3Many3VowelLastNamesFemale[0] = rules->c3Many3VowelLastNamesFemale[1] = rules->c3Many3VowelLastNamesFemale[2] = NULL;
	rules->b3AllVowelLastNamesFemale = false;

	rules->c1Many1ConsFirstNamesFemale[0] = rules->c1Many1ConsFirstNamesFemale[1] = rules->c1Many1ConsFirstNamesFemale[2] = NULL;
	rules->c1Many2ConsFirstNamesFemale[0] = rules->c1Many2ConsFirstNamesFemale[1] = rules->c1Many2ConsFirstNamesFemale[2] = NULL;
	rules->c1Many3ConsFirstNamesFemale[0] = rules->c1Many3ConsFirstNamesFemale[1] = rules->c1Many3ConsFirstNamesFemale[2] = NULL;
	rules->b1AllConsFirstNamesFemale = false;
	rules->c1Many1VowelFirstNamesFemale[0] = rules->c1Many1VowelFirstNamesFemale[1] = rules->c1Many1VowelFirstNamesFemale[2] = NULL;
	rules->c1Many2VowelFirstNamesFemale[0] = rules->c1Many2VowelFirstNamesFemale[1] = rules->c1Many2VowelFirstNamesFemale[2] = NULL;
	rules->c1Many3VowelFirstNamesFemale[0] = rules->c1Many3VowelFirstNamesFemale[1] = rules->c1Many3VowelFirstNamesFemale[2] = NULL;
	rules->b1AllVowelFirstNamesFemale = false;

	rules->c2Many1ConsFirstNamesFemale[0] = rules->c2Many1ConsFirstNamesFemale[1] = rules->c2Many1ConsFirstNamesFemale[2] = NULL;
	rules->c2Many2ConsFirstNamesFemale[0] = rules->c2Many2ConsFirstNamesFemale[1] = rules->c2Many2ConsFirstNamesFemale[2] = NULL;
	rules->c2Many3ConsFirstNamesFemale[0] = rules->c2Many3ConsFirstNamesFemale[1] = rules->c2Many3ConsFirstNamesFemale[2] = NULL;
	rules->b2AllConsFirstNamesFemale = false;
	rules->c2Many1VowelFirstNamesFemale[0] = rules->c2Many1VowelFirstNamesFemale[1] = rules->c2Many1VowelFirstNamesFemale[2] = NULL;
	rules->c2Many2VowelFirstNamesFemale[0] = rules->c2Many2VowelFirstNamesFemale[1] = rules->c2Many2VowelFirstNamesFemale[2] = NULL;
	rules->c2Many3VowelFirstNamesFemale[0] = rules->c2Many3VowelFirstNamesFemale[1] = rules->c2Many3VowelFirstNamesFemale[2] = NULL;
	rules->b2AllVowelFirstNamesFemale = false;

	rules->c3Many1ConsFirstNamesFemale[0] = rules->c3Many1ConsFirstNamesFemale[1] = rules->c3Many1ConsFirstNamesFemale[2] = NULL;
	rules->c3Many2ConsFirstNamesFemale[0] = rules->c3Many2ConsFirstNamesFemale[1] = rules->c3Many2ConsFirstNamesFemale[2] = NULL;
	rules->c3Many3ConsFirstNamesFemale[0] = rules->c3Many3ConsFirstNamesFemale[1] = rules->c3Many3ConsFirstNamesFemale[2] = NULL;
	rules->b3AllConsFirstNamesFemale = false;
	rules->c3Many1VowelFirstNamesFemale[0] = rules->c3Many1VowelFirstNamesFemale[1] = rules->c3Many1VowelFirstNamesFemale[2] = NULL;
	rules->c3Many2VowelFirstNamesFemale[0] = rules->c3Many2VowelFirstNamesFemale[1] = rules->c3Many2VowelFirstNamesFemale[2] = NULL;
	rules->c3Many3VowelFirstNamesFemale[0] = rules->c3Many3VowelFirstNamesFemale[1] = rules->c3Many3VowelFirstNamesFemale[2] = NULL;
	rules->b3AllVowelFirstNamesFemale = false;

	if (!geaFirstVowels)
	{
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_A"); if (ps) eaPush(&geaFirstVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_E"); if (ps) eaPush(&geaFirstVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_I"); if (ps) eaPush(&geaFirstVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_O"); if (ps) eaPush(&geaFirstVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_U"); if (ps) eaPush(&geaFirstVowels, ps);
	}

	if (!geaVowels)
	{
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_A"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_E"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_I"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_O"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_U"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OO"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_EA"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_IR"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_AY"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_EE"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_EI"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OA"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OE"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OU"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OW"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OY"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_Y"); if (ps) {eaPush(&geaVowels, ps); eaPush(&geaVowels, ps);}
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OUR"); if (ps) eaPush(&geaVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_IRE"); if (ps) eaPush(&geaVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_AYER"); if (ps) eaPush(&geaVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OYAL"); if (ps) eaPush(&geaVowels, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_OWER"); if (ps) eaPush(&geaVowels, ps);
	}

	if (!geaCons)
	{
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_QU"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_B"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_C"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_D"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_F"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_G"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_H"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_J"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_K"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_L"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_M"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_N"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_P"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_R"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_S"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_T"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_V"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_W"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_X"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_Y"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_Z"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_ST"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_CH"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_PH"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_SH"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_TH"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_DG"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_NG"); if (ps) eaPush(&geaCons, ps);
		ps = RefSystem_ReferentFromString("PhonemeSet", "Char_SY"); if (ps) eaPush(&geaCons, ps);
	}

	if (randomIntRange(0, 1))
	{
		bool bIsVowel1 = randomIntRange(0, 99) < 5;
		bool bIsVowel2 = randomIntRange(0, 99) < 5;
		bool bIsVowel3 = randomIntRange(0, 99) < 5;

		//First Names
		switch (randomIntRange(0, 8))
		{
		case 0: //Names start with the same letter
			if (bIsVowel1)
			{
				ps = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
				rules->c1Many1VowelFirstNamesMale[0] = rules->c1Many1VowelFirstNamesMale[1] = rules->c1Many1VowelFirstNamesMale[2] = ps;
				rules->c1Many1VowelFirstNamesFemale[0] = rules->c1Many1VowelFirstNamesFemale[1] = rules->c1Many1VowelFirstNamesFemale[2] = ps;
			}
			else
			{
				ps = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
				rules->c1Many1ConsFirstNamesMale[0] = rules->c1Many1ConsFirstNamesMale[1] = rules->c1Many1ConsFirstNamesMale[2] = ps;
				rules->c1Many1ConsFirstNamesFemale[0] = rules->c1Many1ConsFirstNamesFemale[1] = rules->c1Many1ConsFirstNamesFemale[2] = ps;
			}
			if (randomIntRange(0, 1))
			{
				if (bIsVowel2)
				{
					ps = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					rules->c1Many2VowelFirstNamesMale[0] = rules->c1Many2VowelFirstNamesMale[1] = rules->c1Many2VowelFirstNamesMale[2] = ps;
					rules->c1Many2VowelFirstNamesFemale[0] = rules->c1Many2VowelFirstNamesFemale[1] = rules->c1Many2VowelFirstNamesFemale[2] = ps;
				}
				else
				{
					ps = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					rules->c1Many2ConsFirstNamesMale[0] = rules->c1Many2ConsFirstNamesMale[1] = rules->c1Many2ConsFirstNamesMale[2] = ps;
					rules->c1Many2ConsFirstNamesFemale[0] = rules->c1Many2ConsFirstNamesFemale[1] = rules->c1Many2ConsFirstNamesFemale[2] = ps;
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel3)
					{
						ps = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						rules->c1Many3VowelFirstNamesMale[0] = rules->c1Many3VowelFirstNamesMale[1] = rules->c1Many3VowelFirstNamesMale[2] = ps;
						rules->c1Many3VowelFirstNamesFemale[0] = rules->c1Many3VowelFirstNamesFemale[1] = rules->c1Many3VowelFirstNamesFemale[2] = ps;
					}
					else
					{
						ps = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						rules->c1Many3ConsFirstNamesMale[0] = rules->c1Many3ConsFirstNamesMale[1] = rules->c1Many3ConsFirstNamesMale[2] = ps;
						rules->c1Many3ConsFirstNamesFemale[0] = rules->c1Many3ConsFirstNamesFemale[1] = rules->c1Many3ConsFirstNamesFemale[2] = ps;
					}
				}
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b1AllVowelFirstNamesMale = true;
				rules->b1AllVowelFirstNamesFemale = true;
				rules->b1AllConsFirstNamesMale = true;
				rules->b1AllConsFirstNamesFemale = true;
			}
			break;
		case 1: //Names in gender start with the same letter
			if (randomIntRange(0, 1))
			{
				if (bIsVowel1) rules->c1Many1VowelFirstNamesMale[0] = rules->c1Many1VowelFirstNamesMale[1] = rules->c1Many1VowelFirstNamesMale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
				else rules->c1Many1ConsFirstNamesMale[0] = rules->c1Many1ConsFirstNamesMale[1] = rules->c1Many1ConsFirstNamesMale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
				if (randomIntRange(0, 1))
				{
					if (bIsVowel2) rules->c1Many2VowelFirstNamesMale[0] = rules->c1Many2VowelFirstNamesMale[1] = rules->c1Many2VowelFirstNamesMale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					else rules->c1Many2ConsFirstNamesMale[0] = rules->c1Many2ConsFirstNamesMale[1] = rules->c1Many2ConsFirstNamesMale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel3) rules->c1Many3VowelFirstNamesMale[0] = rules->c1Many3VowelFirstNamesMale[1] = rules->c1Many3VowelFirstNamesMale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						else rules->c1Many3ConsFirstNamesMale[0] = rules->c1Many3ConsFirstNamesMale[1] = rules->c1Many3ConsFirstNamesMale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					}
				}
				if (randomIntRange(0, 99) < 5)
				{
					//Letter is required
					rules->b1AllVowelFirstNamesMale = true;
					rules->b1AllConsFirstNamesMale = true;
				}
			}
			if (randomIntRange(0, 1))
			{
				if (bIsVowel1) rules->c1Many1VowelFirstNamesFemale[0] = rules->c1Many1VowelFirstNamesFemale[1] = rules->c1Many1VowelFirstNamesFemale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
				else rules->c1Many1ConsFirstNamesFemale[0] = rules->c1Many1ConsFirstNamesFemale[1] = rules->c1Many1ConsFirstNamesFemale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
				if (randomIntRange(0, 1))
				{
					if (bIsVowel2) rules->c1Many2VowelFirstNamesFemale[0] = rules->c1Many2VowelFirstNamesFemale[1] = rules->c1Many2VowelFirstNamesFemale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					else rules->c1Many2ConsFirstNamesFemale[0] = rules->c1Many2ConsFirstNamesFemale[1] = rules->c1Many2ConsFirstNamesFemale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel3) rules->c1Many3VowelFirstNamesFemale[0] = rules->c1Many3VowelFirstNamesFemale[1] = rules->c1Many3VowelFirstNamesFemale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						else rules->c1Many3ConsFirstNamesFemale[0] = rules->c1Many3ConsFirstNamesFemale[1] = rules->c1Many3ConsFirstNamesFemale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					}
				}
				if (randomIntRange(0, 99) < 5)
				{
					//Letter is required
					rules->b1AllVowelFirstNamesFemale = true;
					rules->b1AllConsFirstNamesFemale = true;
				}
			}
			break;
		case 2: //Names in sub-gender each start with a different letter
			for (i = 0; i < 3; ++i)
			{
				if (randomIntRange(0, 1))
				{
					if (bIsVowel1) rules->c1Many1VowelFirstNamesMale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					else rules->c1Many1ConsFirstNamesMale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel2) rules->c1Many2VowelFirstNamesMale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						else rules->c1Many2ConsFirstNamesMale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						if (randomIntRange(0, 1))
						{
							if (bIsVowel3) rules->c1Many3VowelFirstNamesMale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
							else rules->c1Many3ConsFirstNamesMale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						}
					}
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel1) rules->c1Many1VowelFirstNamesFemale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					else rules->c1Many1ConsFirstNamesFemale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel2) rules->c1Many2VowelFirstNamesFemale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						else rules->c1Many2ConsFirstNamesFemale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						if (randomIntRange(0, 1))
						{
							if (bIsVowel3) rules->c1Many3VowelFirstNamesFemale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
							else rules->c1Many3ConsFirstNamesFemale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						}
					}
				}
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b1AllVowelFirstNamesMale = true;
				rules->b1AllConsFirstNamesMale = true;
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b1AllVowelFirstNamesFemale = true;
				rules->b1AllConsFirstNamesFemale = true;
			}
			break;
		case 3: //Names start with the same two letters
			if (bIsVowel1)
			{
				ps = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
				rules->c1Many1VowelFirstNamesMale[0] = rules->c1Many1VowelFirstNamesMale[1] = rules->c1Many1VowelFirstNamesMale[2] = ps;
				rules->c1Many1VowelFirstNamesFemale[0] = rules->c1Many1VowelFirstNamesFemale[1] = rules->c1Many1VowelFirstNamesFemale[2] = ps;
				ps = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
				rules->c2Many1ConsFirstNamesMale[0] = rules->c2Many1ConsFirstNamesMale[1] = rules->c2Many1ConsFirstNamesMale[2] = ps;
				rules->c2Many1ConsFirstNamesFemale[0] = rules->c2Many1ConsFirstNamesFemale[1] = rules->c2Many1ConsFirstNamesFemale[2] = ps;
			}
			else
			{
				ps = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
				if (ps == geaCons[0])
				{
					ps = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
					rules->c1Many1ConsFirstNamesMale[0] = rules->c1Many1ConsFirstNamesMale[1] = rules->c1Many1ConsFirstNamesMale[2] = ps;
					rules->c1Many1ConsFirstNamesFemale[0] = rules->c1Many1ConsFirstNamesFemale[1] = rules->c1Many1ConsFirstNamesFemale[2] = ps;
					ps = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
					rules->c2Many1VowelFirstNamesMale[0] = rules->c2Many1VowelFirstNamesMale[1] = rules->c2Many1VowelFirstNamesMale[2] = ps;
					rules->c2Many1VowelFirstNamesFemale[0] = rules->c2Many1VowelFirstNamesFemale[1] = rules->c2Many1VowelFirstNamesFemale[2] = ps;
				}
				else
				{
					rules->c1Many1ConsFirstNamesMale[0] = rules->c1Many1ConsFirstNamesMale[1] = rules->c1Many1ConsFirstNamesMale[2] = ps;
					rules->c1Many1ConsFirstNamesFemale[0] = rules->c1Many1ConsFirstNamesFemale[1] = rules->c1Many1ConsFirstNamesFemale[2] = ps;
					ps = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					rules->c2Many1VowelFirstNamesMale[0] = rules->c2Many1VowelFirstNamesMale[1] = rules->c2Many1VowelFirstNamesMale[2] = ps;
					rules->c2Many1VowelFirstNamesFemale[0] = rules->c2Many1VowelFirstNamesFemale[1] = rules->c2Many1VowelFirstNamesFemale[2] = ps;
				}
			}
			if (randomIntRange(0, 1))
			{
				if (bIsVowel2)
				{
					ps = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					rules->c1Many2VowelFirstNamesMale[0] = rules->c1Many2VowelFirstNamesMale[1] = rules->c1Many2VowelFirstNamesMale[2] = ps;
					rules->c1Many2VowelFirstNamesFemale[0] = rules->c1Many2VowelFirstNamesFemale[1] = rules->c1Many2VowelFirstNamesFemale[2] = ps;
					ps = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					rules->c2Many2ConsFirstNamesMale[0] = rules->c2Many2ConsFirstNamesMale[1] = rules->c2Many2ConsFirstNamesMale[2] = ps;
					rules->c2Many2ConsFirstNamesFemale[0] = rules->c2Many2ConsFirstNamesFemale[1] = rules->c2Many2ConsFirstNamesFemale[2] = ps;
				}
				else
				{
					ps = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (ps == geaCons[0])
					{
						ps = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
						rules->c1Many2ConsFirstNamesMale[0] = rules->c1Many2ConsFirstNamesMale[1] = rules->c1Many2ConsFirstNamesMale[2] = ps;
						rules->c1Many2ConsFirstNamesFemale[0] = rules->c1Many2ConsFirstNamesFemale[1] = rules->c1Many2ConsFirstNamesFemale[2] = ps;
						ps = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
						rules->c2Many2VowelFirstNamesMale[0] = rules->c2Many2VowelFirstNamesMale[1] = rules->c2Many2VowelFirstNamesMale[2] = ps;
						rules->c2Many2VowelFirstNamesFemale[0] = rules->c2Many2VowelFirstNamesFemale[1] = rules->c2Many2VowelFirstNamesFemale[2] = ps;
					}
					else
					{
						rules->c1Many2ConsFirstNamesMale[0] = rules->c1Many2ConsFirstNamesMale[1] = rules->c1Many2ConsFirstNamesMale[2] = ps;
						rules->c1Many2ConsFirstNamesFemale[0] = rules->c1Many2ConsFirstNamesFemale[1] = rules->c1Many2ConsFirstNamesFemale[2] = ps;
						ps = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						rules->c2Many2VowelFirstNamesMale[0] = rules->c2Many2VowelFirstNamesMale[1] = rules->c2Many2VowelFirstNamesMale[2] = ps;
						rules->c2Many2VowelFirstNamesFemale[0] = rules->c2Many2VowelFirstNamesFemale[1] = rules->c2Many2VowelFirstNamesFemale[2] = ps;
					}
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel3)
					{
						ps = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						rules->c1Many3VowelFirstNamesMale[0] = rules->c1Many3VowelFirstNamesMale[1] = rules->c1Many3VowelFirstNamesMale[2] = ps;
						rules->c1Many3VowelFirstNamesFemale[0] = rules->c1Many3VowelFirstNamesFemale[1] = rules->c1Many3VowelFirstNamesFemale[2] = ps;
						ps = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						rules->c2Many3ConsFirstNamesMale[0] = rules->c2Many3ConsFirstNamesMale[1] = rules->c2Many3ConsFirstNamesMale[2] = ps;
						rules->c2Many3ConsFirstNamesFemale[0] = rules->c2Many3ConsFirstNamesFemale[1] = rules->c2Many3ConsFirstNamesFemale[2] = ps;
					}
					else
					{
						ps = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (ps == geaCons[0])
						{
							ps = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
							rules->c1Many3ConsFirstNamesMale[0] = rules->c1Many3ConsFirstNamesMale[1] = rules->c1Many3ConsFirstNamesMale[2] = ps;
							rules->c1Many3ConsFirstNamesFemale[0] = rules->c1Many3ConsFirstNamesFemale[1] = rules->c1Many3ConsFirstNamesFemale[2] = ps;
							ps = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
							rules->c2Many3VowelFirstNamesMale[0] = rules->c2Many3VowelFirstNamesMale[1] = rules->c2Many3VowelFirstNamesMale[2] = ps;
							rules->c2Many3VowelFirstNamesFemale[0] = rules->c2Many3VowelFirstNamesFemale[1] = rules->c2Many3VowelFirstNamesFemale[2] = ps;
						}
						else
						{
							rules->c1Many3ConsFirstNamesMale[0] = rules->c1Many3ConsFirstNamesMale[1] = rules->c1Many3ConsFirstNamesMale[2] = ps;
							rules->c1Many3ConsFirstNamesFemale[0] = rules->c1Many3ConsFirstNamesFemale[1] = rules->c1Many3ConsFirstNamesFemale[2] = ps;
							ps = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							rules->c2Many3VowelFirstNamesMale[0] = rules->c2Many3VowelFirstNamesMale[1] = rules->c2Many3VowelFirstNamesMale[2] = ps;
							rules->c2Many3VowelFirstNamesFemale[0] = rules->c2Many3VowelFirstNamesFemale[1] = rules->c2Many3VowelFirstNamesFemale[2] = ps;
						}
					}
				}
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b1AllVowelFirstNamesMale = true;
				rules->b1AllVowelFirstNamesFemale = true;
				rules->b1AllConsFirstNamesMale = true;
				rules->b1AllConsFirstNamesFemale = true;
				rules->b2AllVowelFirstNamesMale = true;
				rules->b2AllVowelFirstNamesFemale = true;
				rules->b2AllConsFirstNamesMale = true;
				rules->b2AllConsFirstNamesFemale = true;
			}
			break;
		case 4: //Names in gender start with the same two letters
			if (randomIntRange(0, 1))
			{
				if (bIsVowel1)
				{
					rules->c1Many1VowelFirstNamesMale[0] = rules->c1Many1VowelFirstNamesMale[1] = rules->c1Many1VowelFirstNamesMale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					rules->c2Many1ConsFirstNamesMale[0] = rules->c2Many1ConsFirstNamesMale[1] = rules->c2Many1ConsFirstNamesMale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
				}
				else
				{
					rules->c1Many1ConsFirstNamesMale[0] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (rules->c1Many1ConsFirstNamesMale[0] == geaCons[0])
					{
						rules->c1Many1VowelFirstNamesMale[0] = rules->c1Many1VowelFirstNamesMale[1] = rules->c1Many1VowelFirstNamesMale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
						rules->c2Many1VowelFirstNamesMale[0] = rules->c2Many1VowelFirstNamesMale[1] = rules->c2Many1VowelFirstNamesMale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
					}
					else
					{
						rules->c1Many1ConsFirstNamesMale[1] = rules->c1Many1ConsFirstNamesMale[2] = rules->c1Many1ConsFirstNamesMale[0];
						rules->c2Many1VowelFirstNamesMale[0] = rules->c2Many1VowelFirstNamesMale[1] = rules->c2Many1VowelFirstNamesMale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					}
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel2)
					{
						rules->c1Many2VowelFirstNamesMale[0] = rules->c1Many2VowelFirstNamesMale[1] = rules->c1Many2VowelFirstNamesMale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						rules->c2Many2ConsFirstNamesMale[0] = rules->c2Many2ConsFirstNamesMale[1] = rules->c2Many2ConsFirstNamesMale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					}
					else
					{
						rules->c1Many2ConsFirstNamesMale[0] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (rules->c1Many2ConsFirstNamesMale[0] == geaCons[0])
						{
							rules->c1Many2VowelFirstNamesMale[0] = rules->c1Many2VowelFirstNamesMale[1] = rules->c1Many2VowelFirstNamesMale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
							rules->c2Many2VowelFirstNamesMale[0] = rules->c2Many2VowelFirstNamesMale[1] = rules->c2Many2VowelFirstNamesMale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
						}
						else
						{
							rules->c1Many2ConsFirstNamesMale[1] = rules->c1Many2ConsFirstNamesMale[2] = rules->c1Many2ConsFirstNamesMale[0];
							rules->c2Many2VowelFirstNamesMale[0] = rules->c2Many2VowelFirstNamesMale[1] = rules->c2Many2VowelFirstNamesMale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						}
					}
					if (randomIntRange(0, 1))
					{
						if (bIsVowel3)
						{
							rules->c1Many3VowelFirstNamesMale[0] = rules->c1Many3VowelFirstNamesMale[1] = rules->c1Many3VowelFirstNamesMale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
							rules->c2Many3ConsFirstNamesMale[0] = rules->c2Many3ConsFirstNamesMale[1] = rules->c2Many3ConsFirstNamesMale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						}
						else
						{
							rules->c1Many3ConsFirstNamesMale[0] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
							if (rules->c1Many3ConsFirstNamesMale[0] == geaCons[0])
							{
								rules->c1Many3VowelFirstNamesMale[0] = rules->c1Many3VowelFirstNamesMale[1] = rules->c1Many3VowelFirstNamesMale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
								rules->c2Many3VowelFirstNamesMale[0] = rules->c2Many3VowelFirstNamesMale[1] = rules->c2Many3VowelFirstNamesMale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
							}
							else
							{
								rules->c1Many3ConsFirstNamesMale[1] = rules->c1Many3ConsFirstNamesMale[2] = rules->c1Many3ConsFirstNamesMale[0];
								rules->c2Many3VowelFirstNamesMale[0] = rules->c2Many3VowelFirstNamesMale[1] = rules->c2Many3VowelFirstNamesMale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							}
						}
					}
				}
				if (randomIntRange(0, 99) < 5)
				{
					//Letter is required
					rules->b1AllVowelFirstNamesMale = true;
					rules->b1AllConsFirstNamesMale = true;
					rules->b2AllVowelFirstNamesMale = true;
					rules->b2AllConsFirstNamesMale = true;
				}
			}
			if (randomIntRange(0, 1))
			{
				if (bIsVowel1)
				{
					rules->c1Many1VowelFirstNamesFemale[0] = rules->c1Many1VowelFirstNamesFemale[1] = rules->c1Many1VowelFirstNamesFemale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
					rules->c2Many1ConsFirstNamesFemale[0] = rules->c2Many1ConsFirstNamesFemale[1] = rules->c2Many1ConsFirstNamesFemale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
				}
				else
				{
					rules->c1Many1ConsFirstNamesFemale[0] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (rules->c1Many1ConsFirstNamesFemale[0] == geaCons[0])
					{
						rules->c1Many1VowelFirstNamesFemale[0] = rules->c1Many1VowelFirstNamesFemale[1] = rules->c1Many1VowelFirstNamesFemale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
						rules->c2Many1VowelFirstNamesFemale[0] = rules->c2Many1VowelFirstNamesFemale[1] = rules->c2Many1VowelFirstNamesFemale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
					}
					else
					{
						rules->c1Many1ConsFirstNamesFemale[1] = rules->c1Many1ConsFirstNamesFemale[2] = rules->c1Many1ConsFirstNamesFemale[0];
						rules->c2Many1VowelFirstNamesFemale[0] = rules->c2Many1VowelFirstNamesFemale[1] = rules->c2Many1VowelFirstNamesFemale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					}
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel2)
					{
						rules->c1Many2VowelFirstNamesFemale[0] = rules->c1Many2VowelFirstNamesFemale[1] = rules->c1Many2VowelFirstNamesFemale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						rules->c2Many2ConsFirstNamesFemale[0] = rules->c2Many2ConsFirstNamesFemale[1] = rules->c2Many2ConsFirstNamesFemale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					}
					else
					{
						rules->c1Many2ConsFirstNamesFemale[0] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (rules->c1Many2ConsFirstNamesFemale[0] == geaCons[0])
						{
							rules->c1Many2VowelFirstNamesFemale[0] = rules->c1Many2VowelFirstNamesFemale[1] = rules->c1Many2VowelFirstNamesFemale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
							rules->c2Many2VowelFirstNamesFemale[0] = rules->c2Many2VowelFirstNamesFemale[1] = rules->c2Many2VowelFirstNamesFemale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
						}
						else
						{
							rules->c1Many2ConsFirstNamesFemale[1] = rules->c1Many2ConsFirstNamesFemale[2] = rules->c1Many2ConsFirstNamesFemale[0];
							rules->c2Many2VowelFirstNamesFemale[0] = rules->c2Many2VowelFirstNamesFemale[1] = rules->c2Many2VowelFirstNamesFemale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						}
					}
					if (randomIntRange(0, 1))
					{
						if (bIsVowel3)
						{
							rules->c1Many3VowelFirstNamesFemale[0] = rules->c1Many3VowelFirstNamesFemale[1] = rules->c1Many3VowelFirstNamesFemale[2] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
							rules->c2Many3ConsFirstNamesFemale[0] = rules->c2Many3ConsFirstNamesFemale[1] = rules->c2Many3ConsFirstNamesFemale[2] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						}
						else
						{
							rules->c1Many3ConsFirstNamesFemale[0] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
							if (rules->c1Many3ConsFirstNamesFemale[0] == geaCons[0])
							{
								rules->c1Many3VowelFirstNamesFemale[0] = rules->c1Many3VowelFirstNamesFemale[1] = rules->c1Many3VowelFirstNamesFemale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
								rules->c2Many3VowelFirstNamesFemale[0] = rules->c2Many3VowelFirstNamesFemale[1] = rules->c2Many3VowelFirstNamesFemale[2] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
							}
							else
							{
								rules->c1Many3ConsFirstNamesFemale[1] = rules->c1Many3ConsFirstNamesFemale[2] = rules->c1Many3ConsFirstNamesFemale[0];
								rules->c2Many3VowelFirstNamesFemale[0] = rules->c2Many3VowelFirstNamesFemale[1] = rules->c2Many3VowelFirstNamesFemale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							}
						}
					}
				}
				if (randomIntRange(0, 99) < 5)
				{
					//Letter is required
					rules->b1AllVowelFirstNamesFemale = true;
					rules->b1AllConsFirstNamesFemale = true;
					rules->b2AllVowelFirstNamesFemale = true;
					rules->b2AllConsFirstNamesFemale = true;
				}
			}
			break;
		case 5: //Names in sub-gender each start with two different letters
			for (i = 0; i < 3; ++i)
			{
				if (randomIntRange(0, 1))
				{
					if (bIsVowel1)
					{
						rules->c1Many1VowelFirstNamesMale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						rules->c2Many1ConsFirstNamesMale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					}
					else
					{
						rules->c1Many1ConsFirstNamesMale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (rules->c1Many1ConsFirstNamesMale[i] == geaCons[0])
						{
							rules->c1Many1ConsFirstNamesMale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
							rules->c2Many1VowelFirstNamesMale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
						}
						else
						{
							rules->c2Many1VowelFirstNamesMale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						}
					}
					if (randomIntRange(0, 1))
					{
						if (bIsVowel2)
						{
							rules->c1Many2VowelFirstNamesMale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
							rules->c2Many2ConsFirstNamesMale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						}
						else
						{
							rules->c1Many2ConsFirstNamesMale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
							if (rules->c1Many2ConsFirstNamesMale[i] == geaCons[0])
							{
								rules->c1Many2ConsFirstNamesMale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
								rules->c2Many2VowelFirstNamesMale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
							}
							else
							{
								rules->c2Many2VowelFirstNamesMale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							}
						}
						if (randomIntRange(0, 1))
						{
							if (bIsVowel3)
							{
								rules->c1Many3VowelFirstNamesMale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
								rules->c2Many3ConsFirstNamesMale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
							}
							else
							{
								rules->c1Many3ConsFirstNamesMale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
								if (rules->c1Many3ConsFirstNamesMale[i] == geaCons[0])
								{
									rules->c1Many3ConsFirstNamesMale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
									rules->c2Many3VowelFirstNamesMale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
								}
								else
								{
									rules->c2Many3VowelFirstNamesMale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
								}
							}
						}
					}
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel1)
					{
						rules->c1Many1VowelFirstNamesFemale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
						rules->c2Many1ConsFirstNamesFemale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
					}
					else
					{
						rules->c1Many1ConsFirstNamesFemale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (rules->c1Many1ConsFirstNamesFemale[i] == geaCons[0])
						{
							rules->c1Many1ConsFirstNamesFemale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
							rules->c2Many1VowelFirstNamesFemale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
						}
						else
						{
							rules->c2Many1VowelFirstNamesFemale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						}
					}
					if (randomIntRange(0, 1))
					{
						if (bIsVowel2)
						{
							rules->c1Many2VowelFirstNamesFemale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
							rules->c2Many2ConsFirstNamesFemale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
						}
						else
						{
							rules->c1Many2ConsFirstNamesFemale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
							if (rules->c1Many2ConsFirstNamesFemale[i] == geaCons[0])
							{
								rules->c1Many2ConsFirstNamesFemale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
								rules->c2Many2VowelFirstNamesFemale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
							}
							else
							{
								rules->c2Many2VowelFirstNamesFemale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							}
						}
						if (randomIntRange(0, 1))
						{
							if (bIsVowel3)
							{
								rules->c1Many3VowelFirstNamesFemale[i] = geaFirstVowels[randomIntRange(0,eaSize(&geaFirstVowels)-1)];
								rules->c2Many3ConsFirstNamesFemale[i] = geaCons[randomIntRange(1,eaSize(&geaCons)-1)];
							}
							else
							{
								rules->c1Many3ConsFirstNamesFemale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
								if (rules->c1Many3ConsFirstNamesFemale[i] == geaCons[0])
								{
									rules->c1Many3ConsFirstNamesFemale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_Q");
									rules->c2Many3VowelFirstNamesFemale[i] = RefSystem_ReferentFromString("PhonemeSet", "Char_U");
								}
								else
								{
									rules->c2Many3VowelFirstNamesFemale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
								}
							}
						}
					}
				}
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b1AllVowelFirstNamesMale = true;
				rules->b1AllConsFirstNamesMale = true;
				rules->b2AllVowelFirstNamesMale = true;
				rules->b2AllConsFirstNamesMale = true;
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b1AllVowelFirstNamesFemale = true;
				rules->b1AllConsFirstNamesFemale = true;
				rules->b2AllVowelFirstNamesFemale = true;
				rules->b2AllConsFirstNamesFemale = true;
			}
			break;
		case 6: //Names end with the same letter
			if (bIsVowel1)
			{
				ps = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
				rules->c3Many1VowelFirstNamesMale[0] = rules->c3Many1VowelFirstNamesMale[1] = rules->c3Many1VowelFirstNamesMale[2] = ps;
				rules->c3Many1VowelFirstNamesFemale[0] = rules->c3Many1VowelFirstNamesFemale[1] = rules->c3Many1VowelFirstNamesFemale[2] = ps;
			}
			else
			{
				ps = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
				rules->c3Many1ConsFirstNamesMale[0] = rules->c3Many1ConsFirstNamesMale[1] = rules->c3Many1ConsFirstNamesMale[2] = ps;
				rules->c3Many1ConsFirstNamesFemale[0] = rules->c3Many1ConsFirstNamesFemale[1] = rules->c3Many1ConsFirstNamesFemale[2] = ps;
			}
			if (randomIntRange(0, 1))
			{
				if (bIsVowel2)
				{
					ps = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					rules->c3Many2VowelFirstNamesMale[0] = rules->c3Many2VowelFirstNamesMale[1] = rules->c3Many2VowelFirstNamesMale[2] = ps;
					rules->c3Many2VowelFirstNamesFemale[0] = rules->c3Many2VowelFirstNamesFemale[1] = rules->c3Many2VowelFirstNamesFemale[2] = ps;
				}
				else
				{
					ps = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					rules->c3Many2ConsFirstNamesMale[0] = rules->c3Many2ConsFirstNamesMale[1] = rules->c3Many2ConsFirstNamesMale[2] = ps;
					rules->c3Many2ConsFirstNamesFemale[0] = rules->c3Many2ConsFirstNamesFemale[1] = rules->c3Many2ConsFirstNamesFemale[2] = ps;
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel3)
					{
						ps = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						rules->c3Many3VowelFirstNamesMale[0] = rules->c3Many3VowelFirstNamesMale[1] = rules->c3Many3VowelFirstNamesMale[2] = ps;
						rules->c3Many3VowelFirstNamesFemale[0] = rules->c3Many3VowelFirstNamesFemale[1] = rules->c3Many3VowelFirstNamesFemale[2] = ps;
					}
					else
					{
						ps = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						rules->c3Many3ConsFirstNamesMale[0] = rules->c3Many3ConsFirstNamesMale[1] = rules->c3Many3ConsFirstNamesMale[2] = ps;
						rules->c3Many3ConsFirstNamesFemale[0] = rules->c3Many3ConsFirstNamesFemale[1] = rules->c3Many3ConsFirstNamesFemale[2] = ps;
					}
				}
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b3AllVowelFirstNamesMale = true;
				rules->b3AllVowelFirstNamesFemale = true;
				rules->b3AllConsFirstNamesMale = true;
				rules->b3AllConsFirstNamesFemale = true;
			}
			break;
		case 7: //Names in gender end with the same letter
			if (randomIntRange(0, 1))
			{
				if (bIsVowel1) rules->c3Many1VowelFirstNamesMale[0] = rules->c3Many1VowelFirstNamesMale[1] = rules->c3Many1VowelFirstNamesMale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
				else rules->c3Many1ConsFirstNamesMale[0] = rules->c3Many1ConsFirstNamesMale[1] = rules->c3Many1ConsFirstNamesMale[2] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
				if (randomIntRange(0, 1))
				{
					if (bIsVowel2) rules->c3Many2VowelFirstNamesMale[0] = rules->c3Many2VowelFirstNamesMale[1] = rules->c3Many2VowelFirstNamesMale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					else rules->c3Many2ConsFirstNamesMale[0] = rules->c3Many2ConsFirstNamesMale[1] = rules->c3Many2ConsFirstNamesMale[2] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel3) rules->c3Many3VowelFirstNamesMale[0] = rules->c3Many3VowelFirstNamesMale[1] = rules->c3Many3VowelFirstNamesMale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						else rules->c3Many3ConsFirstNamesMale[0] = rules->c3Many3ConsFirstNamesMale[1] = rules->c3Many3ConsFirstNamesMale[2] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					}
				}
				if (randomIntRange(0, 99) < 5)
				{
					//Letter is required
					rules->b3AllVowelFirstNamesMale = true;
					rules->b3AllConsFirstNamesMale = true;
				}
			}
			if (randomIntRange(0, 1))
			{
				if (bIsVowel1) rules->c3Many1VowelFirstNamesFemale[0] = rules->c3Many1VowelFirstNamesFemale[1] = rules->c3Many1VowelFirstNamesFemale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
				else rules->c3Many1ConsFirstNamesFemale[0] = rules->c3Many1ConsFirstNamesFemale[1] = rules->c3Many1ConsFirstNamesFemale[2] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
				if (randomIntRange(0, 1))
				{
					if (bIsVowel2) rules->c3Many2VowelFirstNamesFemale[0] = rules->c3Many2VowelFirstNamesFemale[1] = rules->c3Many2VowelFirstNamesFemale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					else rules->c3Many2ConsFirstNamesFemale[0] = rules->c3Many2ConsFirstNamesFemale[1] = rules->c3Many2ConsFirstNamesFemale[2] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel3) rules->c3Many3VowelFirstNamesFemale[0] = rules->c3Many3VowelFirstNamesFemale[1] = rules->c3Many3VowelFirstNamesFemale[2] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						else rules->c3Many3ConsFirstNamesFemale[0] = rules->c3Many3ConsFirstNamesFemale[1] = rules->c3Many3ConsFirstNamesFemale[2] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					}
				}
				if (randomIntRange(0, 99) < 5)
				{
					//Letter is required
					rules->b3AllVowelFirstNamesFemale = true;
					rules->b3AllConsFirstNamesFemale = true;
				}
			}
			break;
		case 8: //Names in sub-gender each end with a different letter
			for (i = 0; i < 3; ++i)
			{
				if (randomIntRange(0, 1))
				{
					if (bIsVowel1) rules->c3Many1VowelFirstNamesMale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					else rules->c3Many1ConsFirstNamesMale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel2) rules->c3Many2VowelFirstNamesMale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						else rules->c3Many2ConsFirstNamesMale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (randomIntRange(0, 1))
						{
							if (bIsVowel3) rules->c3Many3VowelFirstNamesMale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							else rules->c3Many3ConsFirstNamesMale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						}
					}
				}
				if (randomIntRange(0, 1))
				{
					if (bIsVowel1) rules->c3Many1VowelFirstNamesFemale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
					else rules->c3Many1ConsFirstNamesFemale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
					if (randomIntRange(0, 1))
					{
						if (bIsVowel2) rules->c3Many2VowelFirstNamesFemale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
						else rules->c3Many2ConsFirstNamesFemale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						if (randomIntRange(0, 1))
						{
							if (bIsVowel3) rules->c3Many3VowelFirstNamesFemale[i] = geaVowels[randomIntRange(0,eaSize(&geaVowels)-1)];
							else rules->c3Many3ConsFirstNamesFemale[i] = geaCons[randomIntRange(0,eaSize(&geaCons)-1)];
						}
					}
				}
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b3AllVowelFirstNamesMale = true;
				rules->b3AllConsFirstNamesMale = true;
			}
			if (randomIntRange(0, 99) < 5)
			{
				//Letter is required
				rules->b3AllVowelFirstNamesFemale = true;
				rules->b3AllConsFirstNamesFemale = true;
			}
			break;
		}
	}

	if (randomIntRange(0, 1))
	{
		rules->bSize3FirstNamesMale = true;
		rules->bSize4FirstNamesMale = true;
		rules->bSize5FirstNamesMale = true;
		rules->bSize6FirstNamesMale = randomIntRange(0, 99) < 5;
	}
	else
	{
		switch (randomIntRange(0, 3))
		{
		case 0:
			rules->bSize3FirstNamesMale = true;
			rules->bSize4FirstNamesMale = true;
			rules->bSize5FirstNamesMale = true;
			rules->bSize6FirstNamesMale = false;
			break;
		case 1:
			rules->bSize3FirstNamesMale = false;
			rules->bSize4FirstNamesMale = true;
			rules->bSize5FirstNamesMale = true;
			rules->bSize6FirstNamesMale = randomIntRange(0, 99) < 5;
			break;
		case 2:
			rules->bSize3FirstNamesMale = false;
			rules->bSize4FirstNamesMale = false;
			rules->bSize5FirstNamesMale = true;
			rules->bSize6FirstNamesMale = randomIntRange(0, 99) < 5;
			if (!rules->bSize6FirstNamesMale) rules->bSize4FirstNamesMale = true;
			break;
		case 3:
			rules->bSize3FirstNamesMale = true;
			rules->bSize4FirstNamesMale = false;
			rules->bSize5FirstNamesMale = false;
			rules->bSize6FirstNamesMale = randomIntRange(0, 99) < 5;
			if (!rules->bSize6FirstNamesMale) rules->bSize5FirstNamesMale = true;
			break;
		}
	}
	if (rules->bMaleHasLastName)
	{
		if (randomIntRange(0, 1))
		{
			rules->bSize3LastNamesMale = true;
			rules->bSize4LastNamesMale = true;
			rules->bSize5LastNamesMale = true;
			rules->bSize6LastNamesMale = randomIntRange(0, 99) < 5;
		}
		else
		{
			switch (randomIntRange(0, 3))
			{
			case 0:
				rules->bSize3LastNamesMale = true;
				rules->bSize4LastNamesMale = true;
				rules->bSize5LastNamesMale = true;
				rules->bSize6LastNamesMale = false;
				break;
			case 1:
				rules->bSize3LastNamesMale = false;
				rules->bSize4LastNamesMale = true;
				rules->bSize5LastNamesMale = true;
				rules->bSize6LastNamesMale = randomIntRange(0, 99) < 5;
				break;
			case 2:
				rules->bSize3LastNamesMale = false;
				rules->bSize4LastNamesMale = false;
				rules->bSize5LastNamesMale = true;
				rules->bSize6LastNamesMale = randomIntRange(0, 99) < 5;
				if (!rules->bSize6LastNamesMale) rules->bSize4LastNamesMale = true;
				break;
			case 3:
				rules->bSize3LastNamesMale = true;
				rules->bSize4LastNamesMale = false;
				rules->bSize5LastNamesMale = false;
				rules->bSize6LastNamesMale = true;
				rules->bSize6LastNamesMale = randomIntRange(0, 99) < 5;
				if (!rules->bSize6LastNamesMale) rules->bSize5LastNamesMale = true;
				break;
			}
		}
	}
	else
	{
		rules->bSize3LastNamesMale = false;
		rules->bSize4LastNamesMale = false;
		rules->bSize5LastNamesMale = false;
		rules->bSize6LastNamesMale = false;
	}

	if (randomIntRange(0, 1))
	{
		rules->bSize3FirstNamesFemale = true;
		rules->bSize4FirstNamesFemale = true;
		rules->bSize5FirstNamesFemale = true;
		rules->bSize6FirstNamesFemale = randomIntRange(0, 99) < 5;
	}
	else
	{
		switch (randomIntRange(0, 3))
		{
		case 0:
			rules->bSize3FirstNamesFemale = true;
			rules->bSize4FirstNamesFemale = true;
			rules->bSize5FirstNamesFemale = true;
			rules->bSize6FirstNamesFemale = false;
			break;
		case 1:
			rules->bSize3FirstNamesFemale = false;
			rules->bSize4FirstNamesFemale = true;
			rules->bSize5FirstNamesFemale = true;
			rules->bSize6FirstNamesFemale = randomIntRange(0, 99) < 5;
			break;
		case 2:
			rules->bSize3FirstNamesFemale = false;
			rules->bSize4FirstNamesFemale = false;
			rules->bSize5FirstNamesFemale = true;
			rules->bSize6FirstNamesFemale = randomIntRange(0, 99) < 5;
			if (!rules->bSize6FirstNamesFemale) rules->bSize4FirstNamesFemale = true;
			break;
		case 3:
			rules->bSize3FirstNamesFemale = true;
			rules->bSize4FirstNamesFemale = false;
			rules->bSize5FirstNamesFemale = false;
			rules->bSize6FirstNamesFemale = randomIntRange(0, 99) < 5;
			if (!rules->bSize6FirstNamesFemale) rules->bSize5FirstNamesFemale = true;
			break;
		}
	}
	if (rules->bFemaleHasLastName)
	{
		if (randomIntRange(0, 1))
		{
			rules->bSize3LastNamesFemale = true;
			rules->bSize4LastNamesFemale = true;
			rules->bSize5LastNamesFemale = true;
			rules->bSize6LastNamesFemale = randomIntRange(0, 99) < 5;
		}
		else
		{
			switch (randomIntRange(0, 3))
			{
			case 0:
				rules->bSize3LastNamesFemale = true;
				rules->bSize4LastNamesFemale = true;
				rules->bSize5LastNamesFemale = true;
				rules->bSize6LastNamesFemale = false;
				break;
			case 1:
				rules->bSize3LastNamesFemale = false;
				rules->bSize4LastNamesFemale = true;
				rules->bSize5LastNamesFemale = true;
				rules->bSize6LastNamesFemale = randomIntRange(0, 99) < 5;
				break;
			case 2:
				rules->bSize3LastNamesFemale = false;
				rules->bSize4LastNamesFemale = false;
				rules->bSize5LastNamesFemale = true;
				rules->bSize6LastNamesFemale = randomIntRange(0, 99) < 5;
				if (!rules->bSize6LastNamesFemale) rules->bSize4LastNamesFemale = true;
				break;
			case 3:
				rules->bSize3LastNamesFemale = true;
				rules->bSize4LastNamesFemale = false;
				rules->bSize5LastNamesFemale = false;
				rules->bSize6LastNamesFemale = randomIntRange(0, 99) < 5;
				if (!rules->bSize6LastNamesFemale) rules->bSize5LastNamesFemale = true;
				break;
			}
		}
	}
	else
	{
		rules->bSize3LastNamesFemale = false;
		rules->bSize4LastNamesFemale = false;
		rules->bSize5LastNamesFemale = false;
		rules->bSize6LastNamesFemale = false;
	}
}

static int speciesgen_AddPhonemeToNameTemplate(NameTemplateNoRef *pNameTemplate, NOCONST(PhonemeSet) ***peaDefault, NOCONST(PhonemeSet) *Many1, NOCONST(PhonemeSet) *Many2, NOCONST(PhonemeSet) *Many3, bool required)
{
	int iRand;
	NOCONST(PhonemeSet) *ps = NULL;
	if (Many1 || Many2 || Many3)
	{
		if (Many1 && randomIntRange(0, 1)) ps = Many1;
		else if (Many2 && randomIntRange(0, 1)) ps = Many2;
		else if (Many3 && randomIntRange(0, 1)) ps = Many3;
		else if (required && Many1) ps = Many1;
		else if (required && Many2) ps = Many2;
		else if (required && Many3) ps = Many3;
	}
	else if (required) return 0;
	if (!ps)
	{
		iRand = randomIntRange(0, eaSize(peaDefault)-1);
		if (*peaDefault) ps = (*peaDefault)[iRand];
	}
	if (ps)
	{
		eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)ps);
		return eaSize(&ps->pcPhonemes);
	}
	return 0;
}

static void speciesgen_GenerateNameRules(NOCONST(SpeciesDef) *newSpecies, SpeciesGenRules *rules)
{
	char text[256];
	int i, j, count, iRand, iTemp, iNum, iRemoved = -1;
	NOCONST(PhonemeSet) *tempset = NULL;
	NOCONST(PhonemeSet) ***peaSameGender = NULL;
	NOCONST(PhonemeSet) ***peaCurrentGender = NULL;
	NameTemplateListNoRef **ppFirstNameRules = NULL;
	NameTemplateListNoRef **ppLastNameRules = NULL;
	NameTemplateNoRef *pNameTemplate = NULL;
	unsigned long iTotalCombinations = 0, iCombinations;

	//
	//Make PhonemeSets
	//

	if (!eaSize(rules->peaPhonemeVowels))
	{
		//Create species specific PhonemeSets for vowels
		bool hasMonophthong = true, hasDipthong = true, hasTriphthong = false;

		if (randomIntRange(0, 99) < 85)
		{
			hasMonophthong = true;
			hasDipthong = true;
			hasTriphthong = randomIntRange(0, 99) < 2;
		}
		else
		{
			if (randomIntRange(0, 99) < 50)
			{
				hasMonophthong = true;
				hasDipthong = false;
				hasTriphthong = false;
			}
			else if (randomIntRange(0, 99) < 50)
			{
				hasMonophthong = false;
				hasDipthong = true;
				hasTriphthong = false;
			}
			else if (randomIntRange(0, 99) < 5)
			{
				hasMonophthong = true;
				hasDipthong = false;
				hasTriphthong = true;
			}
			else if (randomIntRange(0, 99) < 5)
			{
				hasMonophthong = false;
				hasDipthong = true;
				hasTriphthong = true;
			}
		}

		if (hasMonophthong)
		{
			tempset = speciesgen_CreatePhonemeSet("_Monophthong", rules->speciesName, "a,e,i,o,u,oo,ea,ir", 3, -1);
			if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
			if (hasDipthong)
			{
				tempset = speciesgen_CreatePhonemeSet("_Monophthong2", rules->speciesName, "a,e,i,o,u,oo,ea,ir", 3, -1);
				if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
			}
			if (hasTriphthong)
			{
				tempset = speciesgen_CreatePhonemeSet("_Monophthong3", rules->speciesName, "a,e,i,o,u,oo,ea,ir", 3, -1);
				if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
				tempset = speciesgen_CreatePhonemeSet("_Monophthong4", rules->speciesName, "a,e,i,o,u,oo,ea,ir", 3, -1);
				if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
			}
		}

		if (hasDipthong)
		{
			tempset = speciesgen_CreatePhonemeSet("_Dipthong", rules->speciesName, "ay,ea,ee,ei,oa,oe,ou,ow,oy,y", 6, -1);
			if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
			if (hasTriphthong)
			{
				tempset = speciesgen_CreatePhonemeSet("_Dipthong2", rules->speciesName, "ay,ea,ee,ei,oa,oe,ou,ow,oy,y", 6, -1);
				if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
			}
		}

		if (hasTriphthong)
		{
			tempset = speciesgen_CreatePhonemeSet("_Triphthong", rules->speciesName, "our,ire,ayer,oyal,ower", 3, -1);
			if (tempset) eaPush(rules->peaPhonemeVowels, tempset);
		}
	}

	if (!eaSize(rules->peaPhonemeCons))
	{
		//Create species specific PhonemeSets for consonants
		bool hasStop = true, hasFricative = true, hasAffricate = true, hasNasal = true, hasLiquid = true, hasGlide = true;
		bool hasAlveolar = true, hasBilabial = true, hasGlottal = true, hasPalatal = true, hasVelar = true;

		speciesgen_GenerateNameRulesToggles(rules);

		iRand = randomIntRange(0, 5);
		for (i = 0; i < iRand; ++i)
		{
			switch (randomIntRange(0, 10))
			{
			case 0: if (hasStop) hasStop = false; else --i; break;
			case 1: if (hasFricative) hasFricative = false; else --i; break;
			case 2: if (hasAffricate) hasAffricate = false; else --i; break;
			case 3: if (hasNasal) hasNasal = false; else --i; break;
			case 4: if (hasLiquid) hasLiquid = false; else --i; break;
			case 5: if (hasGlide) hasGlide = false; else --i; break;
			case 6: if (hasAlveolar) hasAlveolar = false; else --i; break;
			case 7: if (hasBilabial) hasBilabial = false; else --i; break;
			case 8: if (hasGlottal) hasGlottal = false; else --i; break;
			case 9: if (hasPalatal) hasPalatal = false; else --i; break;
			case 10: if (hasVelar) hasVelar = false; else --i; break;
			}
		}

		if (hasStop)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[0].name, rules->speciesName, gPhonemeConsList[0].phonemes, gPhonemeConsList[0].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasFricative)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[1].name, rules->speciesName, gPhonemeConsList[1].phonemes, gPhonemeConsList[1].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasAffricate)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[2].name, rules->speciesName, gPhonemeConsList[2].phonemes, gPhonemeConsList[2].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasNasal)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[3].name, rules->speciesName, gPhonemeConsList[3].phonemes, gPhonemeConsList[3].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasLiquid)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[4].name, rules->speciesName, gPhonemeConsList[4].phonemes, gPhonemeConsList[4].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasGlide)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[5].name, rules->speciesName, gPhonemeConsList[5].phonemes, gPhonemeConsList[5].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasAlveolar)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[6].name, rules->speciesName, gPhonemeConsList[6].phonemes, gPhonemeConsList[6].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasBilabial)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[7].name, rules->speciesName, gPhonemeConsList[7].phonemes, gPhonemeConsList[7].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasGlottal)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[8].name, rules->speciesName, gPhonemeConsList[8].phonemes, gPhonemeConsList[8].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasPalatal)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[9].name, rules->speciesName, gPhonemeConsList[9].phonemes, gPhonemeConsList[9].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}

		if (hasVelar)
		{
			tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[10].name, rules->speciesName, gPhonemeConsList[10].phonemes, gPhonemeConsList[10].removeCount, -1);
			if (tempset) eaPush(rules->peaPhonemeCons, tempset);
		}
	}

	if (rules->eGender == Gender_Male)
	{
		switch (rules->genderIndex)
		{
		case 0:	peaCurrentGender = &rules->eaGender1; ppFirstNameRules = &rules->firstNameRules1; ppLastNameRules = &rules->lastNameRules1; break;
		case 1: peaCurrentGender = &rules->eaGender2; ppFirstNameRules = &rules->firstNameRules2; ppLastNameRules = &rules->lastNameRules2; break;
		case 2: peaCurrentGender = &rules->eaGender3; ppFirstNameRules = &rules->firstNameRules3; ppLastNameRules = &rules->lastNameRules3; break;
		}
		if (rules->genderIndex > 0)
		{
			peaSameGender = &rules->eaGender1;
		}
	}
	else
	{
		switch (rules->genderIndex)
		{
		case 0:
			switch (rules->numMales)
			{
			case 0:	peaCurrentGender = &rules->eaGender1; ppFirstNameRules = &rules->firstNameRules1; ppLastNameRules = &rules->lastNameRules1; break;
			case 1: peaCurrentGender = &rules->eaGender2; ppFirstNameRules = &rules->firstNameRules2; ppLastNameRules = &rules->lastNameRules2; break;
			case 2: peaCurrentGender = &rules->eaGender3; ppFirstNameRules = &rules->firstNameRules3; ppLastNameRules = &rules->lastNameRules3; break;
			case 3: peaCurrentGender = &rules->eaGender4; ppFirstNameRules = &rules->firstNameRules4; ppLastNameRules = &rules->lastNameRules4; break;
			}
			break;
		case 1:
			switch (rules->numMales)
			{
			case 0: peaCurrentGender = &rules->eaGender2; peaSameGender = &rules->eaGender1; ppFirstNameRules = &rules->firstNameRules2; ppLastNameRules = &rules->lastNameRules2; break;
			case 1: peaCurrentGender = &rules->eaGender3; peaSameGender = &rules->eaGender2; ppFirstNameRules = &rules->firstNameRules3; ppLastNameRules = &rules->lastNameRules3; break;
			case 2: peaCurrentGender = &rules->eaGender4; peaSameGender = &rules->eaGender3; ppFirstNameRules = &rules->firstNameRules4; ppLastNameRules = &rules->lastNameRules4; break;
			}
			break;
		case 2:
			switch (rules->numMales)
			{
			case 0: peaCurrentGender = &rules->eaGender3; peaSameGender = &rules->eaGender2; ppFirstNameRules = &rules->firstNameRules3; ppLastNameRules = &rules->lastNameRules3; break;
			case 1: peaCurrentGender = &rules->eaGender4; peaSameGender = &rules->eaGender3; ppFirstNameRules = &rules->firstNameRules4; ppLastNameRules = &rules->lastNameRules4; break;
			}
			break;
		}
	}

	if (peaCurrentGender)
	{
		if (peaSameGender && !rules->bDiffPhonemeByGender)
		{
			eaCopy(peaCurrentGender, peaSameGender);
		}
		else if (eaSize(&rules->eaGender1) && !rules->bDiffPhonemeByAll)
		{
			eaCopy(peaCurrentGender, &rules->eaGender1);
		}
		else
		{
			//This gender has slightly different nameing rules
			eaCopy(peaCurrentGender, rules->peaPhonemeCons);

			//Choose up to 1 Con to remove
			if (randomIntRange(0, 1))
			{
				iRand = randomIntRange(0, eaSize(peaCurrentGender)-1);
				if (*peaCurrentGender && (*peaCurrentGender)[iRand])
				{
					iRemoved = speciesgen_GetConsTypeByName((*peaCurrentGender)[iRand]->pcName);
				}
				eaRemove(peaCurrentGender, iRand);
			}

			//Choose up to 4 Cons to modify
			count = randomIntRange(0, 4);
			for (i = 0; i < count; ++i)
			{
				iRand = randomIntRange(0, eaSize(peaCurrentGender)-1);
				iTemp = speciesgen_GetConsTypeByName((*peaCurrentGender)[iRand]->pcName);
				if (iTemp >= 0)
				{
					if (gPhonemeConsList[iTemp].removeCount >= 1)
					{
						iNum = eaSize(&(*peaCurrentGender)[iRand]->pcPhonemes);
						eaRemove(peaCurrentGender, iRand);
						tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[iTemp].name, newSpecies->pcName, gPhonemeConsList[iTemp].phonemes, gPhonemeConsList[iTemp].removeCount, iNum);
						if (tempset)
						{
							eaPush(peaCurrentGender, tempset);
							eaPush(&rules->eaGenderOwner, tempset);
						}
					}
				}
			}

			//Choose up to 2 Cons to add
			if (eaSize(peaCurrentGender) + 3 <= sizeof(gPhonemeConsList)/sizeof(PhonemeConsList))
				count = randomIntRange(0, 2);
			else if (eaSize(peaCurrentGender) + 2 <= sizeof(gPhonemeConsList)/sizeof(PhonemeConsList))
				count = randomIntRange(0, 1);
			else
				count = 0;

			if (count)
			{
				int notThere[sizeof(gPhonemeConsList)/sizeof(PhonemeConsList)];
				for (i = 0; i < sizeof(gPhonemeConsList)/sizeof(PhonemeConsList); ++i)
				{
					notThere[i] = iRemoved == i ? -1 : i;
				}
				for (i = 0; i < eaSize(peaCurrentGender); ++i)
				{
					iTemp = speciesgen_GetConsTypeByName((*peaCurrentGender)[i]->pcName);
					if (iTemp) notThere[iTemp] = -1;
				}
				for (i = 0; i < count; ++i)
				{
					iTemp = (sizeof(gPhonemeConsList)/sizeof(PhonemeConsList)) - eaSize(peaCurrentGender) - 1 - (iRemoved >= 0 ? 1 : 0);
					if (iTemp == 0) iRand = 0;
					else iRand = randomIntRange(0, iTemp);

					for (j = 0; j < sizeof(gPhonemeConsList)/sizeof(PhonemeConsList); ++j)
					{
						if (notThere[j] >= 0)
						{
							if (iRand-- <= 0)
							{
								break;
							}
						}
					}

					tempset = speciesgen_CreatePhonemeSet(gPhonemeConsList[notThere[j]].name, newSpecies->pcName, gPhonemeConsList[notThere[j]].phonemes, gPhonemeConsList[notThere[j]].removeCount, -1);
					if (tempset)
					{
						notThere[j] = -1;
						eaPush(peaCurrentGender, tempset);
						eaPush(&rules->eaGenderOwner, tempset);
					}
				}
			}
		}
	}

	//
	//Now make NameTemplateLists
	//

	*ppFirstNameRules = StructCreate(parse_NameTemplateListNoRef);
	(*ppFirstNameRules)->pcName = allocAddString(newSpecies->pcName);
	if ((rules->eGender == Gender_Male && rules->bMaleHasLastName) || (rules->eGender == Gender_Female && rules->bFemaleHasLastName))
	{
		*text = '\0';
		strcat(text, newSpecies->pcName);
		strcat(text, "_Last");
		*ppLastNameRules = StructCreate(parse_NameTemplateListNoRef);
		(*ppLastNameRules)->pcName = allocAddString(text);
	}

	iTotalCombinations = 0;

	if (rules->eGender == Gender_Male)
	{
		if (rules->bSize3FirstNamesMale)
		{
			if ((!rules->bAllApostropheFirstNamesMale) && (!rules->bAllDashFirstNamesMale))
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					//Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}
			}
		}

		if (rules->bSize4FirstNamesMale)
		{
			j = randomIntRange(3, 5);
			for (i = 0; i < j; ++i)
			{
				if ((!rules->bAllApostropheFirstNamesMale) && (!rules->bAllDashFirstNamesMale))
				{
					//Vowel, Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}

				if (rules->bHasApostropheFirstNamesMale)
				{
					tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

					if (rules->bPosMarkFirstNamesMale[0] && (!rules->c2Many1VowelFirstNamesMale[rules->genderIndex]) && (!rules->c2Many1ConsFirstNamesMale[rules->genderIndex]))
					{
						//Cons, Apostrophe, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Apostrophe, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[2])
					{
						//Cons, Vowel, Cons, Apostrophe, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
				else if (rules->bHasDashFirstNamesMale)
				{
					if (rules->bDashIsASpace && rules->bMaleHasLastName)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Space");
					}
					else
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");
					}

					//Vowel, Cons, Dash, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
					if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 50.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Dash, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
					if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 10.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Vowel, Cons, Dash, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
					if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 50.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}
			}
		}

		if (rules->bSize5FirstNamesMale)
		{
			j = randomIntRange(3, 5);
			for (i = 0; i < j; ++i)
			{
				if ((!rules->bAllApostropheFirstNamesMale) && (!rules->bAllDashFirstNamesMale))
				{
					//Vowel, Cons, Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					pNameTemplate->fWeight /= 9.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 9.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}

				if (rules->bHasApostropheFirstNamesMale)
				{
					tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

					if (rules->bPosMarkFirstNamesMale[0] && (!rules->c2Many1VowelFirstNamesMale[rules->genderIndex]) && (!rules->c2Many1ConsFirstNamesMale[rules->genderIndex]))
					{
						//Cons, Apostrophe, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Apostrophe, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Cons, Vowel, Cons, Apostrophe, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[2])
					{
						//Vowel, Cons, Vowel, Cons, Apostrophe, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
				else if (rules->bHasDashFirstNamesMale)
				{
					if (rules->bDashIsASpace && rules->bMaleHasLastName)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Space");
					}
					else
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");
					}

					if (rules->bPosMarkFirstNamesMale[0] || rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[0] || rules->bPosMarkFirstNamesMale[1])
					{
						//Cons, Vowel, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[0] || rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Dash, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1] || rules->bPosMarkFirstNamesMale[2])
					{
						//Cons, Vowel, Cons, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1] || rules->bPosMarkFirstNamesMale[2])
					{
						//Vowel, Cons, Vowel, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1] || rules->bPosMarkFirstNamesMale[2])
					{
						//Cons, Vowel, Cons, Dash, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}
		}

		if (rules->bSize6FirstNamesMale)
		{
			j = randomIntRange(3, 5);
			for (i = 0; i < j; ++i)
			{
				if ((!rules->bAllApostropheFirstNamesMale) && (!rules->bAllDashFirstNamesMale))
				{
					//Vowel, Cons, Vowel, Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					pNameTemplate->fWeight /= 27.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons, Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 27.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}

				if (rules->bHasApostropheFirstNamesMale)
				{
					tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

					if (rules->bPosMarkFirstNamesMale[0] && (!rules->c2Many1VowelFirstNamesMale[rules->genderIndex]) && (!rules->c2Many1ConsFirstNamesMale[rules->genderIndex]))
					{
						//Cons, Apostrophe, Cons, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Apostrophe, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Cons, Vowel, Cons, Apostrophe, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Vowel, Cons, Apostrophe, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[2])
					{
						//Cons, Vowel, Cons, Vowel, Cons, Apostrophe, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
				else if (rules->bHasDashFirstNamesMale)
				{
					if (rules->bDashIsASpace && rules->bMaleHasLastName)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Space");
					}
					else
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");
					}

					if (rules->bPosMarkFirstNamesMale[0])
					{
						//Vowel, Cons, Dash, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[0])
					{
						//Cons, Vowel, Dash, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[0])
					{
						//Vowel, Cons, Dash, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Cons, Vowel, Cons, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Vowel, Cons, Vowel, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[1])
					{
						//Cons, Vowel, Cons, Dash, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[2])
					{
						//Vowel, Cons, Vowel, Cons, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[2])
					{
						//Cons, Vowel, Cons, Vowel, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsFirstNamesMale[rules->genderIndex], rules->b1AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesMale[rules->genderIndex], rules->c2Many2VowelFirstNamesMale[rules->genderIndex], rules->c2Many3VowelFirstNamesMale[rules->genderIndex], rules->b2AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesMale[rules->genderIndex], rules->c3Many2VowelFirstNamesMale[rules->genderIndex], rules->c3Many3VowelFirstNamesMale[rules->genderIndex], rules->b3AllVowelFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesMale[2])
					{
						//Vowel, Cons, Vowel, Cons, Dash, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesMale[rules->genderIndex], rules->c1Many2VowelFirstNamesMale[rules->genderIndex], rules->c1Many3VowelFirstNamesMale[rules->genderIndex], rules->b1AllVowelFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesMale[rules->genderIndex], rules->c2Many2ConsFirstNamesMale[rules->genderIndex], rules->c2Many3ConsFirstNamesMale[rules->genderIndex], rules->b2AllConsFirstNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesMale[rules->genderIndex], rules->c3Many2ConsFirstNamesMale[rules->genderIndex], rules->c3Many3ConsFirstNamesMale[rules->genderIndex], rules->b3AllConsFirstNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}
		}

		if (rules->bMaleHasLastName)
		{
			if (rules->bSize3LastNamesMale)
			{
				if ((!rules->bAllApostropheLastNamesMale) && (!rules->bAllDashLastNamesMale))
				{
					j = randomIntRange(3, 5);
					for (i = 0; i < j; ++i)
					{
						//Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}

			if (rules->bSize4LastNamesMale)
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					if ((!rules->bAllApostropheLastNamesMale) && (!rules->bAllDashLastNamesMale))
					{
						//Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bHasApostropheLastNamesMale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

						if (rules->bPosMarkLastNamesMale[0] && (!rules->c2Many1VowelLastNamesMale[rules->genderIndex]) && (!rules->c2Many1ConsLastNamesMale[rules->genderIndex]))
						{
							//Cons, Apostrophe, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 3.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Apostrophe, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 3.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[2])
						{
							//Cons, Vowel, Cons, Apostrophe, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 3.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
					else if (rules->bHasDashLastNamesMale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");

						//Vowel, Cons, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Vowel, Cons, Dash, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}

			if (rules->bSize5LastNamesMale)
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					if ((!rules->bAllApostropheLastNamesMale) && (!rules->bAllDashLastNamesMale))
					{
						//Vowel, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bHasApostropheLastNamesMale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

						if (rules->bPosMarkLastNamesMale[0] && (!rules->c2Many1VowelLastNamesMale[rules->genderIndex]) && (!rules->c2Many1ConsLastNamesMale[rules->genderIndex]))
						{
							//Cons, Apostrophe, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Apostrophe, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Cons, Vowel, Cons, Apostrophe, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[2])
						{
							//Vowel, Cons, Vowel, Cons, Apostrophe, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
					else if (rules->bHasDashLastNamesMale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");

						if (rules->bPosMarkLastNamesMale[0] || rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[0] || rules->bPosMarkLastNamesMale[1])
						{
							//Cons, Vowel, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[0] || rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Dash, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1] || rules->bPosMarkLastNamesMale[2])
						{
							//Cons, Vowel, Cons, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1] || rules->bPosMarkLastNamesMale[2])
						{
							//Vowel, Cons, Vowel, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1] || rules->bPosMarkLastNamesMale[2])
						{
							//Cons, Vowel, Cons, Dash, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsFirstNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
				}
			}

			if (rules->bSize6LastNamesMale)
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					if ((!rules->bAllApostropheLastNamesMale) && (!rules->bAllDashLastNamesMale))
					{
						//Vowel, Cons, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bHasApostropheLastNamesMale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

						if (rules->bPosMarkLastNamesMale[0] && (!rules->c2Many1VowelLastNamesMale[rules->genderIndex]) && (!rules->c2Many1ConsLastNamesMale[rules->genderIndex]))
						{
							//Cons, Apostrophe, Cons, Vowel, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Apostrophe, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Cons, Vowel, Cons, Apostrophe, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Vowel, Cons, Apostrophe, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[2])
						{
							//Cons, Vowel, Cons, Vowel, Cons, Apostrophe, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
					else if (rules->bHasDashLastNamesMale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");

						if (rules->bPosMarkLastNamesMale[0])
						{
							//Vowel, Cons, Dash, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[0])
						{
							//Cons, Vowel, Dash, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[0])
						{
							//Vowel, Cons, Dash, Vowel, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Cons, Vowel, Cons, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Vowel, Cons, Vowel, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[1])
						{
							//Cons, Vowel, Cons, Dash, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[2])
						{
							//Vowel, Cons, Vowel, Cons, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[2])
						{
							//Cons, Vowel, Cons, Vowel, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesMale[rules->genderIndex], rules->c1Many2ConsLastNamesMale[rules->genderIndex], rules->c1Many3ConsLastNamesMale[rules->genderIndex], rules->b1AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesMale[rules->genderIndex], rules->c2Many2VowelLastNamesMale[rules->genderIndex], rules->c2Many3VowelLastNamesMale[rules->genderIndex], rules->b2AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesMale[rules->genderIndex], rules->c3Many2VowelLastNamesMale[rules->genderIndex], rules->c3Many3VowelLastNamesMale[rules->genderIndex], rules->b3AllVowelLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesMale[2])
						{
							//Vowel, Cons, Vowel, Cons, Dash, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesMale[rules->genderIndex], rules->c1Many2VowelLastNamesMale[rules->genderIndex], rules->c1Many3VowelLastNamesMale[rules->genderIndex], rules->b1AllVowelLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesMale[rules->genderIndex], rules->c2Many2ConsLastNamesMale[rules->genderIndex], rules->c2Many3ConsLastNamesMale[rules->genderIndex], rules->b2AllConsLastNamesMale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesMale[rules->genderIndex], rules->c3Many2ConsLastNamesMale[rules->genderIndex], rules->c3Many3ConsLastNamesMale[rules->genderIndex], rules->b3AllConsLastNamesMale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
				}
			}
		}
	}
	else
	{
		if (rules->bSize3FirstNamesFemale)
		{
			if ((!rules->bAllApostropheFirstNamesFemale) && (!rules->bAllDashFirstNamesFemale))
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					//Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}
			}
		}

		if (rules->bSize4FirstNamesFemale)
		{
			j = randomIntRange(3, 5);
			for (i = 0; i < j; ++i)
			{
				if ((!rules->bAllApostropheFirstNamesFemale) && (!rules->bAllDashFirstNamesFemale))
				{
					//Vowel, Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}

				if (rules->bHasApostropheFirstNamesFemale)
				{
					tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

					if (rules->bPosMarkFirstNamesFemale[0] && (!rules->c2Many1VowelFirstNamesFemale[rules->genderIndex]) && (!rules->c2Many1ConsFirstNamesFemale[rules->genderIndex]))
					{
						//Cons, Apostrophe, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Apostrophe, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[2])
					{
						//Cons, Vowel, Cons, Apostrophe, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
				else if (rules->bHasDashFirstNamesFemale)
				{
					if (rules->bDashIsASpace && rules->bFemaleHasLastName)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Space");
					}
					else
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");
					}

					//Vowel, Cons, Dash, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
					if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 50.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Dash, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
					if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 10.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Vowel, Cons, Dash, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
					if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 50.0f;
					pNameTemplate->fWeight /= 3.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}
			}
		}

		if (rules->bSize5FirstNamesFemale)
		{
			j = randomIntRange(3, 5);
			for (i = 0; i < j; ++i)
			{
				if ((!rules->bAllApostropheFirstNamesFemale) && (!rules->bAllDashFirstNamesFemale))
				{
					//Vowel, Cons, Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					pNameTemplate->fWeight /= 9.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 9.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}

				if (rules->bHasApostropheFirstNamesFemale)
				{
					tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

					if (rules->bPosMarkFirstNamesFemale[0] && (!rules->c2Many1VowelFirstNamesFemale[rules->genderIndex]) && (!rules->c2Many1ConsFirstNamesFemale[rules->genderIndex]))
					{
						//Cons, Apostrophe, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Apostrophe, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Cons, Vowel, Cons, Apostrophe, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[2])
					{
						//Vowel, Cons, Vowel, Cons, Apostrophe, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
				else if (rules->bHasDashFirstNamesFemale)
				{
					if (rules->bDashIsASpace && rules->bFemaleHasLastName)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Space");
					}
					else
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");
					}

					if (rules->bPosMarkFirstNamesFemale[0] || rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[0] || rules->bPosMarkFirstNamesFemale[1])
					{
						//Cons, Vowel, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[0] || rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Dash, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1] || rules->bPosMarkFirstNamesFemale[2])
					{
						//Cons, Vowel, Cons, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1] || rules->bPosMarkFirstNamesFemale[2])
					{
						//Vowel, Cons, Vowel, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1] || rules->bPosMarkFirstNamesFemale[2])
					{
						//Cons, Vowel, Cons, Dash, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}
		}

		if (rules->bSize6FirstNamesFemale)
		{
			j = randomIntRange(3, 5);
			for (i = 0; i < j; ++i)
			{
				if ((!rules->bAllApostropheFirstNamesFemale) && (!rules->bAllDashFirstNamesFemale))
				{
					//Vowel, Cons, Vowel, Cons, Vowel, Cons
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 5.0f;
					pNameTemplate->fWeight /= 27.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

					//Cons, Vowel, Cons, Vowel, Cons, Vowel
					pNameTemplate = StructCreate(parse_NameTemplateNoRef);
					iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
					iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
					iTotalCombinations += iCombinations;
					pNameTemplate->fWeight = iCombinations;
					pNameTemplate->fWeight /= 27.0f;
					if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
				}

				if (rules->bHasApostropheFirstNamesFemale)
				{
					tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

					if (rules->bPosMarkFirstNamesFemale[0] && (!rules->c2Many1VowelFirstNamesFemale[rules->genderIndex]) && (!rules->c2Many1ConsFirstNamesFemale[rules->genderIndex]))
					{
						//Cons, Apostrophe, Cons, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Apostrophe, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Cons, Vowel, Cons, Apostrophe, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Vowel, Cons, Apostrophe, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[2])
					{
						//Cons, Vowel, Cons, Vowel, Cons, Apostrophe, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
				else if (rules->bHasDashFirstNamesFemale)
				{
					if (rules->bDashIsASpace && rules->bFemaleHasLastName)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Space");
					}
					else
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");
					}

					if (rules->bPosMarkFirstNamesFemale[0])
					{
						//Vowel, Cons, Dash, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[0])
					{
						//Cons, Vowel, Dash, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[0])
					{
						//Vowel, Cons, Dash, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Cons, Vowel, Cons, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Vowel, Cons, Vowel, Dash, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[1])
					{
						//Cons, Vowel, Cons, Dash, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[2])
					{
						//Vowel, Cons, Vowel, Cons, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[2])
					{
						//Cons, Vowel, Cons, Vowel, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsFirstNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsFirstNamesFemale[rules->genderIndex], rules->b1AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelFirstNamesFemale[rules->genderIndex], rules->c2Many2VowelFirstNamesFemale[rules->genderIndex], rules->c2Many3VowelFirstNamesFemale[rules->genderIndex], rules->b2AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelFirstNamesFemale[rules->genderIndex], rules->c3Many2VowelFirstNamesFemale[rules->genderIndex], rules->c3Many3VowelFirstNamesFemale[rules->genderIndex], rules->b3AllVowelFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bPosMarkFirstNamesFemale[2])
					{
						//Vowel, Cons, Vowel, Cons, Dash, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelFirstNamesFemale[rules->genderIndex], rules->c1Many2VowelFirstNamesFemale[rules->genderIndex], rules->c1Many3VowelFirstNamesFemale[rules->genderIndex], rules->b1AllVowelFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsFirstNamesFemale[rules->genderIndex], rules->c2Many2ConsFirstNamesFemale[rules->genderIndex], rules->c2Many3ConsFirstNamesFemale[rules->genderIndex], rules->b2AllConsFirstNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsFirstNamesFemale[rules->genderIndex], rules->c3Many2ConsFirstNamesFemale[rules->genderIndex], rules->c3Many3ConsFirstNamesFemale[rules->genderIndex], rules->b3AllConsFirstNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppFirstNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}
		}

		if (rules->bFemaleHasLastName)
		{
			if (rules->bSize3LastNamesFemale)
			{
				if ((!rules->bAllApostropheLastNamesFemale) && (!rules->bAllDashLastNamesFemale))
				{
					j = randomIntRange(3, 5);
					for (i = 0; i < j; ++i)
					{
						//Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}

			if (rules->bSize4LastNamesFemale)
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					if ((!rules->bAllApostropheLastNamesFemale) && (!rules->bAllDashLastNamesFemale))
					{
						//Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bHasApostropheLastNamesFemale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

						if (rules->bPosMarkLastNamesFemale[0] && (!rules->c2Many1VowelLastNamesFemale[rules->genderIndex]) && (!rules->c2Many1ConsLastNamesFemale[rules->genderIndex]))
						{
							//Cons, Apostrophe, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 3.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Apostrophe, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 3.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[2])
						{
							//Cons, Vowel, Cons, Apostrophe, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 3.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
					else if (rules->bHasDashLastNamesFemale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");

						//Vowel, Cons, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Dash, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 10.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Vowel, Cons, Dash, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
						if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 50.0f;
						pNameTemplate->fWeight /= 3.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}
				}
			}

			if (rules->bSize5LastNamesFemale)
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					if ((!rules->bAllApostropheLastNamesFemale) && (!rules->bAllDashLastNamesFemale))
					{
						//Vowel, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 9.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bHasApostropheLastNamesFemale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

						if (rules->bPosMarkLastNamesFemale[0] && (!rules->c2Many1VowelLastNamesFemale[rules->genderIndex]) && (!rules->c2Many1ConsLastNamesFemale[rules->genderIndex]))
						{
							//Cons, Apostrophe, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Apostrophe, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Cons, Vowel, Cons, Apostrophe, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[2])
						{
							//Vowel, Cons, Vowel, Cons, Apostrophe, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
					else if (rules->bHasDashLastNamesFemale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");

						if (rules->bPosMarkLastNamesFemale[0] || rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[0] || rules->bPosMarkLastNamesFemale[1])
						{
							//Cons, Vowel, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[0] || rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Dash, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1] || rules->bPosMarkLastNamesFemale[2])
						{
							//Cons, Vowel, Cons, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1] || rules->bPosMarkLastNamesFemale[2])
						{
							//Vowel, Cons, Vowel, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1] || rules->bPosMarkLastNamesFemale[2])
						{
							//Cons, Vowel, Cons, Dash, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsFirstNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 9.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
				}
			}

			if (rules->bSize6LastNamesFemale)
			{
				j = randomIntRange(3, 5);
				for (i = 0; i < j; ++i)
				{
					if ((!rules->bAllApostropheLastNamesFemale) && (!rules->bAllDashLastNamesFemale))
					{
						//Vowel, Cons, Vowel, Cons, Vowel, Cons
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 5.0f;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);

						//Cons, Vowel, Cons, Vowel, Cons, Vowel
						pNameTemplate = StructCreate(parse_NameTemplateNoRef);
						iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
						iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
						iTotalCombinations += iCombinations;
						pNameTemplate->fWeight = iCombinations;
						pNameTemplate->fWeight /= 27.0f;
						if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
					}

					if (rules->bHasApostropheLastNamesFemale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Apostrophe");

						if (rules->bPosMarkLastNamesFemale[0] && (!rules->c2Many1VowelLastNamesFemale[rules->genderIndex]) && (!rules->c2Many1ConsLastNamesFemale[rules->genderIndex]))
						{
							//Cons, Apostrophe, Cons, Vowel, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Apostrophe, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Cons, Vowel, Cons, Apostrophe, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Vowel, Cons, Apostrophe, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[2])
						{
							//Cons, Vowel, Cons, Vowel, Cons, Apostrophe, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
					else if (rules->bHasDashLastNamesFemale)
					{
						tempset = RefSystem_ReferentFromString("PhonemeSet", "Char_Dash");

						if (rules->bPosMarkLastNamesFemale[0])
						{
							//Vowel, Cons, Dash, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[0])
						{
							//Cons, Vowel, Dash, Cons, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[0])
						{
							//Vowel, Cons, Dash, Vowel, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Cons, Vowel, Cons, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Vowel, Cons, Vowel, Dash, Cons, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[1])
						{
							//Cons, Vowel, Cons, Dash, Vowel, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[2])
						{
							//Vowel, Cons, Vowel, Cons, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[2])
						{
							//Cons, Vowel, Cons, Vowel, Dash, Cons, Vowel
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c1Many1ConsLastNamesFemale[rules->genderIndex], rules->c1Many2ConsLastNamesFemale[rules->genderIndex], rules->c1Many3ConsLastNamesFemale[rules->genderIndex], rules->b1AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c2Many1VowelLastNamesFemale[rules->genderIndex], rules->c2Many2VowelLastNamesFemale[rules->genderIndex], rules->c2Many3VowelLastNamesFemale[rules->genderIndex], rules->b2AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c3Many1VowelLastNamesFemale[rules->genderIndex], rules->c3Many2VowelLastNamesFemale[rules->genderIndex], rules->c3Many3VowelLastNamesFemale[rules->genderIndex], rules->b3AllVowelLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 10.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}

						if (rules->bPosMarkLastNamesFemale[2])
						{
							//Vowel, Cons, Vowel, Cons, Dash, Vowel, Cons
							pNameTemplate = StructCreate(parse_NameTemplateNoRef);
							iCombinations = speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, rules->c1Many1VowelLastNamesFemale[rules->genderIndex], rules->c1Many2VowelLastNamesFemale[rules->genderIndex], rules->c1Many3VowelLastNamesFemale[rules->genderIndex], rules->b1AllVowelLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c2Many1ConsLastNamesFemale[rules->genderIndex], rules->c2Many2ConsLastNamesFemale[rules->genderIndex], rules->c2Many3ConsLastNamesFemale[rules->genderIndex], rules->b2AllConsLastNamesFemale);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, NULL, NULL, NULL, false);
							if (tempset) eaPush(&pNameTemplate->eaPhonemeSets, (PhonemeSet*)tempset);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, rules->peaPhonemeVowels, NULL, NULL, NULL, false);
							iCombinations *= speciesgen_AddPhonemeToNameTemplate(pNameTemplate, peaCurrentGender, rules->c3Many1ConsLastNamesFemale[rules->genderIndex], rules->c3Many2ConsLastNamesFemale[rules->genderIndex], rules->c3Many3ConsLastNamesFemale[rules->genderIndex], rules->b3AllConsLastNamesFemale);
							iTotalCombinations += iCombinations;
							pNameTemplate->fWeight = iCombinations;
							pNameTemplate->fWeight /= 50.0f;
							pNameTemplate->fWeight /= 27.0f;
							if (iCombinations) eaPush(&(*ppLastNameRules)->eaNameTemplates, pNameTemplate); else StructDestroy(parse_NameTemplateNoRef, pNameTemplate);
						}
					}
				}
			}
		}
	}
}

typedef struct SpeciesGenGeoIndex
{
	int index;
}SpeciesGenGeoIndex;
static PCGeometryDef *speciesgen_GetNextGeo(SpeciesDefiningFeature *sdf, SpeciesGenGeoIndex *geoIndex, bool *bIsExcludeBone)
{
	PCGeometryDef *geo = NULL;
	int trueIndex;
	if (!bIsExcludeBone) return NULL;
	*bIsExcludeBone = false;
	if (!sdf) return NULL;
	if (sdf->eType != kSpeciesDefiningType_Geometry) return NULL;
	if ((!geoIndex) || geoIndex->index < 0) return NULL;
	trueIndex = geoIndex->index;
	if (GET_REF(sdf->hExcludeBone))
	{
		if (geoIndex->index == 0)
		{
			*bIsExcludeBone = true;
			geoIndex->index++;
			return NULL;
		}
		--trueIndex;
	}
	if (trueIndex >= eaSize(&sdf->eaGeometries)) return NULL;
	while (trueIndex < eaSize(&sdf->eaGeometries))
	{
		geo = GET_REF(sdf->eaGeometries[trueIndex]->hGeo);
		if (geo) break;
		trueIndex++;
		geoIndex->index++;
	}
	geoIndex->index++;
	return geo;
}

typedef struct SpeciesGenMatIndex
{
	int geoIndex;
	int index;
}SpeciesGenMatIndex;
static PCMaterialDef *speciesgen_GetNextMat(SpeciesDefiningFeature *sdf, SpeciesGenMatIndex *matIndex, PCGeometryDef **curGeo, NOCONST(MaterialLimits) **curMl)
{
	NOCONST(GeometryLimits) *gl = NULL;
	PCGeometryDef *geo = NULL;
	PCMaterialDef *mat = NULL;
	if (curGeo) *curGeo = NULL;
	if (curMl) *curMl = NULL;
	if (!sdf) return NULL;
	if (sdf->eType != kSpeciesDefiningType_Material) return NULL;
	if ((!matIndex) || matIndex->geoIndex < 0 || matIndex->index < 0) return NULL;
	if (matIndex->geoIndex >= eaSize(&sdf->eaGeometries)) return NULL;

	gl = CONTAINER_NOCONST(GeometryLimits, sdf->eaGeometries[matIndex->geoIndex]);
	geo = GET_REF(gl->hGeo);
	if (gl->bAllowAllMat && geo)
	{
		while (matIndex->index < eaSize(&geo->eaAllowedMaterialDefs))
		{
			mat = RefSystem_ReferentFromString("CostumeMaterial", geo->eaAllowedMaterialDefs[matIndex->index]);
			if (mat) break;
			matIndex->index++;
		}
	}
	else
	{
		while (matIndex->index < eaSize(&gl->eaMaterials))
		{
			mat = GET_REF(gl->eaMaterials[matIndex->index]->hMaterial);
			if (mat) break;
			matIndex->index++;
		}
	}
	while (matIndex->index >= (gl->bAllowAllMat && geo ? eaSize(&geo->eaAllowedMaterialDefs) : eaSize(&gl->eaMaterials)))
	{
		matIndex->index = 0;
		matIndex->geoIndex++;
		if (matIndex->geoIndex >= eaSize(&sdf->eaGeometries)) return NULL;
		gl = CONTAINER_NOCONST(GeometryLimits, sdf->eaGeometries[matIndex->geoIndex]);
		geo = GET_REF(gl->hGeo);
		if (gl->bAllowAllMat && geo)
		{
			while (matIndex->index < eaSize(&geo->eaAllowedMaterialDefs))
			{
				mat = RefSystem_ReferentFromString("CostumeMaterial", geo->eaAllowedMaterialDefs[matIndex->index]);
				if (mat) break;
				matIndex->index++;
			}
		}
		else
		{
			while (matIndex->index < eaSize(&gl->eaMaterials))
			{
				mat = GET_REF(gl->eaMaterials[matIndex->index]->hMaterial);
				if (mat) break;
				matIndex->index++;
			}
		}
	}

	if (curGeo) *curGeo = geo;
	if (curMl && !(gl->bAllowAllMat && geo)) *curMl = gl->eaMaterials[matIndex->index];
	matIndex->index++;
	return mat;
}

typedef struct SpeciesGenTexIndex
{
	int geoIndex;
	int matIndex;
	int index;
}SpeciesGenTexIndex;
static PCTextureDef *speciesgen_GetNextTex(SpeciesDefiningFeature *sdf, SpeciesGenTexIndex *texIndex, PCGeometryDef **curGeo, PCMaterialDef **curMat, NOCONST(MaterialLimits) **curMl, NOCONST(TextureLimits) **curTl)
{
	NOCONST(GeometryLimits) *gl = NULL;
	NOCONST(MaterialLimits) *ml = NULL;
	NOCONST(TextureLimits) *tl = NULL;
	PCGeometryDef *geo = NULL;
	PCMaterialDef *mat = NULL;
	PCTextureDef *tex = NULL;
	if (curGeo) *curGeo = NULL;
	if (curMat) *curMat = NULL;
	if (curMl) *curMl = NULL;
	if (curTl) *curTl = NULL;
	if (!sdf) return NULL;
	if (sdf->eType != kSpeciesDefiningType_Pattern && sdf->eType != kSpeciesDefiningType_Detail && sdf->eType != kSpeciesDefiningType_Specular &&
		sdf->eType != kSpeciesDefiningType_Diffuse && sdf->eType != kSpeciesDefiningType_Movable) return NULL;
	if ((!texIndex) || texIndex->geoIndex < 0 || texIndex->matIndex < 0 || texIndex->index < 0) return NULL;
	if (texIndex->geoIndex >= eaSize(&sdf->eaGeometries)) return NULL;

	gl = CONTAINER_NOCONST(GeometryLimits, sdf->eaGeometries[texIndex->geoIndex]);
	geo = GET_REF(gl->hGeo);
	if (gl->bAllowAllMat && geo)
	{
		while (texIndex->matIndex < eaSize(&geo->eaAllowedMaterialDefs))
		{
			ml = NULL;
			mat = RefSystem_ReferentFromString("CostumeMaterial", geo->eaAllowedMaterialDefs[texIndex->matIndex]);
			if (mat) break;
			texIndex->matIndex++;
		}
	}
	else
	{
		while (texIndex->matIndex < eaSize(&gl->eaMaterials))
		{
			ml = gl->eaMaterials[texIndex->matIndex];
			mat = GET_REF(ml->hMaterial);
			if (mat) break;
			texIndex->matIndex++;
		}
	}
	while (texIndex->matIndex >= (gl->bAllowAllMat && geo ? eaSize(&geo->eaAllowedMaterialDefs) : eaSize(&gl->eaMaterials)))
	{
		texIndex->index = 0;
		texIndex->matIndex = 0;
		texIndex->geoIndex++;
		if (texIndex->geoIndex >= eaSize(&sdf->eaGeometries)) return NULL;
		gl = CONTAINER_NOCONST(GeometryLimits, sdf->eaGeometries[texIndex->geoIndex]);
		geo = GET_REF(gl->hGeo);
		if (gl->bAllowAllMat && geo)
		{
			while (texIndex->matIndex< eaSize(&geo->eaAllowedMaterialDefs))
			{
				ml = NULL;
				mat = RefSystem_ReferentFromString("CostumeMaterial", geo->eaAllowedMaterialDefs[texIndex->matIndex]);
				if (mat) break;
				texIndex->matIndex++;
			}
		}
		else
		{
			while (texIndex->matIndex < eaSize(&gl->eaMaterials))
			{
				ml = gl->eaMaterials[texIndex->matIndex];
				mat = GET_REF(ml->hMaterial);
				if (mat) break;
				texIndex->matIndex++;
			}
		}
	}
	if ((!ml) || ml->bAllowAllTex)
	{
		while (texIndex->index < eaSize(&mat->eaAllowedTextureDefs))
		{
			tl = NULL;
			tex = RefSystem_ReferentFromString("CostumeTexture", mat->eaAllowedTextureDefs[texIndex->index]);
			if (tex) break;
			texIndex->index++;
		}
	}
	else
	{
		while (texIndex->index < eaSize(&ml->eaTextures))
		{
			tl = ml->eaTextures[texIndex->index];
			tex = GET_REF(tl->hTexture);
			if (tex) break;
			texIndex->index++;
		}
	}
	while (texIndex->index >= ((!ml) || ml->bAllowAllTex ? eaSize(&mat->eaAllowedTextureDefs) : eaSize(&ml->eaTextures)))
	{
		texIndex->index = 0;
		texIndex->matIndex++;
		if (gl->bAllowAllMat && geo)
		{
			while (texIndex->matIndex < eaSize(&geo->eaAllowedMaterialDefs))
			{
				ml = NULL;
				mat = RefSystem_ReferentFromString("CostumeMaterial", geo->eaAllowedMaterialDefs[texIndex->matIndex]);
				if (mat) break;
				texIndex->matIndex++;
			}
		}
		else
		{
			while (texIndex->matIndex < eaSize(&gl->eaMaterials))
			{
				ml = gl->eaMaterials[texIndex->matIndex];
				mat = GET_REF(ml->hMaterial);
				if (mat) break;
				texIndex->matIndex++;
			}
		}
		while (texIndex->matIndex >= (gl->bAllowAllMat && geo ? eaSize(&geo->eaAllowedMaterialDefs) : eaSize(&gl->eaMaterials)))
		{
			texIndex->index = 0;
			texIndex->matIndex = 0;
			texIndex->geoIndex++;
			if (texIndex->geoIndex >= eaSize(&sdf->eaGeometries)) return NULL;
			gl = CONTAINER_NOCONST(GeometryLimits, sdf->eaGeometries[texIndex->geoIndex]);
			geo = GET_REF(gl->hGeo);
			if (gl->bAllowAllMat && geo)
			{
				while (texIndex->matIndex< eaSize(&geo->eaAllowedMaterialDefs))
				{
					ml = NULL;
					mat = RefSystem_ReferentFromString("CostumeMaterial", geo->eaAllowedMaterialDefs[texIndex->matIndex]);
					if (mat) break;
					texIndex->matIndex++;
				}
			}
			else
			{
				while (texIndex->matIndex < eaSize(&gl->eaMaterials))
				{
					ml = gl->eaMaterials[texIndex->matIndex];
					mat = GET_REF(ml->hMaterial);
					if (mat) break;
					texIndex->matIndex++;
				}
			}
		}
		if ((!ml) || ml->bAllowAllTex)
		{
			while (texIndex->index < eaSize(&mat->eaAllowedTextureDefs))
			{
				tl = NULL;
				tex = RefSystem_ReferentFromString("CostumeTexture", mat->eaAllowedTextureDefs[texIndex->index]);
				if (tex) break;
				texIndex->index++;
			}
		}
		else
		{
			while (texIndex->index < eaSize(&ml->eaTextures))
			{
				tl = ml->eaTextures[texIndex->index];
				tex = GET_REF(tl->hTexture);
				if (tex) break;
				texIndex->index++;
			}
		}
	}

	if (curGeo) *curGeo = geo;
	if (curMat) *curMat = mat;
	if (curMl && ml) *curMl = ml;
	if (curTl && tl) *curTl = tl;
	texIndex->index++;
	return tex;
}

static SpeciesDefiningFeature *speciesgen_GetMatchingFeature(SpeciesGenData *pSpeciesGenData, SpeciesDefiningFeature *sdf, PCSkeletonDef *pMatchingSkel, PCGeometryDef *geo, PCMaterialDef *mat, PCTextureDef *tex, int *index2)
{
	//Find matching feature
	int j;
	SpeciesDefiningFeature *sdf2 = NULL;
	*index2 = 0;
	for(j = eaSize(&pSpeciesGenData->eaFeaturesToUse)-1; j >= 0; --j)
	{
		sdf2 = (SpeciesDefiningFeature*)GET_REF(pSpeciesGenData->eaFeaturesToUse[j]->hSpeciesDefiningFeatureRef);
		if (!sdf2) continue;
		if (GET_REF(sdf2->hSkeleton) != pMatchingSkel) continue;
		if (!stricmp(sdf2->pcMatchName,sdf->pcMatchName)) break;
	}
	if (j < 0) return NULL;
	if (sdf->eType == kSpeciesDefiningType_Geometry)
	{
		PCGeometryDef *g = NULL;
		SpeciesGenGeoIndex geoIndex;
		bool bExcludeBone = false;

		if (!geo)
		{
			if (!GET_REF(sdf2->hExcludeBone)) return NULL;
			return sdf2;
		}

		geoIndex.index = 0;
		do
		{
			g = speciesgen_GetNextGeo(sdf2, &geoIndex, &bExcludeBone);
			if ((!bExcludeBone) && !g) return NULL;
			if (g)
			{
				const char *pcName1, *pcName2;
				if (g == geo) return sdf2;
				pcName1 = TranslateDisplayMessage(g->displayNameMsg);
				pcName2 = TranslateDisplayMessage(geo->displayNameMsg);
				if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) return sdf2;
			}
			++*index2;
		} while (1);
	}
	else if (sdf->eType == kSpeciesDefiningType_Material)
	{
		PCGeometryDef *g = NULL;
		PCMaterialDef *m = NULL;
		SpeciesGenMatIndex matIndex;

		if ((!geo) || (!mat)) return NULL;

		matIndex.geoIndex = 0;
		matIndex.index = 0;
		do
		{
			const char *pcName1, *pcName2;
			m = speciesgen_GetNextMat(sdf2, &matIndex, &g, NULL);
			if ((!m) || (!g)) return NULL;
			if (g != geo)
			{
				pcName1 = TranslateDisplayMessage(g->displayNameMsg);
				pcName2 = TranslateDisplayMessage(geo->displayNameMsg);
				if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) g = geo;
			}
			if (g == geo)
			{
				if (m == mat) return sdf2;
				pcName1 = TranslateDisplayMessage(m->displayNameMsg);
				pcName2 = TranslateDisplayMessage(mat->displayNameMsg);
				if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) return sdf2;
			}
			++*index2;
		} while (1);
	}
	else if (sdf->eType == kSpeciesDefiningType_Pattern || sdf->eType == kSpeciesDefiningType_Detail || sdf->eType == kSpeciesDefiningType_Specular || 
			sdf->eType == kSpeciesDefiningType_Diffuse || sdf->eType == kSpeciesDefiningType_Movable)
	{
		PCGeometryDef *g = NULL;
		PCMaterialDef *m = NULL;
		PCTextureDef *t = NULL;
		SpeciesGenTexIndex texIndex;

		if ((!geo) || (!mat) || (!tex)) return NULL;

		texIndex.geoIndex = 0;
		texIndex.matIndex = 0;
		texIndex.index = 0;
		do
		{
			const char *pcName1, *pcName2;
			t = speciesgen_GetNextTex(sdf2, &texIndex, &g, &m, NULL, NULL);
			if ((!t) || (!m) || (!g)) return NULL;
			if (g != geo)
			{
				pcName1 = TranslateDisplayMessage(g->displayNameMsg);
				pcName2 = TranslateDisplayMessage(geo->displayNameMsg);
				if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) g = geo;
			}
			if (g == geo)
			{
				if (m != mat)
				{
					pcName1 = TranslateDisplayMessage(m->displayNameMsg);
					pcName2 = TranslateDisplayMessage(mat->displayNameMsg);
					if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) m = mat;
				}
				if (m == mat)
				{
					if (t == tex) return sdf2;
					pcName1 = TranslateDisplayMessage(t->displayNameMsg);
					pcName2 = TranslateDisplayMessage(tex->displayNameMsg);
					if (pcName1 && pcName2 && (stricmp(pcName1, pcName2) == 0)) return sdf2;
				}
			}
			++*index2;
		} while (1);
	}
	return sdf2;
}

static SpeciesDefiningFeature *speciesgen_ChooseDefiningFeature(SpeciesGenData *pSpeciesGenData, int randValue, NOCONST(SpeciesDef) *newSpecies, SpeciesUsedFeatures ***peaUsedFeatures, bool *bScaleChangeSelected, int *index, int *index2, void **eaSpeciesFeatureList, PCSkeletonDef *pMatchingSkel, SpeciesDefiningFeature **pMatchingFeature)
{
	int selection;
	int safety = 0;
	int i, j, k, l, n, o;

	if (randValue < 0) return NULL;

	*index = 0;
	selection = randValue;

	for(i = 0; i < eaSize(&eaSpeciesFeatureList); ++i)
	{
		SpeciesDefiningFeature *sdf = GET_REF(((SpeciesDefiningFeatureRef*)eaSpeciesFeatureList[i])->hSpeciesDefiningFeatureRef);
		if (!sdf) continue;
		if (GET_REF(sdf->hSkeleton) != GET_REF(newSpecies->hSkeleton)) continue;
		if (sdf->eType == kSpeciesDefiningType_Invalid || sdf->eType == kSpeciesDefiningType_Default) continue;
		if (sdf->eType == kSpeciesDefiningType_Grouped)
		{
			if (selection-- <= 0)
			{
				for (j = eaSize(&sdf->eaGeometries)-1; j >= 0; --j)
				{
					PCGeometryDef *geo = GET_REF(sdf->eaGeometries[j]->hGeo);
					if ((!geo) || (!GET_REF(geo->hBone))) continue;
					for (k = eaSize(peaUsedFeatures)-1; k >= 0; --k)
					{
						if (GET_REF(geo->hBone) == (*peaUsedFeatures)[k]->Bone)
						{
							return NULL; //Fail - We don't want to have more than one defining feature per bone
						}
					}
				}
				if (pMatchingSkel && pMatchingFeature && index2)
				{
					*pMatchingFeature = speciesgen_GetMatchingFeature(pSpeciesGenData, sdf, pMatchingSkel, NULL, NULL, NULL, index2);
					if (!*pMatchingFeature) return NULL; //Fail - No matching defining feature found for other gender
				}
				for (j = eaSize(&sdf->eaGeometries)-1; j >= 0; --j)
				{
					PCGeometryDef *geo = GET_REF(sdf->eaGeometries[j]->hGeo);
					SpeciesUsedFeatures *suf;
					if ((!geo) || (!GET_REF(geo->hBone))) continue;
					suf = StructCreate(parse_SpeciesUsedFeatures);
					suf->Bone = GET_REF(geo->hBone);
					suf->Geo = geo;
					eaPush(peaUsedFeatures, suf);
				}
				return sdf;
			}
			continue;
		}
		if (sdf->eType == kSpeciesDefiningType_BodyScale || sdf->eType == kSpeciesDefiningType_BoneScale ||
			sdf->eType == kSpeciesDefiningType_Height || sdf->eType == kSpeciesDefiningType_Muscle)
		{
			if (selection-- <= 0)
			{
				if (*bScaleChangeSelected) return NULL; //Fail - only one body scale change allowed
				if (pMatchingSkel && pMatchingFeature && index2)
				{
					*pMatchingFeature = speciesgen_GetMatchingFeature(pSpeciesGenData, sdf, pMatchingSkel, NULL, NULL, NULL, index2);
					if (!*pMatchingFeature) return NULL; //Fail - No matching defining feature found for other gender
				}
				*bScaleChangeSelected = true;
				return sdf;
			}
			continue;
		}
		if (sdf->eType == kSpeciesDefiningType_Geometry)
		{
			PCGeometryDef *geo = NULL;
			SpeciesGenGeoIndex geoIndex;
			int count = 0;
			bool bExcludeBone = false;
			geoIndex.index = 0;
			do
			{
				geo = speciesgen_GetNextGeo(sdf, &geoIndex, &bExcludeBone);
				if ((!bExcludeBone) && !geo) break;
				++count;
			} while (selection-- > 0);
			--count;

			if (bExcludeBone)
			{
				//Bone with no features counts as a defining feature
				for (j = eaSize(peaUsedFeatures)-1; j >= 0; --j)
				{
					if (GET_REF(sdf->hExcludeBone) == (*peaUsedFeatures)[j]->Bone)
					{
						break; //Fail - We don't want to have more than one defining feature per bone;
					}
				}
				if (j < 0 || !eaSize(peaUsedFeatures))
				{
					SpeciesUsedFeatures *suf;
					if (pMatchingSkel && pMatchingFeature && index2)
					{
						*pMatchingFeature = speciesgen_GetMatchingFeature(pSpeciesGenData, sdf, pMatchingSkel, NULL, NULL, NULL, index2);
						if (!*pMatchingFeature) return NULL; //Fail - No matching defining feature found for other gender
					}
					suf = StructCreate(parse_SpeciesUsedFeatures);
					suf->Bone = GET_REF(sdf->hExcludeBone);
					eaPush(peaUsedFeatures, suf);
					return sdf;
				}
				return NULL;
			}
			if (geo)
			{
				if (!GET_REF(geo->hBone)) return NULL;
				for (j = eaSize(peaUsedFeatures)-1; j >= 0; --j)
				{
					if (GET_REF(geo->hBone) == (*peaUsedFeatures)[j]->Bone)
					{
						break; //Fail - We don't want to have more than one defining feature per bone;
					}
				}
				if (j < 0 || !eaSize(peaUsedFeatures))
				{
					SpeciesUsedFeatures *suf;
					if (pMatchingSkel && pMatchingFeature && index2)
					{
						*pMatchingFeature = speciesgen_GetMatchingFeature(pSpeciesGenData, sdf, pMatchingSkel, geo, NULL, NULL, index2);
						if (!*pMatchingFeature) return NULL; //Fail - No matching defining feature found for other gender
					}
					suf = StructCreate(parse_SpeciesUsedFeatures);
					suf->Bone = GET_REF(geo->hBone);
					suf->Geo = geo;
					eaPush(peaUsedFeatures, suf);
					*index = count;
					if (GET_REF(sdf->hExcludeBone)) ++*index;
					return sdf;
				}
				return NULL;
			}
			continue;
		}
		if (sdf->eType == kSpeciesDefiningType_Material)
		{
			PCGeometryDef *geo = NULL;
			PCMaterialDef *mat = NULL;
			SpeciesGenMatIndex matIndex;
			int count = 0;
			matIndex.geoIndex = 0;
			matIndex.index = 0;
			do
			{
				mat = speciesgen_GetNextMat(sdf, &matIndex, &geo, NULL);
				if (!mat) break;
				++count;
			} while (selection-- > 0);
			--count;

			if (mat)
			{
				bool found = false;
				for (k = eaSize(peaUsedFeatures)-1; k >= 0; --k)
				{
					PCGeometryDef *g = (*peaUsedFeatures)[k]->Geo;
					if (mat == (*peaUsedFeatures)[k]->Mat)
					{
						//If material already exists then fail
						return NULL;
					}
					if (!g) continue;
					for (l = eaSize(&g->eaAllowedMaterialDefs)-1; l >= 0; --l)
					{
						PCMaterialDef *m = RefSystem_ReferentFromString("CostumeMaterial", g->eaAllowedMaterialDefs[l]);
						if (!m) continue;
						if (m == mat)
						{
							//Good, material is allowed
							found = true;
							break;
						}
					}
				}
				if (!found)
				{
					//Look for a valid default
					for (k = eaSize(&newSpecies->eaGeometries)-1; k >= 0; --k)
					{
						PCGeometryDef *g = GET_REF(newSpecies->eaGeometries[k]->hGeo);
						PCBoneDef *b = g ? GET_REF(g->hBone) : NULL;
						if (!b) continue;
						for (l = eaSize(peaUsedFeatures)-1; l >= 0; --l)
						{
							//don't compare with geos that will be replaced
							if ((*peaUsedFeatures)[l]->Bone == b) break;
						}
						if (l >= 0) continue;
						for (l = eaSize(&g->eaAllowedMaterialDefs)-1; l >= 0; --l)
						{
							PCMaterialDef *m = RefSystem_ReferentFromString("CostumeMaterial", g->eaAllowedMaterialDefs[l]);
							if (!m) continue;
							if (m == mat)
							{
								//Good, material is allowed
								found = true;
								break;
							}
						}
						if (found) break;
					}
				}
				if (found)
				{
					SpeciesUsedFeatures *suf;
					if (pMatchingSkel && pMatchingFeature && index2)
					{
						*pMatchingFeature = speciesgen_GetMatchingFeature(pSpeciesGenData, sdf, pMatchingSkel, geo, mat, NULL, index2);
						if (!*pMatchingFeature) return NULL; //Fail - No matching defining feature found for other gender
					}
					suf = StructCreate(parse_SpeciesUsedFeatures);
					suf->Bone = GET_REF(geo->hBone);
					suf->Geo = geo;
					suf->Mat = mat;
					eaPush(peaUsedFeatures, suf);
					*index = count;
					return sdf;
				}
				return NULL;
			}
			continue;
		}
		if (sdf->eType == kSpeciesDefiningType_Pattern || sdf->eType == kSpeciesDefiningType_Detail || sdf->eType == kSpeciesDefiningType_Specular || 
			sdf->eType == kSpeciesDefiningType_Diffuse || sdf->eType == kSpeciesDefiningType_Movable)
		{
			PCGeometryDef *geo = NULL;
			PCMaterialDef *mat = NULL;
			PCTextureDef *tex = NULL;
			SpeciesGenTexIndex texIndex;
			int count = 0;
			texIndex.geoIndex = 0;
			texIndex.matIndex = 0;
			texIndex.index = 0;
			do
			{
				tex = speciesgen_GetNextTex(sdf, &texIndex, &geo, &mat, NULL, NULL);
				if (!tex) break;
				++count;
			} while (selection-- > 0);
			--count;

			if (tex)
			{
				bool found = false;
				for (l = eaSize(peaUsedFeatures)-1; l >= 0; --l)
				{
					PCGeometryDef *g = (*peaUsedFeatures)[l]->Geo;
					if ((tex->eTypeFlags == kPCTextureType_Pattern && tex == (*peaUsedFeatures)[l]->tex_pattern) ||
						(tex->eTypeFlags == kPCTextureType_Detail && tex == (*peaUsedFeatures)[l]->tex_detail) ||
						(tex->eTypeFlags == kPCTextureType_Specular && tex == (*peaUsedFeatures)[l]->tex_specular) ||
						(tex->eTypeFlags == kPCTextureType_Diffuse && tex == (*peaUsedFeatures)[l]->tex_diffuse) ||
						(tex->eTypeFlags == kPCTextureType_Movable && tex == (*peaUsedFeatures)[l]->tex_movable))
					{
						//If texture already exists then fail
						return NULL;
					}
					if (!g) continue;
					if (g == geo)
					{
						//If we already have this texture type on this geo then we fail
						if (tex->eTypeFlags == kPCTextureType_Pattern && (*peaUsedFeatures)[l]->tex_pattern) return NULL;
						if (tex->eTypeFlags == kPCTextureType_Detail && (*peaUsedFeatures)[l]->tex_detail) return NULL;
						if (tex->eTypeFlags == kPCTextureType_Specular && (*peaUsedFeatures)[l]->tex_specular) return NULL;
						if (tex->eTypeFlags == kPCTextureType_Diffuse && (*peaUsedFeatures)[l]->tex_diffuse) return NULL;
						if (tex->eTypeFlags == kPCTextureType_Movable && (*peaUsedFeatures)[l]->tex_movable) return NULL;
					}
					for (n = eaSize(&g->eaAllowedMaterialDefs)-1; n >= 0; --n)
					{
						PCMaterialDef *m = RefSystem_ReferentFromString("CostumeMaterial", g->eaAllowedMaterialDefs[n]);
						if (!m) continue;
						for (o = eaSize(&m->eaAllowedTextureDefs)-1; o >= 0; --o)
						{
							PCTextureDef *t = RefSystem_ReferentFromString("CostumeTexture", m->eaAllowedTextureDefs[o]);
							if (!t) continue;
							if (t == tex)
							{
								//Good, texture is allowed
								found = true;
								break;
							}
						}
					}
				}
				if (!found)
				{
					//Look for a valid default
					for (n = eaSize(&newSpecies->eaGeometries)-1; n >= 0; --n)
					{
						PCGeometryDef *g = GET_REF(newSpecies->eaGeometries[n]->hGeo);
						PCBoneDef *b = g ? GET_REF(g->hBone) : NULL;
						if (!b) continue;
						for (l = eaSize(peaUsedFeatures)-1; l >= 0; --l)
						{
							//don't compare with geos that will be replaced
							if ((*peaUsedFeatures)[l]->Bone == b) break;
						}
						if (l >= 0) continue;
						for (l = eaSize(&g->eaAllowedMaterialDefs)-1; l >= 0; --l)
						{
							PCMaterialDef *m = RefSystem_ReferentFromString("CostumeMaterial", g->eaAllowedMaterialDefs[l]);
							if (!m) continue;
							for (o = eaSize(&m->eaAllowedTextureDefs)-1; o >= 0; --o)
							{
								PCTextureDef *t = RefSystem_ReferentFromString("CostumeTexture", m->eaAllowedTextureDefs[o]);
								if (!t) continue;
								if (t == tex)
								{
									//Good, texture is allowed
									found = true;
									break;
								}
							}
							if (found) break;
						}
						if (found) break;
					}
				}
				if (found)
				{
					SpeciesUsedFeatures *suf;
					if (pMatchingSkel && pMatchingFeature && index2)
					{
						*pMatchingFeature = speciesgen_GetMatchingFeature(pSpeciesGenData, sdf, pMatchingSkel, geo, mat, tex, index2);
						if (!*pMatchingFeature) return NULL; //Fail - No matching defining feature found for other gender
					}
					suf = StructCreate(parse_SpeciesUsedFeatures);
					suf->Bone = GET_REF(geo->hBone);
					suf->Geo = geo;
					suf->Mat = mat;
					switch (sdf->eType)
					{
					case kSpeciesDefiningType_Pattern: suf->tex_pattern = tex; break;
					case kSpeciesDefiningType_Detail: suf->tex_detail = tex; break;
					case kSpeciesDefiningType_Specular: suf->tex_specular = tex; break;
					case kSpeciesDefiningType_Diffuse: suf->tex_diffuse = tex; break;
					case kSpeciesDefiningType_Movable: suf->tex_movable = tex; break;
					}
					eaPush(peaUsedFeatures, suf);
					*index = count;
					return sdf;
				}
				return NULL;
			}
			continue;
		}
	}

	return NULL;
}

static void speciesgen_ApplyNewFeature(NOCONST(SpeciesDef) *newSpecies, SpeciesDefiningFeature *sdf, int index)
{
	int j, k, l, n, o;

	if (sdf->eType == kSpeciesDefiningType_Geometry)
	{
		NOCONST(GeometryLimits) *gl;
		PCGeometryDef *geo = NULL;
		PCBoneDef *bone = NULL;
		SpeciesGenGeoIndex geoIndex;
		int count = 0;
		bool bExcludeBone = false;
		geoIndex.index = 0;
		do
		{
			geo = speciesgen_GetNextGeo(sdf, &geoIndex, &bExcludeBone);
			if ((!bExcludeBone) && !geo) break;
			if (!bExcludeBone) ++count;
		} while (index-- > 0);
		--count;

		if (bExcludeBone)
		{
			bone = GET_REF(sdf->hExcludeBone);
		}
		else
		{
			bone = geo ? GET_REF(geo->hBone) : NULL;
		}
		if (bone)
		{
			for (j = eaSize(&newSpecies->eaGeometries)-1; j >= 0; --j)
			{
				PCGeometryDef *g = GET_REF(newSpecies->eaGeometries[j]->hGeo);
				PCBoneDef *b = g ? GET_REF(g->hBone) : NULL;
				if (b == bone)
				{
					//For defining feature only one possible geo allowed per bone (unless grouped in the SpeciesDefiningFeature)
					StructDestroyNoConst(parse_GeometryLimits, newSpecies->eaGeometries[j]);
					eaRemove(&newSpecies->eaGeometries, j);
				}
			}
			if (geo)
			{
				gl = StructCloneDeConst(parse_GeometryLimits,sdf->eaGeometries[count]);
				if (gl)
				{
					NOCONST(SpeciesBoneData) *sbd = StructCreateNoConst(parse_SpeciesBoneData);
					if (sbd)
					{
						COPY_HANDLE(sbd->hBone, geo->hBone);
						sbd->bRequires = true;
						eaPush(&newSpecies->eaBoneData, sbd);
					}
					eaPush(&newSpecies->eaGeometries, gl);
				}
			}
		}
	}
	else if (sdf->eType == kSpeciesDefiningType_Grouped)
	{
		for (k = eaSize(&sdf->eaGeometries)-1; k >= 0; --k)
		{
			PCGeometryDef *geo = GET_REF(sdf->eaGeometries[k]->hGeo);
			PCBoneDef *bone = geo ? GET_REF(geo->hBone) : NULL;
			if (bone)
			{
				for (j = eaSize(&newSpecies->eaGeometries)-1; j >= 0; --j)
				{
					PCGeometryDef *g = GET_REF(newSpecies->eaGeometries[j]->hGeo);
					PCBoneDef *b = g ? GET_REF(g->hBone) : NULL;
					if (b == bone)
					{
						//For defining feature only one possible geo allowed per bone (unless grouped in the SpeciesDefiningFeature)
						StructDestroyNoConst(parse_GeometryLimits, newSpecies->eaGeometries[j]);
						eaRemove(&newSpecies->eaGeometries, j);
					}
				}
			}
		}
		for (k = eaSize(&sdf->eaGeometries)-1; k >= 0; --k)
		{
			NOCONST(GeometryLimits) *gl = StructCloneDeConst(parse_GeometryLimits,sdf->eaGeometries[k]);
			PCGeometryDef *geo = GET_REF(sdf->eaGeometries[k]->hGeo);
			if (geo)
			{
				for (j = eaSize(&newSpecies->eaBoneData)-1; j >= 0; --j)
				{
					if (GET_REF(newSpecies->eaBoneData[j]->hBone) == GET_REF(geo->hBone)) break;
				}
				if (j < 0)
				{
					NOCONST(SpeciesBoneData) *sbd = StructCreateNoConst(parse_SpeciesBoneData);
					if (sbd)
					{
						COPY_HANDLE(sbd->hBone, geo->hBone);
						sbd->bRequires = true;
						eaPush(&newSpecies->eaBoneData, sbd);
					}
				}
			}
			if (gl) eaPush(&newSpecies->eaGeometries, gl);
		}
	}
	else if (sdf->eType == kSpeciesDefiningType_Material)
	{
		PCGeometryDef *geo = NULL;
		PCMaterialDef *mat = NULL;
		NOCONST(MaterialLimits) *ml0 = NULL;
		SpeciesGenMatIndex matIndex;
		matIndex.geoIndex = 0;
		matIndex.index = 0;
		do
		{
			mat = speciesgen_GetNextMat(sdf, &matIndex, &geo, &ml0);
			if (!mat) break;
		} while (index-- > 0);

		if (mat)
		{
			//Find all valid places for this an put it there
			for (j = eaSize(&newSpecies->eaGeometries)-1; j >= 0; --j)
			{
				PCGeometryDef *g = GET_REF(newSpecies->eaGeometries[j]->hGeo);
				if (!g) continue;
				for (l = eaSize(&g->eaAllowedMaterialDefs)-1; l >= 0; --l)
				{
					PCMaterialDef *m = RefSystem_ReferentFromString("CostumeMaterial", g->eaAllowedMaterialDefs[l]);
					if (!m) continue;
					if (m == mat)
					{
						//It is allowed; let's add it
						NOCONST(MaterialLimits) *ml = ml0 ? StructCloneNoConst(parse_MaterialLimits,ml0) : StructCreateNoConst(parse_MaterialLimits);
						if (ml)
						{
							if (!ml0) SET_HANDLE_FROM_REFERENT("CostumeMaterial", mat, ml->hMaterial);
							eaPush(&newSpecies->eaGeometries[j]->eaMaterials, ml);
							newSpecies->eaGeometries[j]->bAllowAllMat = false;
							if (geo)
							{
								for (j = eaSize(&newSpecies->eaBoneData)-1; j >= 0; --j)
								{
									if (GET_REF(newSpecies->eaBoneData[j]->hBone) == GET_REF(geo->hBone)) break;
								}
								if (j < 0)
								{
									NOCONST(SpeciesBoneData) *sbd = StructCreateNoConst(parse_SpeciesBoneData);
									if (sbd)
									{
										COPY_HANDLE(sbd->hBone, geo->hBone);
										sbd->bRequires = true;
										eaPush(&newSpecies->eaBoneData, sbd);
									}
								}
							}
						}
					}
				}
			}
		}
	}
	else if (sdf->eType == kSpeciesDefiningType_Pattern || sdf->eType == kSpeciesDefiningType_Detail || sdf->eType == kSpeciesDefiningType_Specular || 
		sdf->eType == kSpeciesDefiningType_Diffuse || sdf->eType == kSpeciesDefiningType_Movable)
	{
		PCGeometryDef *geo = NULL;
		PCMaterialDef *mat = NULL;
		PCTextureDef *tex = NULL;
		NOCONST(MaterialLimits) *ml0 = NULL;
		NOCONST(TextureLimits) *tl0 = NULL;
		SpeciesGenTexIndex texIndex;
		texIndex.geoIndex = 0;
		texIndex.matIndex = 0;
		texIndex.index = 0;
		do
		{
			tex = speciesgen_GetNextTex(sdf, &texIndex, &geo, &mat, &ml0, &tl0);
			if (!tex) break;
		} while (index-- > 0);

		if (tex)
		{
			//Find all valid places for this an put it there
			for (j = eaSize(&newSpecies->eaGeometries)-1; j >= 0; --j)
			{
				PCGeometryDef *g = GET_REF(newSpecies->eaGeometries[j]->hGeo);
				if (!g) continue;
				if (newSpecies->eaGeometries[j]->bAllowAllMat)
				{
					for (l = eaSize(&g->eaAllowedMaterialDefs)-1; l >= 0; --l)
					{
						PCMaterialDef *m = RefSystem_ReferentFromString("CostumeMaterial", g->eaAllowedMaterialDefs[l]);
						if (!m) continue;
						for (o = eaSize(&m->eaAllowedTextureDefs)-1; o >= 0; --o)
						{
							PCTextureDef *t = RefSystem_ReferentFromString("CostumeTexture", m->eaAllowedTextureDefs[o]);
							if (!t) continue;
							if (t == tex)
							{
								//It is allowed; let's add it
								NOCONST(MaterialLimits) *ml = StructCreateNoConst(parse_MaterialLimits);
								if (ml)
								{
									NOCONST(TextureLimits) *tl = StructCreateNoConst(parse_TextureLimits);
									if (tl)
									{
										SET_HANDLE_FROM_REFERENT("CostumeTexture", t, tl->hTexture);
										if (tl0 && tl0->bOverrideConstValues)
										{
											tl->bOverrideConstValues = true;
											tl->fValueMin = tl0->fValueMin;
											tl->fValueMax = tl0->fValueMax;
										}
										if (tl0 && tl0->bOverrideMovableValues)
										{
											tl->bOverrideMovableValues = true;
											tl->fMovableMinX = tl0->fMovableMinX;
											tl->fMovableMaxX = tl0->fMovableMaxX;
											tl->fMovableMinY = tl0->fMovableMinY;
											tl->fMovableMaxY = tl0->fMovableMaxY;
											tl->fMovableMinScaleX = tl0->fMovableMinScaleX;
											tl->fMovableMaxScaleX = tl0->fMovableMaxScaleX;
											tl->fMovableMinScaleY = tl0->fMovableMinScaleY;
											tl->fMovableMaxScaleY = tl0->fMovableMaxScaleY;
											tl->bMovableCanEditPosition = tl0->bMovableCanEditPosition;
											tl->bMovableCanEditRotation = tl0->bMovableCanEditRotation;
											tl->bMovableCanEditScale = tl0->bMovableCanEditScale;
										}
										eaPush(&ml->eaTextures, tl);
										SET_HANDLE_FROM_REFERENT("CostumeMaterial", m, ml->hMaterial);
										ml->bAllowAllTex = false;
										switch (tex->eTypeFlags)
										{
										case kPCTextureType_Pattern: ml->bRequiresPattern = true; break;
										case kPCTextureType_Detail: ml->bRequiresDetail = true; break;
										case kPCTextureType_Specular: ml->bRequiresSpecular = true; break;
										case kPCTextureType_Diffuse: ml->bRequiresDiffuse = true; break;
										case kPCTextureType_Movable: ml->bRequiresMovable = true; break;
										}
										eaPush(&newSpecies->eaGeometries[j]->eaMaterials, ml);
										newSpecies->eaGeometries[j]->bAllowAllMat = false;
										if (geo)
										{
											for (j = eaSize(&newSpecies->eaBoneData)-1; j >= 0; --j)
											{
												if (GET_REF(newSpecies->eaBoneData[j]->hBone) == GET_REF(geo->hBone)) break;
											}
											if (j < 0)
											{
												NOCONST(SpeciesBoneData) *sbd = StructCreateNoConst(parse_SpeciesBoneData);
												if (sbd)
												{
													COPY_HANDLE(sbd->hBone, geo->hBone);
													sbd->bRequires = true;
													eaPush(&newSpecies->eaBoneData, sbd);
												}
											}
										}
									}
									else
									{
										StructDestroyNoConst(parse_MaterialLimits,ml);
									}
								}
							}
						}
					}
				}
				else
				{
					for (l = eaSize(&newSpecies->eaGeometries[j]->eaMaterials)-1; l >= 0; --l)
					{
						PCMaterialDef *m = GET_REF(newSpecies->eaGeometries[j]->eaMaterials[l]->hMaterial);
						if (!m) continue;
						for (o = eaSize(&m->eaAllowedTextureDefs)-1; o >= 0; --o)
						{
							PCTextureDef *t = RefSystem_ReferentFromString("CostumeTexture", m->eaAllowedTextureDefs[o]);
							if (!t) continue;
							if (t == tex)
							{
								//It is allowed; First remove textures of the same type
								if (!newSpecies->eaGeometries[j]->eaMaterials[l]->bAllowAllTex)
								{
									for (n = eaSize(&newSpecies->eaGeometries[j]->eaMaterials[l]->eaTextures)-1; n >= 0; --n)
									{
										PCTextureDef *t2 = GET_REF(newSpecies->eaGeometries[j]->eaMaterials[l]->eaTextures[n]->hTexture);
										if (!t2) continue;
										if (t2->eTypeFlags == tex->eTypeFlags)
										{
											StructDestroyNoConst(parse_TextureLimits, newSpecies->eaGeometries[j]->eaMaterials[l]->eaTextures[n]);
											eaRemove(&newSpecies->eaGeometries[j]->eaMaterials[l]->eaTextures, n);
										}
									}
								}

								//It is allowed; let's add it
								{
									NOCONST(TextureLimits) *tl = StructCreateNoConst(parse_TextureLimits);
									NOCONST(MaterialLimits) *ml = newSpecies->eaGeometries[j]->eaMaterials[l];
									if (tl)
									{
										SET_HANDLE_FROM_REFERENT("CostumeTexture", t, tl->hTexture);
										if (tl0 && tl0->bOverrideConstValues)
										{
											tl->bOverrideConstValues = true;
											tl->fValueMin = tl0->fValueMin;
											tl->fValueMax = tl0->fValueMax;
										}
										if (tl0 && tl0->bOverrideMovableValues)
										{
											tl->bOverrideMovableValues = true;
											tl->fMovableMinX = tl0->fMovableMinX;
											tl->fMovableMaxX = tl0->fMovableMaxX;
											tl->fMovableMinY = tl0->fMovableMinY;
											tl->fMovableMaxY = tl0->fMovableMaxY;
											tl->fMovableMinScaleX = tl0->fMovableMinScaleX;
											tl->fMovableMaxScaleX = tl0->fMovableMaxScaleX;
											tl->fMovableMinScaleY = tl0->fMovableMinScaleY;
											tl->fMovableMaxScaleY = tl0->fMovableMaxScaleY;
											tl->bMovableCanEditPosition = tl0->bMovableCanEditPosition;
											tl->bMovableCanEditRotation = tl0->bMovableCanEditRotation;
											tl->bMovableCanEditScale = tl0->bMovableCanEditScale;
										}
										eaPush(&ml->eaTextures, tl);
										ml->bAllowAllTex = false;
										switch (tex->eTypeFlags)
										{
										case kPCTextureType_Pattern: ml->bRequiresPattern = true; break;
										case kPCTextureType_Detail: ml->bRequiresDetail = true; break;
										case kPCTextureType_Specular: ml->bRequiresSpecular = true; break;
										case kPCTextureType_Diffuse: ml->bRequiresDiffuse = true; break;
										case kPCTextureType_Movable: ml->bRequiresMovable = true; break;
										}
										newSpecies->eaGeometries[j]->bAllowAllMat = false;
										if (geo)
										{
											for (j = eaSize(&newSpecies->eaBoneData)-1; j >= 0; --j)
											{
												if (GET_REF(newSpecies->eaBoneData[j]->hBone) == GET_REF(geo->hBone)) break;
											}
											if (j < 0)
											{
												NOCONST(SpeciesBoneData) *sbd = StructCreateNoConst(parse_SpeciesBoneData);
												if (sbd)
												{
													COPY_HANDLE(sbd->hBone, geo->hBone);
													sbd->bRequires = true;
													eaPush(&newSpecies->eaBoneData, sbd);
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	else if (sdf->eType == kSpeciesDefiningType_BodyScale)
	{
		for (k = eaSize(&sdf->eaBodyScaleLimits)-1; k >= 0; --k)
		{
			for (j = eaSize(&newSpecies->eaBodyScaleLimits)-1; j >= 0; --j)
			{
				if (!stricmp(newSpecies->eaBodyScaleLimits[j]->pcName,sdf->eaBodyScaleLimits[k]->pcName))
				{
					newSpecies->eaBodyScaleLimits[j]->fMin = sdf->eaBodyScaleLimits[k]->fMin;
					newSpecies->eaBodyScaleLimits[j]->fMax = sdf->eaBodyScaleLimits[k]->fMax;
					break;
				}
			}
			if (j < 0)
			{
				NOCONST(BodyScaleLimit) *bsl = StructCreateNoConst(parse_BodyScaleLimit);
				bsl->fMin = sdf->eaBodyScaleLimits[k]->fMin;
				bsl->fMax = sdf->eaBodyScaleLimits[k]->fMax;
				bsl->pcName = allocAddString(sdf->eaBodyScaleLimits[k]->pcName);
				eaPush(&newSpecies->eaBodyScaleLimits, bsl);
			}
		}
	}
	else if (sdf->eType == kSpeciesDefiningType_BoneScale)
	{
		for (k = eaSize(&sdf->eaBoneScaleLimits)-1; k >= 0; --k)
		{
			for (j = eaSize(&newSpecies->eaBoneScaleLimits)-1; j >= 0; --j)
			{
				if (!stricmp(newSpecies->eaBoneScaleLimits[j]->pcName,sdf->eaBoneScaleLimits[k]->pcName))
				{
					newSpecies->eaBoneScaleLimits[j]->fMin = sdf->eaBoneScaleLimits[k]->fMin;
					newSpecies->eaBoneScaleLimits[j]->fMax = sdf->eaBoneScaleLimits[k]->fMax;
					break;
				}
			}
			if (j < 0)
			{
				NOCONST(BoneScaleLimit) *bsl = StructCreateNoConst(parse_BoneScaleLimit);
				bsl->fMin = sdf->eaBoneScaleLimits[k]->fMin;
				bsl->fMax = sdf->eaBoneScaleLimits[k]->fMax;
				bsl->pcName = allocAddString(sdf->eaBoneScaleLimits[k]->pcName);
				eaPush(&newSpecies->eaBoneScaleLimits, bsl);
			}
		}
	}
	else if (sdf->eType == kSpeciesDefiningType_Height)
	{
		newSpecies->fMinHeight = sdf->fMinHeight;
		newSpecies->fMaxHeight = sdf->fMaxHeight;
	}
	else if (sdf->eType == kSpeciesDefiningType_Muscle)
	{
		newSpecies->fMinMuscle = sdf->fMinMuscle;
		newSpecies->fMaxMuscle = sdf->fMaxMuscle;
	}
}

static int featureSortCB(const SpeciesDefiningFeatureRef **pptr1, const SpeciesDefiningFeatureRef **pptr2)
{
	SpeciesDefiningFeature *sdf1 = GET_REF((*pptr1)->hSpeciesDefiningFeatureRef);
	SpeciesDefiningFeature *sdf2 = GET_REF((*pptr2)->hSpeciesDefiningFeatureRef);
	if (!sdf1) return 0;
	if (!sdf2) return 0;
	return sdf1->eType - sdf2->eType;
}

static void speciesgen_GenerateDefiningFeatures(NOCONST(SpeciesDef) *newSpecies, SpeciesGenRules *rules)
{
	static SpeciesUsedFeatures **eaUsedFeatures = NULL;
	static bool bScaleChangeSelected = false;
	int index, index2;
	void **eaSpeciesFeatureList = NULL;
	int count = 0;
	int i, j, l;

	if ((rules->eGender == Gender_Male && rules->genderIndex == 0) ||
		(rules->eGender == Gender_Female && rules->genderIndex == 0 && rules->pSpeciesFeatureMale) ||
		rules->bSpeciesFeatureEachDifferent)
	{
		//count all the available features
		if (rules->eGender == Gender_Male && rules->genderIndex == 0)
		{
			bScaleChangeSelected = false;
			eaDestroyStruct(&eaUsedFeatures, parse_SpeciesUsedFeatures);
		}
		eaCopy(&eaSpeciesFeatureList,&rules->pSpeciesGenData->eaFeaturesToUse);
		eaQSort(eaSpeciesFeatureList,featureSortCB); //We want geometries listed before materials before textures
		for(i = eaSize(&rules->pSpeciesGenData->eaFeaturesToUse)-1; i >= 0; --i)
		{
			SpeciesDefiningFeature *sdf = (SpeciesDefiningFeature*)GET_REF(rules->pSpeciesGenData->eaFeaturesToUse[i]->hSpeciesDefiningFeatureRef);
			if (!sdf) continue;
			if (GET_REF(sdf->hSkeleton) != GET_REF(newSpecies->hSkeleton)) continue;
			if (sdf->eType == kSpeciesDefiningType_Invalid || sdf->eType == kSpeciesDefiningType_Default) continue;
			if (sdf->eType == kSpeciesDefiningType_Grouped || sdf->eType == kSpeciesDefiningType_BodyScale || sdf->eType == kSpeciesDefiningType_BoneScale ||
				sdf->eType == kSpeciesDefiningType_Height || sdf->eType == kSpeciesDefiningType_Muscle)
			{
				++count;
				continue;
			}
			if (sdf->eType == kSpeciesDefiningType_Geometry)
			{
				PCGeometryDef *geo = NULL;
				SpeciesGenGeoIndex geoIndex;
				bool bExcludeBone = false;
				geoIndex.index = 0;
				do
				{
					geo = speciesgen_GetNextGeo(sdf, &geoIndex, &bExcludeBone);
					if ((!bExcludeBone) && !geo) break;
					++count;
				} while (1);
				continue;
			}
			if (sdf->eType == kSpeciesDefiningType_Material)
			{
				PCMaterialDef *mat = NULL;
				SpeciesGenMatIndex matIndex;
				matIndex.geoIndex = 0;
				matIndex.index = 0;
				do
				{
					mat = speciesgen_GetNextMat(sdf, &matIndex, NULL, NULL);
					if (!mat) break;
					++count;
				} while (1);
				continue;
			}
			if (sdf->eType == kSpeciesDefiningType_Pattern || sdf->eType == kSpeciesDefiningType_Detail || sdf->eType == kSpeciesDefiningType_Specular || 
				sdf->eType == kSpeciesDefiningType_Diffuse || sdf->eType == kSpeciesDefiningType_Movable)
			{
				PCTextureDef *tex = NULL;
				SpeciesGenTexIndex texIndex;
				texIndex.geoIndex = 0;
				texIndex.matIndex = 0;
				texIndex.index = 0;
				do
				{
					tex = speciesgen_GetNextTex(sdf, &texIndex, NULL, NULL, NULL, NULL);
					if (!tex) break;
					++count;
				} while (1);
				continue;
			}
		}
	}

	if (rules->eGender == Gender_Male && rules->genderIndex == 0)
	{
		int iRand[10];
		SpeciesDefiningFeature *sdf;
		bool bGenderDiff = false;

		//initialize
		if (randomIntRange(0, 99) < 2)
		{
			//genders have differences
			bGenderDiff = true;
			if (randomIntRange(0, 99) < 5)
			{
				rules->bSpeciesFeatureEachDifferent = true;
			}
			if (rules->iNumDefiningFeatures > 2) --rules->iNumDefiningFeatures;
		}

		for (i = 0; i < rules->iNumDefiningFeatures; ++i)
		{
			iRand[i] = randomIntRange(0, count-1);
			//sort - we want smallest first so geometries are added before materials before textures
			j = i;
			while (j > 0)
			{
				if (iRand[j] < iRand[j-1])
				{
					l = iRand[j];
					iRand[j] = iRand[j-1];
					iRand[j-1] = l;
					--j;
				}
				else break;
			}
		}

		rules->iNumSpeciesFeaturesAll = 0;
		for (i = 0; i < rules->iNumDefiningFeatures; ++i)
		{
			SpeciesDefiningFeature *sdfFemale = NULL;
			sdf = speciesgen_ChooseDefiningFeature(rules->pSpeciesGenData, iRand[i], newSpecies, &eaUsedFeatures, &bScaleChangeSelected, &index, &index2, eaSpeciesFeatureList, rules->pFemaleSkeleton, &sdfFemale);
			if (sdf)
			{
				rules->pSpeciesFeatureAllM[rules->iNumSpeciesFeaturesAll] = sdf;
				rules->pSpeciesFeatureAllF[rules->iNumSpeciesFeaturesAll] = sdfFemale;
				rules->iSpeciesFeatureAllM[rules->iNumSpeciesFeaturesAll] = index;
				rules->iSpeciesFeatureAllF[rules->iNumSpeciesFeaturesAll] = index2;
				++rules->iNumSpeciesFeaturesAll;
			}
		}

		if (bGenderDiff && !rules->bSpeciesFeatureEachDifferent)
		{
			rules->pSpeciesFeatureMale = speciesgen_ChooseDefiningFeature(rules->pSpeciesGenData, randomIntRange(0, count-1), newSpecies, &eaUsedFeatures, &bScaleChangeSelected, &rules->iSpeciesFeatureMale, NULL, eaSpeciesFeatureList, NULL, NULL);
		}
	}

	if (rules->eGender == Gender_Female && rules->genderIndex == 0 && rules->pSpeciesFeatureMale)
	{
		rules->pSpeciesFeatureFemale = speciesgen_ChooseDefiningFeature(rules->pSpeciesGenData, randomIntRange(0, count-1), newSpecies, &eaUsedFeatures, &bScaleChangeSelected, &rules->iSpeciesFeatureFemale, NULL, eaSpeciesFeatureList, NULL, NULL);
	}

	//Apply features that go to all
	for (i = 0; i < rules->iNumSpeciesFeaturesAll; ++i)
	{
		speciesgen_ApplyNewFeature(newSpecies, rules->eGender == Gender_Male ? rules->pSpeciesFeatureAllM[i] : rules->pSpeciesFeatureAllF[i], rules->eGender == Gender_Male ? rules->iSpeciesFeatureAllM[i] : rules->iSpeciesFeatureAllF[i]);
	}

	//Apply features that go to gender if any
	if (rules->pSpeciesFeatureMale && rules->eGender == Gender_Male) speciesgen_ApplyNewFeature(newSpecies, rules->pSpeciesFeatureMale, rules->iSpeciesFeatureMale);
	if (rules->pSpeciesFeatureFemale && rules->eGender == Gender_Female) speciesgen_ApplyNewFeature(newSpecies, rules->pSpeciesFeatureFemale, rules->iSpeciesFeatureFemale);

	//If required choose a random feature to add; or randomly choose to not add
	if (rules->bSpeciesFeatureEachDifferent)
	{
		int tempIndex;
		SpeciesDefiningFeature *tempDF = speciesgen_ChooseDefiningFeature(rules->pSpeciesGenData, randomIntRange(0, count-1), newSpecies, &eaUsedFeatures, &bScaleChangeSelected, &tempIndex, NULL, eaSpeciesFeatureList, NULL, NULL);
		if (tempDF) speciesgen_ApplyNewFeature(newSpecies, tempDF, tempIndex);
	}

	if (eaSpeciesFeatureList) eaDestroy(&eaSpeciesFeatureList);
}

static SpeciesDef *speciesgen_GenerateSingleSpecies(SpeciesGenRules *rules)
{
	int i, j, k, iRand;
	char text[256], num[10];
	float fTotalWeight, fRand;
	NOCONST(SpeciesDef) *newSpecies = StructCreateNoConst(parse_SpeciesDef);
	UniformGroupGen *ugg;

	if (!newSpecies) return NULL;

	// Assign species passed in values
	newSpecies->pcSpeciesName = StructAllocString(rules->speciesName);
	newSpecies->eGender = rules->eGender;
	newSpecies->bIsGenSpecies = true;

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
	newSpecies->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	newSpecies->displayNameMsg.pEditorCopy->bDoNotTranslate = true;
	newSpecies->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(rules->speciesName);
	*text = '\0';
	strcat(text, "GenSpecies.");
	strcat(text, newSpecies->pcName);
	strcat(text, ".DisplayName");
	newSpecies->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(text);
	newSpecies->displayNameMsg.bEditorCopyIsServer = true;
	newSpecies->genderNameMsg.pEditorCopy = StructCreate(parse_Message);
	newSpecies->genderNameMsg.bEditorCopyIsServer = true;
	*text = '\0';
	strcat(text, "GenSpecies.");
	strcat(text, newSpecies->pcName);
	strcat(text, ".GenderName");
	newSpecies->genderNameMsg.pEditorCopy->pcMessageKey = allocAddString(text);

	// Gender display name
	if (rules->eGender == Gender_Male)
	{
		if (rules->numMales > 1)
		{
			if (rules->genderIndex == 0)
			{
				newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Primary Male");
			}
			else if (rules->genderIndex == 1)
			{
				newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Secondary Male");
			}
			else
			{
				newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Tertiary Male");
			}
		}
		else
		{
			newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Male");
		}
	}
	else
	{
		if (rules->numFemales > 1)
		{
			if (rules->genderIndex == 0)
			{
				newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Primary Female");
			}
			else if (rules->genderIndex == 1)
			{
				newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Secondary Female");
			}
			else
			{
				newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Tertiary Female");
			}
		}
		else
		{
			newSpecies->genderNameMsg.pEditorCopy->pcDefaultString = StructAllocString("Female");
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

	// Assign Default Values for Species
	if (rules->eGender == Gender_Male)
	{
		for (i = eaSize(&rules->pSpeciesFeatureDefMale->eaCategories)-1; i >= 0; --i)
		{
			//Ugh - Find all Geos in this category
			PCCategory *cat = GET_REF(rules->pSpeciesFeatureDefMale->eaCategories[i]->hCategory);
			DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeGeometry");
			for(j = eaSize(&deas->ppReferents)-1; j >= 0; --j)
			{
				PCGeometryDef *geo = (PCGeometryDef*)deas->ppReferents[j];
				for (k = eaSize(&geo->eaCategories)-1; k >= 0; --k)
				{
					if (GET_REF(geo->eaCategories[k]->hCategory) == cat)
					{
						break;
					}
				}
				if (k >= 0)
				{
					NOCONST(GeometryLimits) *gl = StructCreateNoConst(parse_GeometryLimits);
					SET_HANDLE_FROM_REFERENT("CostumeGeometry", geo, gl->hGeo);
					eaPush(&newSpecies->eaGeometries, gl);
				}
			}
		}
		for (i = eaSize(&rules->pSpeciesFeatureDefMale->eaGeometries)-1; i >= 0; --i)
		{
			NOCONST(GeometryLimits) *gl = StructCloneDeConst(parse_GeometryLimits, rules->pSpeciesFeatureDefMale->eaGeometries[i]);
			eaPush(&newSpecies->eaGeometries, gl);
		}
		if (GET_REF(rules->pSpeciesFeatureDefMale->hSpeciesDefault))
		{
			NOCONST(SpeciesDef) *pSpecies = CONTAINER_NOCONST(SpeciesDef, GET_REF(rules->pSpeciesFeatureDefMale->hSpeciesDefault));
			eaCopyStructsNoConst(&pSpecies->eaBodyScaleLimits,&newSpecies->eaBodyScaleLimits,parse_BodyScaleLimit);
			eaCopyStructsNoConst(&pSpecies->eaBoneScaleLimits,&newSpecies->eaBoneScaleLimits,parse_BoneScaleLimit);
			eaCopyStructsNoConst(&pSpecies->eaStanceInfo,&newSpecies->eaStanceInfo,parse_PCStanceInfo);
			newSpecies->pcDefaultStance = allocAddString(pSpecies->pcDefaultStance);
			newSpecies->fMinHeight = pSpecies->fMinHeight;
			newSpecies->fMaxHeight = pSpecies->fMaxHeight;
			newSpecies->fMinMuscle = pSpecies->fMinMuscle;
			newSpecies->fMaxMuscle = pSpecies->fMaxMuscle;
		}
		else
		{
			for (i = eaSize(&rules->pMaleSkeleton->eaBodyScaleInfo)-1; i >= 0; --i)
			{
				PCBodyScaleInfo *bsi = rules->pMaleSkeleton->eaBodyScaleInfo[i];
				NOCONST(BodyScaleLimit) *bsl = StructCreateNoConst(parse_BodyScaleLimit);
				bsl->pcName = allocAddString(bsi->pcName);
				bsl->fMin = rules->pMaleSkeleton->eafPlayerMinBodyScales[i];
				bsl->fMax = rules->pMaleSkeleton->eafPlayerMaxBodyScales[i];
				eaPush(&newSpecies->eaBodyScaleLimits, bsl);
			}
			for (i = eaSize(&rules->pMaleSkeleton->eaScaleInfoGroups)-1; i >= 0; --i)
			{
				PCScaleInfoGroup *sig = rules->pMaleSkeleton->eaScaleInfoGroups[i];
				for (j = eaSize(&sig->eaScaleInfo)-1; j >= 0; --j)
				{
					PCScaleInfo *si = sig->eaScaleInfo[j];
					NOCONST(BoneScaleLimit) *bsl = StructCreateNoConst(parse_BoneScaleLimit);
					bsl->pcName = allocAddString(si->pcName);
					bsl->fMin = si->fPlayerMin;
					bsl->fMax = si->fPlayerMax;
					eaPush(&newSpecies->eaBoneScaleLimits, bsl);
				}
			}
			for (j = eaSize(&rules->pMaleSkeleton->eaScaleInfo)-1; j >= 0; --j)
			{
				PCScaleInfo *si = rules->pMaleSkeleton->eaScaleInfo[j];
				NOCONST(BoneScaleLimit) *bsl = StructCreateNoConst(parse_BoneScaleLimit);
				bsl->pcName = allocAddString(si->pcName);
				bsl->fMin = si->fPlayerMin;
				bsl->fMax = si->fPlayerMax;
				eaPush(&newSpecies->eaBoneScaleLimits, bsl);
			}
			eaCopyStructsDeConst(&rules->pMaleSkeleton->eaStanceInfo,&newSpecies->eaStanceInfo,parse_PCStanceInfo);
			newSpecies->pcDefaultStance = allocAddString(rules->pMaleSkeleton->pcDefaultStance);
			newSpecies->fMinHeight = rules->pMaleSkeleton->fPlayerMinHeight;
			newSpecies->fMaxHeight = rules->pMaleSkeleton->fPlayerMaxHeight;
			newSpecies->fMinMuscle = rules->pMaleSkeleton->fPlayerMinMuscle;
			newSpecies->fMaxMuscle = rules->pMaleSkeleton->fPlayerMaxMuscle;
		}
	}
	else
	{
		for (i = eaSize(&rules->pSpeciesFeatureDefFemale->eaCategories)-1; i >= 0; --i)
		{
			//Ugh - Find all Geos in this category
			PCCategory *cat = GET_REF(rules->pSpeciesFeatureDefFemale->eaCategories[i]->hCategory);
			DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeGeometry");
			for(j = eaSize(&deas->ppReferents)-1; j >= 0; --j)
			{
				PCGeometryDef *geo = (PCGeometryDef*)deas->ppReferents[j];
				for (k = eaSize(&geo->eaCategories)-1; k >= 0; --k)
				{
					if (GET_REF(geo->eaCategories[k]->hCategory) == cat)
					{
						break;
					}
				}
				if (k >= 0)
				{
					NOCONST(GeometryLimits) *gl = StructCreateNoConst(parse_GeometryLimits);
					SET_HANDLE_FROM_REFERENT("CostumeGeometry", geo, gl->hGeo);
					eaPush(&newSpecies->eaGeometries, gl);
				}
			}
		}
		for (i = eaSize(&rules->pSpeciesFeatureDefFemale->eaGeometries)-1; i >= 0; --i)
		{
			NOCONST(GeometryLimits) *gl = StructCloneDeConst(parse_GeometryLimits, rules->pSpeciesFeatureDefFemale->eaGeometries[i]);
			eaPush(&newSpecies->eaGeometries, gl);
		}
		if (GET_REF(rules->pSpeciesFeatureDefFemale->hSpeciesDefault))
		{
			NOCONST(SpeciesDef) *pSpecies = CONTAINER_NOCONST(SpeciesDef, GET_REF(rules->pSpeciesFeatureDefFemale->hSpeciesDefault));
			eaCopyStructsNoConst(&pSpecies->eaBodyScaleLimits,&newSpecies->eaBodyScaleLimits,parse_BodyScaleLimit);
			eaCopyStructsNoConst(&pSpecies->eaBoneScaleLimits,&newSpecies->eaBoneScaleLimits,parse_BoneScaleLimit);
			eaCopyStructsNoConst(&pSpecies->eaStanceInfo,&newSpecies->eaStanceInfo,parse_PCStanceInfo);
			newSpecies->pcDefaultStance = allocAddString(pSpecies->pcDefaultStance);
			newSpecies->fMinHeight = pSpecies->fMinHeight;
			newSpecies->fMaxHeight = pSpecies->fMaxHeight;
			newSpecies->fMinMuscle = pSpecies->fMinMuscle;
			newSpecies->fMaxMuscle = pSpecies->fMaxMuscle;
		}
		else
		{
			for (i = eaSize(&rules->pFemaleSkeleton->eaBodyScaleInfo)-1; i >= 0; --i)
			{
				PCBodyScaleInfo *bsi = rules->pFemaleSkeleton->eaBodyScaleInfo[i];
				NOCONST(BodyScaleLimit) *bsl = StructCreateNoConst(parse_BodyScaleLimit);
				bsl->pcName = allocAddString(bsi->pcName);
				bsl->fMin = rules->pFemaleSkeleton->eafPlayerMinBodyScales[i];
				bsl->fMax = rules->pFemaleSkeleton->eafPlayerMaxBodyScales[i];
				eaPush(&newSpecies->eaBodyScaleLimits, bsl);
			}
			for (i = eaSize(&rules->pFemaleSkeleton->eaScaleInfoGroups)-1; i >= 0; --i)
			{
				PCScaleInfoGroup *sig = rules->pFemaleSkeleton->eaScaleInfoGroups[i];
				for (j = eaSize(&sig->eaScaleInfo)-1; j >= 0; --j)
				{
					PCScaleInfo *si = sig->eaScaleInfo[j];
					NOCONST(BoneScaleLimit) *bsl = StructCreateNoConst(parse_BoneScaleLimit);
					bsl->pcName = allocAddString(si->pcName);
					bsl->fMin = si->fPlayerMin;
					bsl->fMax = si->fPlayerMax;
					eaPush(&newSpecies->eaBoneScaleLimits, bsl);
				}
			}
			for (j = eaSize(&rules->pFemaleSkeleton->eaScaleInfo)-1; j >= 0; --j)
			{
				PCScaleInfo *si = rules->pFemaleSkeleton->eaScaleInfo[j];
				NOCONST(BoneScaleLimit) *bsl = StructCreateNoConst(parse_BoneScaleLimit);
				bsl->pcName = allocAddString(si->pcName);
				bsl->fMin = si->fPlayerMin;
				bsl->fMax = si->fPlayerMax;
				eaPush(&newSpecies->eaBoneScaleLimits, bsl);
			}
			eaCopyStructsDeConst(&rules->pFemaleSkeleton->eaStanceInfo,&newSpecies->eaStanceInfo,parse_PCStanceInfo);
			newSpecies->pcDefaultStance = allocAddString(rules->pFemaleSkeleton->pcDefaultStance);
			newSpecies->fMinHeight = rules->pFemaleSkeleton->fPlayerMinHeight;
			newSpecies->fMaxHeight = rules->pFemaleSkeleton->fPlayerMaxHeight;
			newSpecies->fMinMuscle = rules->pFemaleSkeleton->fPlayerMinMuscle;
			newSpecies->fMaxMuscle = rules->pFemaleSkeleton->fPlayerMaxMuscle;
		}
	}

	//Choose naming rules
	speciesgen_GenerateNameRules(newSpecies, rules);

	//Choose species color
	if (rules->pCommonColor)
	{
		SET_HANDLE_FROM_REFERENT("CostumeColors", rules->pCommonColor, newSpecies->hSkinColorSet);
	}
	else if (rules->eGender == Gender_Male && rules->pCommonMaleColor)
	{
		SET_HANDLE_FROM_REFERENT("CostumeColors", rules->pCommonMaleColor, newSpecies->hSkinColorSet);
	}
	else if (rules->eGender == Gender_Female && rules->pCommonFemaleColor)
	{
		SET_HANDLE_FROM_REFERENT("CostumeColors", rules->pCommonFemaleColor, newSpecies->hSkinColorSet);
	}
	else
	{
		SET_HANDLE_FROM_REFERENT("CostumeColors", speciesgen_GetRandomColorSet(), newSpecies->hSkinColorSet);
	}

	//Choose defining geos or body shape rules
	speciesgen_GenerateDefiningFeatures(newSpecies, rules);

	//Choose voices
	{
		newSpecies->bAllowAllVoices = false;
		if (rules->eGender == Gender_Male)
		{
			if (eaSize(&rules->pSpeciesGenData->eaAllowedMaleVoices))
			{
				iRand = randomIntRange(0, eaSize(&rules->pSpeciesGenData->eaAllowedMaleVoices)-1);
				eaPush(&newSpecies->eaAllowedVoices, StructCloneDeConst(parse_VoiceRef,rules->pSpeciesGenData->eaAllowedMaleVoices[iRand]));
			}
			else
			{
				PCVoice **eaVoices = NULL;
				DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeVoice");
				for(j = eaSize(&deas->ppReferents)-1; j >= 0; --j)
				{
					if (((PCVoice*)deas->ppReferents[j])->pcUnlockCode && *((PCVoice*)deas->ppReferents[j])->pcUnlockCode) continue;
					if (((PCVoice*)deas->ppReferents[j])->eGender == Gender_Male)
					{
						eaPush(&eaVoices, deas->ppReferents[j]);
					}
				}
				if (eaSize(&eaVoices))
				{
					NOCONST(VoiceRef) *v;
					iRand = randomIntRange(0, eaSize(&eaVoices)-1);
					v = StructCreateNoConst(parse_VoiceRef);
					SET_HANDLE_FROM_REFERENT("CostumeVoice", eaVoices[iRand], v->hVoice);
					eaPush(&newSpecies->eaAllowedVoices, v);
				}
				eaDestroy(&eaVoices);
			}
		}
		else
		{
			if (eaSize(&rules->pSpeciesGenData->eaAllowedFemaleVoices))
			{
				iRand = randomIntRange(0, eaSize(&rules->pSpeciesGenData->eaAllowedFemaleVoices)-1);
				eaPush(&newSpecies->eaAllowedVoices, StructCloneDeConst(parse_VoiceRef,rules->pSpeciesGenData->eaAllowedFemaleVoices[iRand]));
			}
			else
			{
				PCVoice **eaVoices = NULL;
				DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeVoice");
				for(j = eaSize(&deas->ppReferents)-1; j >= 0; --j)
				{
					if (((PCVoice*)deas->ppReferents[j])->pcUnlockCode && *((PCVoice*)deas->ppReferents[j])->pcUnlockCode) continue;
					if (((PCVoice*)deas->ppReferents[j])->eGender == Gender_Female)
					{
						eaPush(&eaVoices, deas->ppReferents[j]);
					}
				}
				if (eaSize(&eaVoices))
				{
					NOCONST(VoiceRef) *v;
					iRand = randomIntRange(0, eaSize(&eaVoices)-1);
					v = StructCreateNoConst(parse_VoiceRef);
					SET_HANDLE_FROM_REFERENT("CostumeVoice", eaVoices[iRand], v->hVoice);
					eaPush(&newSpecies->eaAllowedVoices, v);
				}
				eaDestroy(&eaVoices);
			}
		}
	}

	//
	//Choose costume packages
	//
	if (rules->eGender == Gender_Male && rules->genderIndex == 0)
	{
		fTotalWeight = 0;
		for (i = eaSize(&rules->pSpeciesGenData->eaUniformGroups)-1; i >= 0; --i)
		{
			fTotalWeight += rules->pSpeciesGenData->eaUniformGroups[i]->fWeight;
		}
		fRand = randomPositiveF32() * fTotalWeight;
		i = 0;
		rules->iUniformSet = 0;
		while(i < eaSize(&rules->pSpeciesGenData->eaUniformGroups))
		{
			if(fRand < rules->pSpeciesGenData->eaUniformGroups[i]->fWeight)
			{
				rules->iUniformSet = i;
				break;
			}
			fRand -= rules->pSpeciesGenData->eaUniformGroups[i]->fWeight;
			i++;
		}
	}
	if (eaSize(&rules->pSpeciesGenData->eaUniformGroups))
	{
		ugg = rules->pSpeciesGenData->eaUniformGroups[rules->iUniformSet];
		if (ugg)
		{
			for (i = eaSize(&ugg->eaUniforms)-1; i >= 0; --i)
			{
				PlayerCostume *pc = GET_REF(ugg->eaUniforms[i]->hPlayerCostume);
				if (!pc) continue;
				if (rules->eGender == Gender_Male && GET_REF(pc->hSkeleton) != rules->pMaleSkeleton) continue;
				if (rules->eGender == Gender_Female && GET_REF(pc->hSkeleton) != rules->pFemaleSkeleton) continue;
				for (j = eaSize(&pc->eaParts)-1; j >= 0; --j)
				{
					NOCONST(GeometryLimits) *gl;
					PCGeometryDef *geo = GET_REF(pc->eaParts[j]->hGeoDef);
					if (!geo) continue;
					for (k = eaSize(&newSpecies->eaGeometries)-1; k >= 0; --k)
					{
						gl = newSpecies->eaGeometries[k];
						if (GET_REF(gl->hGeo) == geo) break;
					}
					if (k >= 0) continue;
					gl = StructCreateNoConst(parse_GeometryLimits);
					if (gl)
					{
						SET_HANDLE_FROM_REFERENT("CostumeGeometry",geo,gl->hGeo);
						eaPush(&newSpecies->eaGeometries, gl);
					}
				}
			}
		}
	}

	return (SpeciesDef*)newSpecies;
}

void speciesgen_GenerateSpecies(SpeciesGenData *pSpeciesGenData, SpeciesDef ***peaSpeciesList, NOCONST(PhonemeSet) ***peaPhonemeVowels, NOCONST(PhonemeSet) ***peaPhonemeCons,
								NameTemplateListNoRef ***peaNameRules1, NameTemplateListNoRef ***peaNameRules2, NameTemplateListNoRef ***peaNameRules3, NameTemplateListNoRef ***peaNameRules4)
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
	eaClear(peaPhonemeVowels);
	eaClear(peaPhonemeCons);
	eaClear(peaNameRules1);
	eaClear(peaNameRules2);
	eaClear(peaNameRules3);
	eaClear(peaNameRules4);

	//Set up rules
	memset(&rules, 0, sizeof(SpeciesGenRules));
	rules.pSpeciesGenData = pSpeciesGenData;
	rules.pMaleSkeleton = GET_REF(pSpeciesGenData->hMaleSkeleton);
	rules.pFemaleSkeleton = GET_REF(pSpeciesGenData->hFemaleSkeleton);
	rules.peaPhonemeVowels = peaPhonemeVowels;
	rules.peaPhonemeCons = peaPhonemeCons;

	//Create a name gen structure
	speciesNameRules = RefSystem_ReferentFromString("NameTemplateList", "Common");
	item = StructCreate(parse_NameTemplateListRef);
	if (!item) goto cleanup;
	SET_HANDLE_FROM_REFERENT("NameTemplateList", speciesNameRules, item->hNameTemplateList);
	eaPush(&refList, item);

	//Create a new species name
	do 
	{
		temp = namegen_GenerateFullName(refList, NULL);
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

	// Choose defining features
	rules.iNumDefiningFeatures = randomIntRange(2, 5);

	//Choose skin color rules
	if (randomIntRange(0, 99) < 85)
	{
		rules.pCommonColor = RefSystem_ReferentFromString("CostumeColors", "GenSpeciesColorSet000");
	}
	else
	{
		if (randomIntRange(0, 99) < 2)
		{
			if (randomIntRange(0, 99) < 85)
			{
				rules.pCommonMaleColor = speciesgen_GetRandomColorSet();
				rules.pCommonFemaleColor = speciesgen_GetRandomColorSet();
			}
			--rules.iNumDefiningFeatures;
		}
		else
		{
			rules.pCommonColor = speciesgen_GetRandomColorSet();
			--rules.iNumDefiningFeatures;
		}
	}

	rules.pSpeciesFeatureDefMale = GET_REF(pSpeciesGenData->hDefaultMale);
	rules.pSpeciesFeatureDefFemale = GET_REF(pSpeciesGenData->hDefaultFemale);
	if ((!rules.pSpeciesFeatureDefMale) || (!rules.pSpeciesFeatureDefFemale))
	{
		goto cleanup;
	}

	//Generate gender species
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

	if (eaSize(&rules.eaGenderOwner))
	{
		eaPushEArray(rules.peaPhonemeCons, &rules.eaGenderOwner);
		eaDestroy(&rules.eaGenderOwner);
	}
	if (eaSize(&rules.eaGender1)) eaDestroy(&rules.eaGender1);
	if (eaSize(&rules.eaGender2)) eaDestroy(&rules.eaGender2);
	if (eaSize(&rules.eaGender3)) eaDestroy(&rules.eaGender3);
	if (eaSize(&rules.eaGender4)) eaDestroy(&rules.eaGender4);
	if (rules.firstNameRules1) eaPush(peaNameRules1, rules.firstNameRules1);
	if (rules.lastNameRules1) eaPush(peaNameRules1, rules.lastNameRules1);
	if (rules.firstNameRules2) eaPush(peaNameRules2, rules.firstNameRules2);
	if (rules.lastNameRules2) eaPush(peaNameRules2, rules.lastNameRules2);
	if (rules.firstNameRules3) eaPush(peaNameRules3, rules.firstNameRules3);
	if (rules.lastNameRules3) eaPush(peaNameRules3, rules.lastNameRules3);
	if (rules.firstNameRules4) eaPush(peaNameRules4, rules.firstNameRules4);
	if (rules.lastNameRules4) eaPush(peaNameRules4, rules.lastNameRules4);

cleanup:
	if (rules.speciesName) StructFreeString((char*)rules.speciesName);
}

CritterGroup *speciesgen_GenerateCritterGroup(const char *pcSpeciesName)
{
	char text[1024];
	CritterGroup *pCritterGroup = StructCreate(parse_CritterGroup);

	*text = '\0';
	strcat(text, "GenSpecies.");
	strcat(text, pcSpeciesName);
	pCritterGroup->pchName = (char*)allocAddString(text);

	pCritterGroup->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
	pCritterGroup->displayNameMsg.pEditorCopy->bDoNotTranslate = true;
	pCritterGroup->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(pcSpeciesName);
	*text = '\0';
	strcat(text, "GenSpecies.CritterGroup.");
	strcat(text, pcSpeciesName);
	strcat(text, ".DisplayName");
	pCritterGroup->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(text);
	pCritterGroup->displayNameMsg.bEditorCopyIsServer = true;

	return pCritterGroup;
}

void CritterGenerateNewAttribKeyBlock(CritterDef *pDef);

void speciesgen_GenerateCritters(SpeciesGenData *pSpeciesGenData, CritterGroup *pCritterGroup, SpeciesDef **eaSpeciesList, CritterDef ***peaCritterDefList)
{
	int i, j, k;
	float fRand, fTotalWeight = 0;
	CritterGroupGen *cgg = NULL;
	CritterDef *pCritterDef;
	char text[1024];

	//Pick random critter group
	for (i = eaSize(&pSpeciesGenData->eaCritterGroupGen)-1; i >= 0; --i)
	{
		fTotalWeight += pSpeciesGenData->eaCritterGroupGen[i]->fWeight;
	}

	fRand = randomPositiveF32() * fTotalWeight;
	i = 0;
	while(i < eaSize(&pSpeciesGenData->eaCritterGroupGen))
	{
		if(fRand < pSpeciesGenData->eaCritterGroupGen[i]->fWeight)
		{
			cgg = pSpeciesGenData->eaCritterGroupGen[i];
			break;
		}
		fRand -= pSpeciesGenData->eaCritterGroupGen[i]->fWeight;
		i++;
	}

	if (!cgg) return;

	//make each critter def for each species
	for (i = eaSize(&cgg->eaCritterDef)-1; i >= 0; --i)
	{
		CritterDef *cd = GET_REF(cgg->eaCritterDef[i]->hCritterDef);
		if (!cd) continue;
		ANALYSIS_ASSUME(cd != NULL);
		for (j = eaSize(&eaSpeciesList)-1; j >= 0; --j)
		{
			bool isgood = false;

			pCritterDef = StructCreate(parse_CritterDef);

			pCritterDef->iMinLevel = 0;
			pCritterDef->iMaxLevel = -1;

			pCritterDef->pInheritance = StructCreate(parse_InheritanceData);
			pCritterDef->pInheritance->pParentName = StructAllocString(cd->pchName);

			do 
			{
				CritterGenerateNewAttribKeyBlock(pCritterDef);
				for (k = eaSize(peaCritterDefList)-1; k >= 0; --k)
				{
					if ((*peaCritterDefList)[k]->iKeyBlock == pCritterDef->iKeyBlock)
					{
						break;
					}
				}
				if (k < 0) isgood = true;
			} while (!isgood);

			*text = '\0';
			strcat(text, eaSpeciesList[j]->pcName);
			strcat(text, "_");
			ANALYSIS_ASSUME(cd->pchName != NULL); // I have no idea if this is correct
			strcat(text, cd->pchName);
			pCritterDef->pchName = (char*)allocAddString(text);
			StructInherit_CreateFieldOverride(parse_CritterDef, pCritterDef, ".Name");

			*text = '\0';
			strcat(text, "speciesGen/");
			strcat(text, pSpeciesGenData->pcName);
			strcat(text, "/");
			strcat(text, eaSpeciesList[0]->pcSpeciesName);
			pCritterDef->pchScope = StructAllocString(text);
			StructInherit_CreateFieldOverride(parse_CritterDef, pCritterDef, ".Scope");

			SET_HANDLE_FROM_STRING("CritterGroup", pCritterGroup->pchName,pCritterDef->hGroup);
			StructInherit_CreateFieldOverride(parse_CritterDef, pCritterDef, ".GroupName");

			SET_HANDLE_FROM_STRING("Species", eaSpeciesList[j]->pcName,pCritterDef->hSpecies);
			StructInherit_CreateFieldOverride(parse_CritterDef, pCritterDef, ".Species");

			pCritterDef->displayNameMsg.pEditorCopy = StructCreate(parse_Message);
			pCritterDef->displayNameMsg.pEditorCopy->bDoNotTranslate = true;
			pCritterDef->displayNameMsg.pEditorCopy->pcDefaultString = StructAllocString(eaSpeciesList[0]->pcSpeciesName);
			*text = '\0';
			strcat(text, "CritterDef.");
			strcat(text, eaSpeciesList[j]->pcName);
			strcat(text, "_");
			strcat(text, cd->pchName);
			pCritterDef->displayNameMsg.pEditorCopy->pcMessageKey = allocAddString(text);
			pCritterDef->displayNameMsg.bEditorCopyIsServer = true;
			StructInherit_CreateFieldOverride(parse_CritterDef, pCritterDef, ".displayNameMsg.EditorCopy");

			pCritterDef->bGenerateRandomCostume = true;
			StructInherit_CreateFieldOverride(parse_CritterDef, pCritterDef, ".GenerateRandomCostume");

			eaPush(peaCritterDefList, pCritterDef);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(GenerateSpeciesData) ACMD_ACCESSLEVEL(9);
void speciesgen_GenerateSpeciesData(const char *pcSpeciesGenData)
{
	PCSkeletonDef *pMaleSkeleton = NULL, *pFemaleSkeleton = NULL;
	SpeciesDef **eaSpeciesList = NULL;
	PhonemeSet **eaVowels = NULL, **eaCons = NULL;
	NameTemplateListNoRef **eaRulesGender1 = NULL, **eaRulesGender2 = NULL, **eaRulesGender3 = NULL, **eaRulesGender4 = NULL;
	int i, j, k, l;
	char text[2048];
	NameTemplateList **eaTempStore = NULL;
	SpeciesGenData *pSpeciesGenData = RefSystem_ReferentFromString("SpeciesGenData", pcSpeciesGenData);
	CritterGroup *pCritterGroup = NULL, **eaCritterGroups= NULL;
	CritterDef **eaCritterDefList = NULL;

	if (!pSpeciesGenData) return;

	pMaleSkeleton = GET_REF(pSpeciesGenData->hMaleSkeleton);
	pFemaleSkeleton = GET_REF(pSpeciesGenData->hFemaleSkeleton);

	if ((!pMaleSkeleton) || (!pFemaleSkeleton)) return;

	sprintf(text, "%s/defs/Choice/speciesGen/%s", fileDataDir(), pSpeciesGenData->pcName);
	fileMoveToRecycleBin(text);
	sprintf(text, "%s/defs/critters/speciesGen/%s", fileDataDir(), pSpeciesGenData->pcName);
	fileMoveToRecycleBin(text);
	sprintf(text, "%s/defs/CritterGroups/speciesGen/%s", fileDataDir(), pSpeciesGenData->pcName);
	fileMoveToRecycleBin(text);
	sprintf(text, "%s/defs/species/speciesGen/%s", fileDataDir(), pSpeciesGenData->pcName);
	fileMoveToRecycleBin(text);
	sprintf(text, "%s/defs/nameGen/speciesGen/nameGen/%s", fileDataDir(), pSpeciesGenData->pcName);
	fileMoveToRecycleBin(text);
	sprintf(text, "%s/defs/nameGen/speciesGen/phoneme/%s", fileDataDir(), pSpeciesGenData->pcName);
	fileMoveToRecycleBin(text);

	for (i = 0; i < pSpeciesGenData->iNumToGenerate; ++i)
	{
		ResourceActionList tempList = {0};

		//Generate
		speciesgen_GenerateSpecies(pSpeciesGenData, &eaSpeciesList, &(NOCONST(PhonemeSet)**)eaVowels, &(NOCONST(PhonemeSet)**)eaCons, &eaRulesGender1, &eaRulesGender2, &eaRulesGender3, &eaRulesGender4);

		if (eaSize(&eaSpeciesList))
		{
			*text = '\0';
			strcat(text, "speciesGen/phoneme/");
			strcat(text, pSpeciesGenData->pcName);
			strcat(text, "/");
			strcat(text, eaSpeciesList[0]->pcSpeciesName);

			//Save PhonemeSets
			resSetDictionaryEditMode(g_hPhonemeSetDict, true);
			resSetDictionaryEditMode(gMessageDict, true);
			for (j = eaSize(&eaVowels)-1; j >= 0; --j)
			{
				eaVowels[j]->pcScope = allocAddString(text);
				eaVowels[j]->bIsNotInDict = 0;
				resAddRequestLockResource(&tempList, g_hPhonemeSetDict, eaVowels[j]->pcName, eaVowels[j]);
				resAddRequestSaveResource(&tempList, g_hPhonemeSetDict, eaVowels[j]->pcName, eaVowels[j]);
			}
			for (j = eaSize(&eaCons)-1; j >= 0; --j)
			{
				eaCons[j]->pcScope = allocAddString(text);
				eaCons[j]->bIsNotInDict = 0;
				resAddRequestLockResource(&tempList, g_hPhonemeSetDict, eaCons[j]->pcName, eaCons[j]);
				resAddRequestSaveResource(&tempList, g_hPhonemeSetDict, eaCons[j]->pcName, eaCons[j]);
			}
			resRequestResourceActions(&tempList);

			//Save NameTemplateLists
			*text = '\0';
			strcat(text, "speciesGen/nameGen/");
			strcat(text, pSpeciesGenData->pcName);
			strcat(text, "/");
			strcat(text, eaSpeciesList[0]->pcSpeciesName);

			eaClear(&tempList.ppActions);
			tempList.eResult = 0;
			resSetDictionaryEditMode(g_hNameTemplateListDict, true);
			for (j = eaSize(&eaRulesGender1)-1; j >= 0; --j)
			{
				NameTemplateList *ntl = StructCreate(parse_NameTemplateList);
				ntl->pcName = allocAddString(eaRulesGender1[j]->pcName);
				for (k = eaSize(&eaRulesGender1[j]->eaNameTemplates)-1; k >= 0; --k)
				{
					NameTemplate *nt = StructCreate(parse_NameTemplate);
					nt->fWeight = eaRulesGender1[j]->eaNameTemplates[k]->fWeight;
					for (l = eaSize(&eaRulesGender1[j]->eaNameTemplates[k]->eaPhonemeSets)-1; l >= 0; --l)
					{
						PhonemeSetRef *psr = StructCreate(parse_PhonemeSetRef);
						SET_HANDLE_FROM_STRING(g_hPhonemeSetDict, eaRulesGender1[j]->eaNameTemplates[k]->eaPhonemeSets[l]->pcName, psr->hPhonemeSet);
						eaPush(&nt->eaPhonemeSets, psr);
					}
					eaPush(&ntl->eaNameTemplates, nt);
				}
				ntl->pcScope = allocAddString(text);
				resAddRequestLockResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				resAddRequestSaveResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				eaPush(&eaTempStore, ntl);
			}
			for (j = eaSize(&eaRulesGender2)-1; j >= 0; --j)
			{
				NameTemplateList *ntl = StructCreate(parse_NameTemplateList);
				ntl->pcName = allocAddString(eaRulesGender2[j]->pcName);
				for (k = eaSize(&eaRulesGender2[j]->eaNameTemplates)-1; k >= 0; --k)
				{
					NameTemplate *nt = StructCreate(parse_NameTemplate);
					nt->fWeight = eaRulesGender2[j]->eaNameTemplates[k]->fWeight;
					for (l = eaSize(&eaRulesGender2[j]->eaNameTemplates[k]->eaPhonemeSets)-1; l >= 0; --l)
					{
						PhonemeSetRef *psr = StructCreate(parse_PhonemeSetRef);
						SET_HANDLE_FROM_STRING(g_hPhonemeSetDict, eaRulesGender2[j]->eaNameTemplates[k]->eaPhonemeSets[l]->pcName, psr->hPhonemeSet);
						eaPush(&nt->eaPhonemeSets, psr);
					}
					eaPush(&ntl->eaNameTemplates, nt);
				}
				ntl->pcScope = allocAddString(text);
				resAddRequestLockResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				resAddRequestSaveResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				eaPush(&eaTempStore, ntl);
			}
			for (j = eaSize(&eaRulesGender3)-1; j >= 0; --j)
			{
				NameTemplateList *ntl = StructCreate(parse_NameTemplateList);
				ntl->pcName = allocAddString(eaRulesGender3[j]->pcName);
				for (k = eaSize(&eaRulesGender3[j]->eaNameTemplates)-1; k >= 0; --k)
				{
					NameTemplate *nt = StructCreate(parse_NameTemplate);
					nt->fWeight = eaRulesGender3[j]->eaNameTemplates[k]->fWeight;
					for (l = eaSize(&eaRulesGender3[j]->eaNameTemplates[k]->eaPhonemeSets)-1; l >= 0; --l)
					{
						PhonemeSetRef *psr = StructCreate(parse_PhonemeSetRef);
						SET_HANDLE_FROM_STRING(g_hPhonemeSetDict, eaRulesGender3[j]->eaNameTemplates[k]->eaPhonemeSets[l]->pcName, psr->hPhonemeSet);
						eaPush(&nt->eaPhonemeSets, psr);
					}
					eaPush(&ntl->eaNameTemplates, nt);
				}
				ntl->pcScope = allocAddString(text);
				resAddRequestLockResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				resAddRequestSaveResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				eaPush(&eaTempStore, ntl);
			}
			for (j = eaSize(&eaRulesGender4)-1; j >= 0; --j)
			{
				NameTemplateList *ntl = StructCreate(parse_NameTemplateList);
				ntl->pcName = allocAddString(eaRulesGender4[j]->pcName);
				for (k = eaSize(&eaRulesGender4[j]->eaNameTemplates)-1; k >= 0; --k)
				{
					NameTemplate *nt = StructCreate(parse_NameTemplate);
					nt->fWeight = eaRulesGender4[j]->eaNameTemplates[k]->fWeight;
					for (l = eaSize(&eaRulesGender4[j]->eaNameTemplates[k]->eaPhonemeSets)-1; l >= 0; --l)
					{
						PhonemeSetRef *psr = StructCreate(parse_PhonemeSetRef);
						SET_HANDLE_FROM_STRING(g_hPhonemeSetDict, eaRulesGender4[j]->eaNameTemplates[k]->eaPhonemeSets[l]->pcName, psr->hPhonemeSet);
						eaPush(&nt->eaPhonemeSets, psr);
					}
					eaPush(&ntl->eaNameTemplates, nt);
				}
				ntl->pcScope = allocAddString(text);
				resAddRequestLockResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				resAddRequestSaveResource(&tempList, g_hNameTemplateListDict, ntl->pcName, ntl);
				eaPush(&eaTempStore, ntl);
			}
			resRequestResourceActions(&tempList);

			//Save Species
			*text = '\0';
			strcat(text, "speciesGen/");
			strcat(text, pSpeciesGenData->pcName);
			strcat(text, "/");
			strcat(text, eaSpeciesList[0]->pcSpeciesName);

			eaClear(&tempList.ppActions);
			tempList.eResult = 0;
			resSetDictionaryEditMode(g_hSpeciesDict, true);
			for (j = 0; j < eaSize(&eaSpeciesList); ++j)
			{
				eaSpeciesList[j]->pcScope = allocAddString(text);
				eaSpeciesList[j]->displayNameMsg.pEditorCopy->pcScope = allocAddString(text);
				eaSpeciesList[j]->genderNameMsg.pEditorCopy->pcScope = allocAddString(text);
				switch (j)
				{
				case 0:
					for (k = 0; k < eaSize(&eaRulesGender1); ++k)
					{
						NOCONST(NameTemplateListRef) *ntlr = StructCreateNoConst(parse_NameTemplateListRef);
						SET_HANDLE_FROM_STRING(g_hNameTemplateListDict, eaRulesGender1[k]->pcName, ntlr->hNameTemplateList);
						eaPush(&(NOCONST(NameTemplateListRef)**)eaSpeciesList[j]->eaNameTemplateLists, ntlr);
					}
					break;
				case 1:
					for (k = 0; k < eaSize(&eaRulesGender2); ++k)
					{
						NOCONST(NameTemplateListRef) *ntlr = StructCreateNoConst(parse_NameTemplateListRef);
						SET_HANDLE_FROM_STRING(g_hNameTemplateListDict, eaRulesGender2[k]->pcName, ntlr->hNameTemplateList);
						eaPush(&(NOCONST(NameTemplateListRef)**)eaSpeciesList[j]->eaNameTemplateLists, ntlr);
					}
					break;
				case 2:
					for (k = 0; k < eaSize(&eaRulesGender3); ++k)
					{
						NOCONST(NameTemplateListRef) *ntlr = StructCreateNoConst(parse_NameTemplateListRef);
						SET_HANDLE_FROM_STRING(g_hNameTemplateListDict, eaRulesGender3[k]->pcName, ntlr->hNameTemplateList);
						eaPush(&(NOCONST(NameTemplateListRef)**)eaSpeciesList[j]->eaNameTemplateLists, ntlr);
					}
					break;
				case 3:
					for (k = 0; k < eaSize(&eaRulesGender4); ++k)
					{
						NOCONST(NameTemplateListRef) *ntlr = StructCreateNoConst(parse_NameTemplateListRef);
						SET_HANDLE_FROM_STRING(g_hNameTemplateListDict, eaRulesGender4[k]->pcName, ntlr->hNameTemplateList);
						eaPush(&(NOCONST(NameTemplateListRef)**)eaSpeciesList[j]->eaNameTemplateLists, ntlr);
					}
					break;
				}
				resAddRequestLockResource(&tempList, g_hSpeciesDict, eaSpeciesList[j]->pcName, eaSpeciesList[j]);
				resAddRequestSaveResource(&tempList, g_hSpeciesDict, eaSpeciesList[j]->pcName, eaSpeciesList[j]);
			}
			resRequestResourceActions(&tempList);

			//Save CritterGroup
			pCritterGroup = speciesgen_GenerateCritterGroup(eaSpeciesList[0]->pcSpeciesName);
			resSetDictionaryEditMode(g_hCritterGroupDict, true);
			pCritterGroup->pchScope = (char*)allocAddString(text);
			resAddRequestLockResource(&tempList, g_hCritterGroupDict, pCritterGroup->pchName, pCritterGroup);
			resAddRequestSaveResource(&tempList, g_hCritterGroupDict, pCritterGroup->pchName, pCritterGroup);
			resRequestResourceActions(&tempList);

			//Save Critters
			speciesgen_GenerateCritters(pSpeciesGenData, pCritterGroup, eaSpeciesList, &eaCritterDefList);
			resSetDictionaryEditMode(g_hCritterDefDict, true);
			for (j = 0; j < eaSize(&eaCritterDefList); ++j)
			{
				resAddRequestLockResource(&tempList, g_hCritterDefDict, eaCritterDefList[j]->pchName, eaCritterDefList[j]);
				resAddRequestSaveResource(&tempList, g_hCritterDefDict, eaCritterDefList[j]->pchName, eaCritterDefList[j]);
			}
			resRequestResourceActions(&tempList);
		}

		//Clean up
		eaPush(&eaCritterGroups, pCritterGroup); pCritterGroup = NULL;
		eaDestroyStruct(&eaCritterDefList, parse_CritterDef);
		eaDestroyStruct(&eaSpeciesList, parse_SpeciesDef);
		eaDestroyStruct(&eaVowels, parse_PhonemeSet);
		eaDestroyStruct(&eaCons, parse_PhonemeSet);
		eaDestroyStruct(&eaRulesGender1, parse_NameTemplateListNoRef);
		eaDestroyStruct(&eaRulesGender2, parse_NameTemplateListNoRef);
		eaDestroyStruct(&eaRulesGender3, parse_NameTemplateListNoRef);
		eaDestroyStruct(&eaRulesGender4, parse_NameTemplateListNoRef);
		eaDestroyStruct(&eaTempStore, parse_NameTemplateList);
	}

	//Generate Choice Table
	if (eaSize(&eaCritterGroups))
	{
		ResourceActionList tempList = {0};
		ChoiceTable *pChoiceTable = StructCreate(parse_ChoiceTable);
		ChoiceTableValueDef *ctvd;

		*text = '\0';
		strcat(text, "SpeciesGen_");
		strcat(text, pSpeciesGenData->pcName);
		pChoiceTable->pchName = allocAddString(text);

		*text = '\0';
		strcat(text, "speciesGen/");
		strcat(text, pSpeciesGenData->pcName);
		pChoiceTable->pchScope = allocAddString(text);

		ctvd = StructCreate(parse_ChoiceTableValueDef);
		ctvd->pchName = allocAddString("Antagonist1");
		ctvd->eType = WVAR_CRITTER_GROUP;
		eaPush(&pChoiceTable->eaDefs, ctvd);

		for (j = 0; j < eaSize(&eaCritterGroups); ++j)
		{
			CritterGroup *cg = eaCritterGroups[j];
			ChoiceEntry *ce = StructCreate(parse_ChoiceEntry);
			ChoiceValue *cv = StructCreate(parse_ChoiceValue);

			cv->value.eType = WVAR_CRITTER_GROUP;
			SET_HANDLE_FROM_STRING("CritterGroup", cg->pchName, cv->value.hCritterGroup);

			eaPush(&ce->eaValues, cv);
			eaPush(&pChoiceTable->eaEntry, ce);
		}

		resSetDictionaryEditMode(g_hChoiceTableDict, true);
		resSetDictionaryEditMode( gMessageDict, true );
		resAddRequestLockResource(&tempList, g_hChoiceTableDict, pChoiceTable->pchName, pChoiceTable);
		resAddRequestSaveResource(&tempList, g_hChoiceTableDict, pChoiceTable->pchName, pChoiceTable);
		resRequestResourceActions(&tempList);

		eaDestroyStruct(&eaCritterGroups, parse_CritterGroup);
	}
}

#include "AutoGen/speciesGen_c_ast.c"