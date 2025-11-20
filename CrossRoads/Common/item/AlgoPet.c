/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "AlgoPet.h"

#include "entCritter.h"
#include "error.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "rand.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "structDefines.h"
#include "textparser.h"
#include "CostumeCommon.h"
#include "CostumeCommonRandom.h"
#include "CostumeCommonTailor.h"
#include "PowerTree.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "TextFilter.h"

#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/AlgoPet_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"

DictionaryHandle g_hAlgoPetDict;
DefineContext *g_pAlgoCategory = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static ExprContext *s_pAlgoContext = NULL;
static const char *s_pchAlgoPet = NULL;

static int s_hAlgoPet = 0;
static AlgoPet *s_pCurAlgo = NULL;

ExprContext *algoPetGetContext(void)
{
	return s_pAlgoContext;
}

static F32 AlgoPet_Eval(AlgoPet *pAlgoPet, AlgoPetDef *pAlgoPetDef, Expression *pExpr)
{
	MultiVal mVal = {0};

	s_pCurAlgo = pAlgoPet;

	exprContextSetPointerVarPooledCached(s_pAlgoContext,s_pchAlgoPet,pAlgoPetDef,parse_AlgoPetDef,true,false,&s_hAlgoPet);

	exprEvaluate(pExpr,s_pAlgoContext,&mVal);

	return MultiValGetFloat(&mVal,NULL);
}

// If iGoalCount is greater than 0, then return as soon as that number of powers is found
static int algoPetDef_CountPowersWithAlgoCategory(AlgoPet *pAlgoPet, AlgoPetDef *pDef, AlgoCategory eCatName, int iGoalCount)
{
	int iReturn = 0;

	if(pDef)
	{
		int i;

		for(i=0;i<eaSize(&pDef->ppPowers);i++)
		{
			if(ea32Find(&pDef->ppPowers[i]->puiCategory,eCatName) != -1)
			{
				int j;

				for(j=0;j<eaSize(&pAlgoPet->ppEscrowNodes);j++)
				{
					if(GET_REF(pAlgoPet->ppEscrowNodes[j]->hNodeDef) == GET_REF(pDef->ppPowers[i]->hPowerNode))
						iReturn++;

					if(iGoalCount > 0 && iReturn >= iGoalCount)
						return iReturn;
				}
			}
		}
	}

	return iReturn;
}

static F32 *pfFinalWeights = NULL;
static int algoPetDef_PickPower(AlgoPetPowerDef **ppPowers, U32 *pSeed)
{
	F32 fWeightTotal = 0.0f;
	F32 fRandNumber;
	int i;

	for(i=0;i<ea32Size(&pfFinalWeights);i++)
	{
		fWeightTotal += pfFinalWeights[i];
	}

	if(pSeed)
		fRandNumber = randomF32Seeded(pSeed,RandType_LCG) * fWeightTotal;
	else
		fRandNumber = randomF32() * fWeightTotal;
	i = 0;

	while(i < ea32Size(&pfFinalWeights) && fRandNumber > pfFinalWeights[i])
	{
		fRandNumber -= pfFinalWeights[i];
		i++;
	}

	if(i < eaSize(&ppPowers))
		return i;

	return -1;
}

static int algoPetDef_FindValidPowers(AlgoPet *pAlgoPet, AlgoPetDef *pDef,int iRarity, int iChoice, U32 **uiSharedCategories, AlgoPetPowerDef ***pppPowersOut)
{
	int i;
	int iCount=0;
	AlgoPetPowerChoice *pChoice = pDef->ppPowerQuality[iRarity]->ppChoices[iChoice];

	eafClear(&pfFinalWeights);

	for(i=0;i<eaSize(&pDef->ppPowers);i++)
	{
		int c;
		F32 fFinalWeight = 0.0f;

		for(c=ea32Size(&pChoice->puiCategory)-1;c>=0;c--)
		{
			if(ea32Find(&pDef->ppPowers[i]->puiCategory,pChoice->puiCategory[c]) == -1)
				break;
		}

		if(c!=-1)
			continue;

		if(ea32Size(uiSharedCategories) > 0)
		{
			for(c=ea32Size(uiSharedCategories)-1;c>=0;c--)
			{
				if(ea32Find(&pDef->ppPowers[i]->puiCategory,(*uiSharedCategories)[c]) != -1)
					break;
			}

			if(c==-1)
				continue;
		}

		if(ea32Size(&pDef->ppPowers[i]->puiExclusiveCategory))
		{
			for(c=ea32Size(&pDef->ppPowers[i]->puiExclusiveCategory)-1;c>=0;c--)
			{
				AlgoCategory eExclusiveCat = (AlgoCategory)pDef->ppPowers[i]->puiExclusiveCategory[c];
				if(algoPetDef_CountPowersWithAlgoCategory(pAlgoPet, pDef, eExclusiveCat, 1))
				{
					break;
				}
			}
			if (c!=-1)
				continue;
		}

		fFinalWeight = pDef->ppPowers[i]->fWeight;

		if(pDef->ppPowers[i]->pExprWeightMulti)
			fFinalWeight *= AlgoPet_Eval(pAlgoPet,pDef,pDef->ppPowers[i]->pExprWeightMulti);
		

		if(fFinalWeight > 0)
		{
			if(pppPowersOut)
			{
				eafPush(&pfFinalWeights,fFinalWeight);
				eaPush(pppPowersOut,pDef->ppPowers[i]);
			}else{
				return 1;
			}
		}
	}

	return pppPowersOut ? eaSize(pppPowersOut) : 0;
}

static bool algoPetDef_ValidateChoice(AlgoPetPowerDef *pPower, AlgoPet *pAlgoPet, AlgoPetDef *pDef,int iRarity, int iChoice, U32 **uiSharedCategories)
{
	NOCONST(AlgoPet) *palgoPetCopy = StructCreateNoConst(parse_AlgoPet);
	NOCONST(PTNodeDefRefCont) *pEscrow = StructCreateNoConst(parse_PTNodeDefRefCont);
	U32 *uiSharedCategoriesCopy = NULL;
	int i,c;

	StructCopyAllDeConst(parse_AlgoPet,pAlgoPet,palgoPetCopy);
	ea32Copy(&uiSharedCategoriesCopy,uiSharedCategories);



	COPY_HANDLE(pEscrow->hNodeDef,pPower->hPowerNode);
	eaPush(&palgoPetCopy->ppEscrowNodes,pEscrow);

	for(c=0;c<ea32Size(&uiSharedCategoriesCopy);c++)
	{
		if(ea32Find(&pPower->puiCategory,uiSharedCategoriesCopy[c]) != -1)
		{
			ea32RemoveFast(&uiSharedCategoriesCopy,c);
			break;
		}
	}

	for(i=iChoice+1;i<eaSize(&pDef->ppPowerQuality[iRarity]->ppChoices);i++)
	{
		if(!algoPetDef_FindValidPowers((AlgoPet*)palgoPetCopy,pDef,iRarity,i,&uiSharedCategoriesCopy,NULL))
		{
			StructDestroyNoConst(parse_AlgoPet,palgoPetCopy);
			ea32Destroy(&uiSharedCategoriesCopy);
			return false;
		}
	}


	StructDestroyNoConst(parse_AlgoPet,palgoPetCopy);
	ea32Destroy(&uiSharedCategoriesCopy);
	return true;
}

static int algoPetDef_FindValidCostume(PetDef *pDef, int iLevel)
{
	int i;
	F32 fRand, fTotalWeight = 0.f;
	CritterDef *pCritterDef = pDef ? GET_REF(pDef->hCritterDef) : NULL;

	if(!pCritterDef)
		return -1;

	for(i=0;i<eaSize(&pCritterDef->ppCostume);i++)
	{
		if(pCritterDef->ppCostume[i]->iMinLevel == -1 || pCritterDef->ppCostume[i]->iMinLevel <= iLevel
			&& pCritterDef->ppCostume[i]->iMaxLevel == -1 || pCritterDef->ppCostume[i]->iMaxLevel >= iLevel)
		{
			fTotalWeight += pCritterDef->ppCostume[i]->fWeight;
		}
	}

	if(fTotalWeight == 0.f)
		return -1;

	fRand = randomPositiveF32() * fTotalWeight;
	i=0;

	while(i < eaSize(&pCritterDef->ppCostume))
	{

		if(pCritterDef->ppCostume[i]->iMinLevel == -1 || pCritterDef->ppCostume[i]->iMinLevel <= iLevel
			&& pCritterDef->ppCostume[i]->iMaxLevel == -1 || pCritterDef->ppCostume[i]->iMaxLevel >= iLevel)
		{
			if(pCritterDef->ppCostume[i]->fWeight >= fRand)
			{
				return i;		
			}
			fRand -= pCritterDef->ppCostume[i]->fWeight;
		}

		i++;
	}

	return -1;
}

const char *algoPetDef_GenerateRandomName(SpeciesDef *pSpecies, const char **ppcSubName, U32 *pSeed)
{
	static char fullname[256];
	char name1[64], name2[64], name3[64];
	const char *tempName, *firstName = NULL, *middleName = NULL, *lastName = NULL;
	int namecount = 0;
	int len;
	U32 restrictGroup;

	if (eaSize(&pSpecies->eaNameTemplateLists))
	{
		do
		{
			firstName = NULL;
			middleName = NULL;
			lastName = NULL;
			restrictGroup = 0xFFFFFFFF;

			if (eaSize(&pSpecies->eaNameTemplateLists) >= 3)
			{
				tempName = namegen_GenerateName(pSpecies->eaNameTemplateLists[2], &restrictGroup, pSeed);
				len = tempName ? (int)strlen(tempName) : 0;
				if (len > 0)
				{
					if ((!tempName) || len < 3 || len > 20 || IsAnyProfane(tempName) || IsAnyRestricted(tempName) || IsDisallowed(tempName)) tempName = NULL;
				}
				*name3 = '\0';
				if (tempName && name3) strcat(name3, tempName);
				if (pSpecies->eNameOrder == kNameOrder_FML) lastName = name3; else middleName = name3;
			}

			if (eaSize(&pSpecies->eaNameTemplateLists) >= 2)
			{
				tempName = namegen_GenerateName(pSpecies->eaNameTemplateLists[1], &restrictGroup, pSeed);
				len = tempName ? (int)strlen(tempName) : 0;
				if (len > 0)
				{
					if ((!tempName) || len > 20 || IsAnyProfane(tempName) || IsAnyRestricted(tempName) || IsDisallowed(tempName)) tempName = NULL;
				}
				*name2 = '\0';
				if (tempName && name2)
				{
					ANALYSIS_ASSUME(tempName != NULL);
					ANALYSIS_ASSUME(name2 != NULL);
					strcat(name2, tempName);
				}
				if (pSpecies->eNameOrder == kNameOrder_FML)
				{
					if (eaSize(&pSpecies->eaNameTemplateLists) == 2) lastName = name2; else middleName = name2;
				}
				else //kNameOrder_LFM
				{
					firstName = name2;
				}
			}

			tempName = namegen_GenerateName(pSpecies->eaNameTemplateLists[0], &restrictGroup, pSeed);
			len = tempName ? (int)strlen(tempName) : 0;
			if (len > 0)
			{
				if ((!tempName) || len < 3 || len > 20 || IsAnyProfane(tempName) || IsAnyRestricted(tempName) || IsDisallowed(tempName)) tempName = NULL;
			}
			*name1 = '\0';
			if (tempName && name1) strcat(name1, tempName);
			if (pSpecies->eNameOrder == kNameOrder_FML)
			{
				if (eaSize(&pSpecies->eaNameTemplateLists) == 1) lastName = name1; else firstName = name1;
			}
			else
			{
				lastName = name1;
			}

			++namecount;

			if (eaSize(&pSpecies->eaNameTemplateLists) == 1)
			{
				if (((!lastName) || firstName || middleName) && namecount < 10) continue;
			}
			if (eaSize(&pSpecies->eaNameTemplateLists) == 2)
			{
				if (((!lastName) || (!firstName) || middleName) && namecount < 10) continue;
			}
			if (eaSize(&pSpecies->eaNameTemplateLists) >= 3)
			{
				if (((!lastName) || (!firstName) || (!middleName)) && namecount < 10) continue;
			}
			*fullname = '\0';
			if (pSpecies->eNameOrder == kNameOrder_FML)
			{
				if (firstName) strcat(fullname, firstName);
				if (middleName)
				{
					if (*fullname) strcat(fullname, " ");
					strcat(fullname, middleName);
				}
				if (lastName)
				{
					if (*fullname) strcat(fullname, " ");
					strcat(fullname, lastName);
				}
			}
			else
			{
				if (lastName) strcat(fullname, lastName);
				if (firstName)
				{
					if (*fullname) strcat(fullname, " ");
					strcat(fullname, firstName);
				}
				if (middleName)
				{
					if (*fullname) strcat(fullname, " ");
					strcat(fullname, middleName);
				}
			}
			if ((!*fullname) || IsAnyProfane(fullname) || IsAnyRestricted(fullname) || IsDisallowed(fullname)) continue;
			break;

		} while (1);

		if (namecount < 10)
		{
			if (pSpecies->eNameOrder == kNameOrder_FML)
			{
				*fullname = '\0';
				strcat(fullname, "FML:");
				if (firstName) strcat(fullname, firstName);
				strcat(fullname, ":");
				if (middleName) strcat(fullname, middleName);
				strcat(fullname, ":");
				if (lastName) strcat(fullname, lastName);
			}
			else //kNameOrder_LFM
			{
				*fullname = '\0';
				strcat(fullname, "LFM:");
				if (lastName) strcat(fullname, lastName);
				strcat(fullname, ":");
				if (firstName) strcat(fullname, firstName);
				strcat(fullname, ":");
				if (middleName) strcat(fullname, middleName);
			}

			if (*fullname && (firstName || middleName)) *ppcSubName = StructAllocString(fullname);
		}
	}

	if (firstName || lastName)
	{
		*fullname = '\0';
		if (pSpecies->pcDefaultNamePrefix)
		{
			strcat(fullname, pSpecies->pcDefaultNamePrefix);
			strcat(fullname, " ");
		}
		strcat(fullname, (firstName ? firstName : lastName));
		return fullname;
	}
	return NULL;
}

NOCONST(PlayerCostume) *algoPetDef_GenerateRandomCostume(AlgoPetDef *pDef, CritterDef *pCritterDef, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, SpeciesDef **ppSpeciesOut, const char *filename, U32 *pSeed)
{
	NOCONST(PlayerCostume) *pCostume = NULL;
	int i,j,k;

	if (!eaSize(&pDef->eaSpecies))
	{
		ErrorFilenamef(filename,"%s Algo PetDef was unable to find any valid species",pDef ? pDef->pchName : "[Unknown]");
		return NULL;
	}

	//Create a random costume
	if (pCritterDef)
	{
		pCostume = StructCreateNoConst(parse_PlayerCostume);

		//
		// This section picks a random species
		//
		if (eaSize(&pDef->eaSpecies))
		{
			int count = 0, genderCount = 0;
			int	iRand;
			SpeciesDef *pSpecies = NULL;

			for (i = eaSize(&pDef->eaSpecies)-1; i >= 0; --i)
			{
				pSpecies = GET_REF(pDef->eaSpecies[i]->hSpecies);
				if (!pSpecies) continue;
				for (j = ea32Size(&pDef->eaExcludeSpeciesClassType)-1; j >= 0; --j)
				{
					if (pSpecies->eType == (U32)pDef->eaExcludeSpeciesClassType[j]) break;
				}
				if (j >= 0) continue;
				if (pAllegiance)
				{
					for (k = eaSize(&pAllegiance->eaPetSpecies)-1; k >= 0; --k)
					{
						if (GET_REF(pAllegiance->eaPetSpecies[k]->hSpecies) == pSpecies) break;
					}
					if (k < 0)
					{
						if (pSubAllegiance)
						{
							for (k = eaSize(&pSubAllegiance->eaPetSpecies)-1; k >= 0; --k)
							{
								if (GET_REF(pSubAllegiance->eaPetSpecies[k]->hSpecies) == pSpecies) break;
							}
						}
						if (k < 0) continue;
					}
				}
				++count;
			}
			
			pSpecies=NULL;

			if (count)
			{
				if (count == 1)
				{
					iRand = 0;
				}
				else
				{
					if(pSeed)
						iRand = randomIntRangeSeeded(pSeed,RandType_LCG,0,count-1);
					else
						iRand = randomIntRange(0, count-1);
				}
				for (i = eaSize(&pDef->eaSpecies)-1; i >= 0; --i)
				{
					pSpecies = GET_REF(pDef->eaSpecies[i]->hSpecies);
					if (!pSpecies) continue;
					for (j = ea32Size(&pDef->eaExcludeSpeciesClassType)-1; j >= 0; --j)
					{
						if (pSpecies->eType == (U32)pDef->eaExcludeSpeciesClassType[j]) break;
					}
					if (j >= 0) continue;
					if (pAllegiance)
					{
						for (k = eaSize(&pAllegiance->eaPetSpecies)-1; k >= 0; --k)
						{
							if (GET_REF(pAllegiance->eaPetSpecies[k]->hSpecies) == pSpecies) break;
						}
						if (k < 0)
						{
							if (pSubAllegiance)
							{
								for (k = eaSize(&pSubAllegiance->eaPetSpecies)-1; k >= 0; --k)
								{
									if (GET_REF(pSubAllegiance->eaPetSpecies[k]->hSpecies) == pSpecies) break;
								}
							}
							if (k < 0) continue;
						}
					}
					if (iRand-- <= 0)
					{
						break;
					}
					pSpecies = NULL;
				}
			}

			if (pSpecies)
			{
				//
				// This section generates a random costume
				//
				pCostume->eGender = pSpecies->eGender;
				if(ppSpeciesOut)
					*ppSpeciesOut = pSpecies;
				COPY_HANDLE(pCostume->hSkeleton, pSpecies->hSkeleton);
				pCostume->eCostumeType = kPCCostumeType_Player;

				costumeRandom_FillRandom(pCostume, pSpecies, NULL, NULL, NULL, NULL, NULL, true, true, false, false, true, true, true);
				costumeTailor_StripUnnecessary(pCostume);

				//
				// This section picks a random uniform and applies it over the randomly generated costume
				//
				if (eaSize(&pDef->eaUniforms))
				{
					PlayerCostume *pc = NULL;

					//Find uniforms that have same species; If non are found choose uniforms of same gender; If non are found choose from all uniforms
					count = 0;
					for (i = eaSize(&pDef->eaUniforms)-1; i >= 0; --i)
					{
						pc = GET_REF(pDef->eaUniforms[i]->hPlayerCostume);
						if ((!pc) || (!GET_REF(pc->hSpecies))) continue;
						if (GET_REF(pc->hSpecies) == pSpecies)
						{
							++count;
						}
						if (pSpecies->eGender == GET_REF(pc->hSpecies)->eGender)
						{
							++genderCount;
						}
					}
					if (!count)
					{
						if (genderCount)
						{
							if (genderCount == 1)
							{
								iRand = 0;
							}
							else
							{
								if(pSeed)
									iRand = randomIntRangeSeeded(pSeed,RandType_LCG,0,genderCount-1);
								else
									iRand = randomIntRange(0, genderCount-1);
							}
							for (i = eaSize(&pDef->eaUniforms)-1; i >= 0; --i)
							{
								pc = GET_REF(pDef->eaUniforms[i]->hPlayerCostume);
								if ((!pc) || (!GET_REF(pc->hSpecies))) continue;
								if (pSpecies->eGender == GET_REF(pc->hSpecies)->eGender)
								{
									if (iRand-- <= 0)
									{
										break;
									}
								}
								pc = NULL;
							}
							if (pc)
							{
								SET_HANDLE_FROM_REFERENT("Species",pSpecies,pCostume->hSpecies);
								costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pCostume, NULL, pc, NULL, "Uniforms", NULL, true, false, true, false);
							}
						}
						else
						{
							if(pSeed)
								iRand = randomIntRangeSeeded(pSeed,RandType_LCG,0,eaSize(&pDef->eaUniforms)-1);
							else
								iRand = randomIntRange(0, eaSize(&pDef->eaUniforms)-1);
							if (GET_REF(pDef->eaUniforms[iRand]->hPlayerCostume))
							{
								SET_HANDLE_FROM_REFERENT("Species",pSpecies,pCostume->hSpecies);
								costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pCostume, NULL, GET_REF(pDef->eaUniforms[iRand]->hPlayerCostume), NULL, "Uniforms", NULL, true, false, true, false);
							}
						}
					}
					else
					{
						if (count == 1)
						{
							iRand = 0;
						}
						else
						{
							if(pSeed)
								iRand = randomIntRangeSeeded(pSeed,RandType_LCG,0,count-1);
							else
								iRand = randomIntRange(0, count-1);
						}
						for (i = eaSize(&pDef->eaUniforms)-1; i >= 0; --i)
						{
							pc = GET_REF(pDef->eaUniforms[i]->hPlayerCostume);
							if (!pc) continue;
							if (GET_REF(pc->hSpecies) == pSpecies)
							{
								if (iRand-- <= 0)
								{
									break;
								}
							}
							pc = NULL;
						}
						if (pc)
						{
							SET_HANDLE_FROM_REFERENT("Species",pSpecies,pCostume->hSpecies);
							costumeTailor_ApplyCostumeOverlay((PlayerCostume*)pCostume, NULL, pc, NULL, "Uniforms", NULL, true, false, true, false);
						}
					}
				}
			}
		}
	}

	return pCostume;
}

static void algoPetDef_PickRandomVoice(SpeciesDef *pSpeciesDef, NOCONST(PlayerCostume) *pCostume)
{
	int unlockedCount, iRand, i;

	if (pSpeciesDef && (pSpeciesDef->bAllowAllVoices == false || eaSize(&pSpeciesDef->eaAllowedVoices)))
	{
		VoiceRef ***peaVoices = &(VoiceRef**)pSpeciesDef->eaAllowedVoices;

		if (eaSize(peaVoices))
		{
			unlockedCount = 0;
			for (i = eaSize(peaVoices)-1; i >= 0; --i)
			{
				PCVoice *pVoice = GET_REF((*peaVoices)[i]->hVoice);
				if (!pVoice) continue;
				if (pVoice->pcUnlockCode && *pVoice->pcUnlockCode)
				{
					//Assume it is not unlocked
				}
				else
				{
					++unlockedCount;
				}
			}
			if (unlockedCount)
			{
				iRand = randomIntRange(0, unlockedCount-1);
				for (i = eaSize(peaVoices)-1; i >= 0; --i)
				{
					PCVoice *pVoice = GET_REF((*peaVoices)[i]->hVoice);
					if (!pVoice) continue;
					if (pVoice->pcUnlockCode && *pVoice->pcUnlockCode)
					{
						//Assume it is not unlocked
					}
					else
					{
						if (iRand-- <= 0)
						{
							COPY_HANDLE(pCostume->hVoice, (*peaVoices)[i]->hVoice);
							break;
						}
					}
				}
			}

		}
	}
	else
	{
		DictionaryEArrayStruct *deas = resDictGetEArrayStruct("CostumeVoice");
		PCVoice ***peaVoices = deas ? (PCVoice***)&deas->ppReferents : NULL;

		if (peaVoices && eaSize(peaVoices))
		{
			unlockedCount = 0;
			for (i = eaSize(peaVoices)-1; i >= 0; --i)
			{
				if ((*peaVoices)[i]->pcUnlockCode && *(*peaVoices)[i]->pcUnlockCode)
				{
					//Assume it is not unlocked
				}
				else
				{
					++unlockedCount;
				}
			}
			if (unlockedCount)
			{
				iRand = randomIntRange(0, eaSize(peaVoices)-1);
				for (i = eaSize(peaVoices)-1; i >= 0; --i)
				{
					if ((*peaVoices)[i]->pcUnlockCode && *(*peaVoices)[i]->pcUnlockCode)
					{
						//Assume it is not unlocked
					}
					else
					{
						if (iRand-- <= 0)
						{
							SET_HANDLE_FROM_REFERENT("CostumeVoice", (*peaVoices)[i], pCostume->hVoice);
							break;
						}
					}
				}
			}

		}
	}
}

NOCONST(AlgoPet) *algoPetDef_CreateNew(AlgoPetDef *pDef, PetDef *pPetDef, U32 uiRarity, int iLevel, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, U32 *pSeed)
{
	NOCONST(AlgoPet) *pReturn;
	int i, j;
	CritterDef *pCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	const char *pcTemp;

	if(!pDef && !pPetDef)
		return NULL;

	pReturn = StructCreateNoConst(parse_AlgoPet);

	i = algoPetDef_FindValidCostume(pPetDef,iLevel);

	if(i==-1)
	{
		ErrorFilenamef(pDef ? pDef->pchFileName : pPetDef->pchFilename,"%s Algo PetDef was unable to find any valid costumes : %s PetDef",pDef ? pDef->pchName : "[Unknown]",pPetDef?pPetDef->pchPetName:"");	
	}

	pReturn->iCostume = i;

	if (pDef)
	{
		SpeciesDef *pSpeciesDef = NULL;

		pReturn->pCostume = algoPetDef_GenerateRandomCostume(pDef,pCritterDef,pAllegiance,pSubAllegiance,&pSpeciesDef,pDef ? pDef->pchFileName : pPetDef->pchFilename,pSeed);

		pcTemp = algoPetDef_GenerateRandomName(pSpeciesDef, &pReturn->pchPetSubName, pSeed);
		if (pcTemp) pReturn->pchPetName = StructAllocString(pcTemp);

		if (pReturn->pCostume)
		{
			char text[1024];
			static unsigned int increment = 0;
			sprintf(text, "Pet%d", increment++);
			pReturn->pCostume->pcName = allocAddString(text);

			//Pick random Voice
			algoPetDef_PickRandomVoice(pSpeciesDef, pReturn->pCostume);
		}

		if(pSpeciesDef)
			SET_HANDLE_FROM_REFERENT("Species", pSpeciesDef, pReturn->hSpecies);
	}

	if (!GET_REF(pReturn->hSpecies))
	{
		if (pCritterDef && pReturn->iCostume >= 0 && pReturn->iCostume < eaSize(&pCritterDef->ppCostume))
		{
			PlayerCostume *pc = GET_REF(pCritterDef->ppCostume[pReturn->iCostume]->hCostumeRef);
			if (pc)
			{
				COPY_HANDLE(pReturn->hSpecies, pc->hSpecies);
			}
		}
	}

	if(!pDef)
	{
		//There is no AlgoPetDef, just create the info from the pet def
		for(i=0;i<eaSize(&pPetDef->ppEscrowPowers);i++)
		{
			NOCONST(PTNodeDefRefCont) *pEscrow = StructCreateNoConst(parse_PTNodeDefRefCont);

			COPY_HANDLE(pEscrow->hNodeDef,pPetDef->ppEscrowPowers[i]->hNodeDef);
			eaPush(&pReturn->ppEscrowNodes,pEscrow);
		}

		return pReturn;
	}
	
	SET_HANDLE_FROM_STRING(g_hAlgoPetDict,pDef->pchName,pReturn->hAlgoPet);

	//Fill in valid powers
	for(i=0;i<eaSize(&pDef->ppPowerQuality);i++)
	{
		AlgoPetPowerDef **ppPowers = NULL;
		int *iSharedCategories = NULL;

		if(pDef->ppPowerQuality[i]->uiRarity != (ItemQuality)uiRarity)
			continue;

		ea32Copy(&iSharedCategories,&pDef->ppPowerQuality[i]->puiSharedCategories);

		//Find all valid powers for this choice
		for(j=0;j<eaSize(&pDef->ppPowerQuality[i]->ppChoices);j++)
		{
			if(!algoPetDef_FindValidPowers((AlgoPet*)pReturn,pDef,i,j, &iSharedCategories, &ppPowers))
			{
				ErrorFilenamef(pDef->pchFileName,"%s Algo Pet Def was unable to find any powers for choice %d",pDef->pchName,j);
			}
			else
			{
				int iPower = -1;
				bool bPowerValid = false;

				while(bPowerValid == false && eaSize(&ppPowers) > 0)
				{
					iPower = algoPetDef_PickPower(ppPowers, pSeed);

					if(algoPetDef_ValidateChoice(ppPowers[iPower],(AlgoPet*)pReturn,pDef,i,j,&iSharedCategories))
					{
						bPowerValid = true;
						break;
					}

					eaRemoveFast(&ppPowers,iPower);
				}

				if(bPowerValid)
				{
					int c;

					NOCONST(PTNodeDefRefCont) *pEscrow = StructCreateNoConst(parse_PTNodeDefRefCont);

					COPY_HANDLE(pEscrow->hNodeDef,ppPowers[iPower]->hPowerNode);
					eaPush(&pReturn->ppEscrowNodes,pEscrow);

					for(c=0;c<ea32Size(&iSharedCategories);c++)
					{
						if(ea32Find(&ppPowers[iPower]->puiCategory,iSharedCategories[c]) != -1)
						{
							ea32RemoveFast(&iSharedCategories,c);
							break;
						}
					}
				}
				else
				{
					ErrorDetailsf("Choice %d, Rarity %s", j, StaticDefineIntRevLookup(ItemQualityEnum, uiRarity));
					ErrorFilenamef(pDef->pchFileName,"Algo Pet Def had no valid power for choice");
				}
			} 

			eaDestroy(&ppPowers);
		}

		ea32Destroy(&iSharedCategories);
		break;
	}

	if (pPetDef && GET_REF(pPetDef->hPetDiag))
	{
		SavedPet_PetDiag_FixupEscrowNodes(GET_REF(pPetDef->hPetDiag), &pReturn->ppEscrowNodes);
	}
	return pReturn;
}

//********************AlgoPet Expressions************************//

//Find all the currently chosen powers with the category requested
AUTO_EXPR_FUNC(AlgoPetFuncs);
int PowersWithAlgoCategory(ACMD_EXPR_ENUM(AlgoCategory) const char *pchAlgoCat)
{
	AlgoPetDef *pDef = GET_REF(s_pCurAlgo->hAlgoPet);
	AlgoCategory eCatName = StaticDefineIntGetInt(AlgoCategoryEnum,pchAlgoCat);

	return algoPetDef_CountPowersWithAlgoCategory(s_pCurAlgo, pDef, eCatName, 0);
}

AUTO_EXPR_FUNC(AlgoPetFuncs);
bool AlgoPetIsSpecies(ACMD_EXPR_RES_DICT(Species) const char *pchSpecies)
{
	return stricmp(REF_STRING_FROM_HANDLE(s_pCurAlgo->hSpecies),pchSpecies) == 0;
}

//****************************************************************//

static void AlgoPetDef_Generate(AlgoPetDef *pDef)
{
	int i;

	for(i=0;i<eaSize(&pDef->ppPowers);i++)
	{
		if(pDef->ppPowers[i]->pExprWeightMulti)
			exprGenerate(pDef->ppPowers[i]->pExprWeightMulti,s_pAlgoContext);
	}
}

bool algoPetDef_Validate(AlgoPetDef *pDef)
{
	int i;

	for(i=0;i<eaSize(&pDef->ppPowers);i++)
	{
		if(GET_REF(pDef->ppPowers[i]->hPowerNode) == NULL)
		{
			ErrorFilenamef(pDef->pchFileName,"%s Algo Pet Def references unknown power node %s[%d]",pDef->pchName,REF_STRING_FROM_HANDLE(pDef->ppPowers[i]->hPowerNode),i);
			return false;
		}

		if(pDef->ppPowers[i]->fWeight == 0.0f)
		{
			ErrorFilenamef(pDef->pchFileName,"%s Algo Pet Def has a power with a weight of 0! %s[%d]",pDef->pchName,REF_STRING_FROM_HANDLE(pDef->ppPowers[i]->hPowerNode),i);
			return false;
		}
	}

	return true;
}

static int AlgoPetDefValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	//TODO(MM) validate:
	AlgoPetDef *pData = pResource;

	switch(eType)
	{	
	case RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pData->pchFileName, "defs/items/algopets", pData->pchScope, pData->pchName, "algopet");
		return VALIDATE_HANDLED;
	case RESVALIDATE_POST_TEXT_READING:		
		AlgoPetDef_Generate(pData);
		return VALIDATE_HANDLED;;
	case RESVALIDATE_POST_BINNING:
		algoPetDef_Validate(pData);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void AlgoPetAutoRun(void)
{
	s_pchAlgoPet = allocAddStaticString("AlgoDef");
}

void AlgoPetExprInit(void)
{
	ExprFuncTable* stTable;

	s_pAlgoContext = exprContextCreate();

	stTable = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(stTable, "util");
	exprContextAddFuncsToTableByTag(stTable, "AlgoPetFuncs");
	exprContextSetFuncTable(s_pAlgoContext, stTable);

	exprContextSetPointerVarPooledCached(s_pAlgoContext,s_pchAlgoPet,NULL,parse_AlgoPetDef,true,false,&s_hAlgoPet);
}

AUTO_RUN;
void RegisterAlgoPetDict(void)
{
	// Set up reference dictionaries
	g_hAlgoPetDict = RefSystem_RegisterSelfDefiningDictionary("AlgoPetDef", false, parse_AlgoPetDef, true, true, NULL);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hAlgoPetDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hAlgoPetDict, NULL, ".Scope", NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hAlgoPetDict, 8, false, resClientRequestSendReferentCommand );
	}

	resDictManageValidation(g_hAlgoPetDict, AlgoPetDefValidateCB);
}

void AlgoPetCategoryLoad(void)
{
	AlgoCategoryNames catNames = {0};
	S32 i;

	g_pAlgoCategory = DefineCreate();

	loadstart_printf("Loading Algo Categories... ");

	ParserLoadFiles("defs/items/algopets", "AlgoPet_Category.def", "AlgoPet_Category.bin", PARSER_OPTIONALFLAG, parse_AlgoCategoryNames, &catNames);

	// I use i+1 for the default "uncategorized" index, which is always present
	for (i = 0; i < eaSize(&catNames.pchNames); i++)
		DefineAddInt(g_pAlgoCategory, catNames.pchNames[i], i+1);

	StructDeInit(parse_AlgoCategoryNames, &catNames);

	loadend_printf(" done (%d Algo Categories).", i);
}

// Reload PowerDefs top level callback
static void AlgoPetsReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading AlgoPets...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionaryWithFlags(pchRelPath,g_hAlgoPetDict,PARSER_OPTIONALFLAG);

	loadend_printf(" done (%d AlgoPets)", RefSystem_GetDictionaryNumberOfReferents(g_hAlgoPetDict));
}

AUTO_STARTUP(AlgoPet) ASTRT_DEPS(Powers, Items, ItemPowers, ItemTags, AS_Messages, PowerTrees);
void AlgoPetDefLoad(void)
{
	AlgoPetExprInit();
	AlgoPetCategoryLoad();

	if(IsClient())
	{
		//Do nothing
	}
	else
	{
		loadstart_printf("Loading AlgoPet Data files...");

		resLoadResourcesFromDisk(g_hAlgoPetDict, "defs/items/algopets", ".AlgoPet", "AlgoPets.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

		// Reload callbacks
		if(isDevelopmentMode())
		{
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/items/algopets*.AlgoPet", AlgoPetsReload);
		}

		loadend_printf(" done (%d AlgoPets).", RefSystem_GetDictionaryNumberOfReferents(g_hAlgoPetDict));
	}

	
	
}


#include "AutoGen/AlgoPet_h_ast.c"
