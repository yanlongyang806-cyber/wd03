/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "CharacterAttribs.h"
#include "cmdClientReport.h"
#include "contact_common.h"
#include "GameEvent.h"
#include "entCritter.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Expression.h"
#include "GraphicsLib.h"
#include "gslDataFixupUtils.h"
#include "gslMission.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "gslMission_transact.h"
#include "PowerAnimFX.h"
#include "PowerTree.h"
#include "rewardCommon.h"
#include "species_common.h"
#include "StringCache.h"
#include "SuperCritterPet.h"
#include "TextParserInheritance.h"
#include "WorldGrid.h"
#include "gimmeDLLWrapper.h"
#include "queue_common.h"
#include "storeCommon.h"
#include "CombatPowerStateSwitching.h"

#include "Entity.h"
#include "fileutil.h"
#include "StateMachine.h"
#include "Message.h"
#include "contact_common.h"
#include "cutscene_common.h"
#include "encounter_common.h"
#include "interaction_common.h"
#include "mission_common.h"
#include "Sound_Common.h"
#include "UGCCommon.h"

#include "ObjectLibrary.h"
#include "../StaticWorld/WorldGridPrivate.h"
#include "../StaticWorld/WorldGridLoadPrivate.h"

#include "cmdClientReport_h_ast.h"
#include "AutoGen/oldencounter_common_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/powers_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ---------------------- Code to dump an asset report on costumes ---------------------------

typedef struct ReportSkelDefInfo {
	PCSkeletonDef *pSkel;

	PCRegion **eaRegions;
	PCCategory **eaCategories;
	PCBoneDef **eaBones;
	PCGeometryDef **eaGeometries;
	PCMaterialDef **eaMaterials;
	PCTextureDef **eaTextures;
	PlayerCostume **eaCostumes;
} ReportSkelDefInfo;

static void SkeletonDefAssetScan(ReportSkelDefInfo ***peaInfos)
{
	DictionaryEArrayStruct *pSkeletons = resDictGetEArrayStruct("CostumeSkeleton");
	DictionaryEArrayStruct *pCostumes = resDictGetEArrayStruct("PlayerCostume");
	DictionaryEArrayStruct *pGeometries = resDictGetEArrayStruct("CostumeGeometry");
	DictionaryEArrayStruct *pGeoAdds = resDictGetEArrayStruct("CostumeGeometryAdd");
	DictionaryEArrayStruct *pMatAdds = resDictGetEArrayStruct("CostumeMaterialAdd");
	int i, j, k, m, n, p, q;

	// Collect infos
	for(i=0; i<eaSize(&pSkeletons->ppReferents); ++i) {
		PCSkeletonDef *pDef;
		ReportSkelDefInfo *pInfo;
			
		pDef = pSkeletons->ppReferents[i];

		pInfo = calloc(1,sizeof(ReportSkelDefInfo));
		pInfo->pSkel = pDef;
		eaPush(peaInfos, pInfo);

		// Collect regions and categories
		for(j=eaSize(&pDef->eaRegions)-1; j>=0; --j) {
			PCRegion *pRegion = GET_REF(pDef->eaRegions[j]->hRegion);
			if (pRegion) {
				eaPushUnique(&pInfo->eaRegions, pRegion);
				for (k=eaSize(&pRegion->eaCategories)-1; k>=0; --k) {
					PCCategory *pCat = GET_REF(pRegion->eaCategories[k]->hCategory);
					if (pCat) {
						eaPushUnique(&pInfo->eaCategories, pCat);
					}
				}
			}	
		}

		// Collect Bones
		for(j=eaSize(&pDef->eaRequiredBoneDefs)-1; j>=0; --j) {
			PCBoneDef *pBone = GET_REF(pDef->eaRequiredBoneDefs[j]->hBone);
			if (pBone) {
				eaPushUnique(&pInfo->eaBones, pBone);
			}
		}
		for(j=eaSize(&pDef->eaOptionalBoneDefs)-1; j>=0; --j) {
			PCBoneDef *pBone = GET_REF(pDef->eaOptionalBoneDefs[j]->hBone);
			if (pBone) {
				eaPushUnique(&pInfo->eaBones, pBone);
			}
		}

		// Collect Geometries, Materials and Textures
		for(j=eaSize(&pGeometries->ppReferents)-1; j>=0; --j) {
			PCGeometryDef *pGeo = pGeometries->ppReferents[j];
			for(k=eaSize(&pInfo->eaBones)-1; k>=0; --k) {
				if (GET_REF(pGeo->hBone) == pInfo->eaBones[k]) {
					eaPush(&pInfo->eaGeometries, pGeo);

					for(m=eaSize(&pGeo->eaAllowedMaterialDefs)-1; m>=0; --m) {
						PCMaterialDef *pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pGeo->eaAllowedMaterialDefs[m]);
						if (pMat) {
							eaPushUnique(&pInfo->eaMaterials, pMat);

							for(q=eaSize(&pMat->eaAllowedTextureDefs)-1; q>=0; --q) {
								PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMat->eaAllowedTextureDefs[q]);
								if (pTex) {
									eaPushUnique(&pInfo->eaTextures, pTex);
								}
							}
							for(p=eaSize(&pMatAdds->ppReferents)-1; p>=0; --p) {
								PCMaterialAdd *pMatAdd = pMatAdds->ppReferents[p];
								if ((pMatAdd->pcMatName && stricmp(pMatAdd->pcMatName, pMat->pcName) == 0) ||
									(!pMatAdd->pcMatName && stricmp(pMatAdd->pcName, pMat->pcName) == 0)) {
									for(q=eaSize(&pMatAdd->eaAllowedTextureDefs)-1; q>=0; --q) {
										PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatAdd->eaAllowedTextureDefs[q]);
										if (pTex) {
											eaPushUnique(&pInfo->eaTextures, pTex);
										}
									}
								}
							}
						}
					}

					for(m=eaSize(&pGeoAdds->ppReferents)-1; m>=0; --m) {
						PCGeometryAdd *pGeoAdd = pGeoAdds->ppReferents[m];
						if ((pGeoAdd->pcGeoName && stricmp(pGeoAdd->pcGeoName, pGeo->pcName) == 0) ||
							(!pGeoAdd->pcGeoName && stricmp(pGeoAdd->pcName, pGeo->pcName) == 0)) {
							for(n=eaSize(&pGeoAdd->eaAllowedMaterialDefs)-1; n>=0; --n) {
								PCMaterialDef *pMat = RefSystem_ReferentFromString(g_hCostumeMaterialDict, pGeoAdd->eaAllowedMaterialDefs[n]);
								if (pMat) {
									eaPushUnique(&pInfo->eaMaterials, pMat);

									for(q=eaSize(&pMat->eaAllowedTextureDefs)-1; q>=0; --q) {
										PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMat->eaAllowedTextureDefs[q]);
										if (pTex) {
											eaPushUnique(&pInfo->eaTextures, pTex);
										}
									}
									for(p=eaSize(&pMatAdds->ppReferents)-1; p>=0; --p) {
										PCMaterialAdd *pMatAdd = pMatAdds->ppReferents[p];
										if ((pMatAdd->pcMatName && stricmp(pMatAdd->pcMatName, pMat->pcName) == 0) ||
											(!pMatAdd->pcMatName && stricmp(pMatAdd->pcName, pMat->pcName) == 0)) {
											for(q=eaSize(&pMatAdd->eaAllowedTextureDefs)-1; q>=0; --q) {
												PCTextureDef *pTex = RefSystem_ReferentFromString(g_hCostumeTextureDict, pMatAdd->eaAllowedTextureDefs[q]);
												if (pTex) {
													eaPushUnique(&pInfo->eaTextures, pTex);
												}
											}
										}
									}
								}
							}
						}
					}
					break;
				}
			}
		}

		// Collect Costumes
		for(j=eaSize(&pCostumes->ppReferents)-1; j>=0; --j) {
			PlayerCostume *pCostume = pCostumes->ppReferents[j];
			if (GET_REF(pCostume->hSkeleton) == pDef) {
				eaPush(&pInfo->eaCostumes, pCostume);
			}
		}

	}
}

typedef struct ReportCostumeInfo {
	PlayerCostume *pCostume;

	CritterDef **eaCritters;
	CritterGroup **eaCritterGroups;
	ItemDef **eaItems;
	PowerDef **eaPowers;
	RewardTable **eaRewards;
	ContactDef **eaContacts;
	SpeciesDef **eaSpecies;
	PCCostumeSet **eaSets;
	SuperCritterPetDef **eaSuperPets;
} ReportCostumeInfo;

static void CostumeAssetScan(ReportCostumeInfo ***peaInfos)
{
	DictionaryEArrayStruct *pCostumes = resDictGetEArrayStruct("PlayerCostume");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pCritterGroups = resDictGetEArrayStruct("CritterGroup");
	DictionaryEArrayStruct *pItems = resDictGetEArrayStruct("ItemDef");
	DictionaryEArrayStruct *pPowers = resDictGetEArrayStruct("PowerDef");
	DictionaryEArrayStruct *pRewards = resDictGetEArrayStruct("RewardTable");
	DictionaryEArrayStruct *pContacts = resDictGetEArrayStruct("ContactDef");
	DictionaryEArrayStruct *pSpecies = resDictGetEArrayStruct("SpeciesDef");
	DictionaryEArrayStruct *pSets = resDictGetEArrayStruct("CostumeSet");
	DictionaryEArrayStruct *pSuperPets = resDictGetEArrayStruct("SuperCritterPetDef");
	ReportCostumeInfo *pInfo;
	int i,j,k;

	// Create Costume data
	for(i=0; i<eaSize(&pCostumes->ppReferents); ++i) {
		pInfo = calloc(1,sizeof(ReportCostumeInfo));
		pInfo->pCostume = pCostumes->ppReferents[i];
		eaPush(peaInfos, pInfo);
	}

	// Tally up sets
	for(i=eaSize(&pSets->ppReferents)-1; i>=0; --i) {
		PCCostumeSet *pSet = pSets->ppReferents[i];

		for(j=eaSize(&pSet->eaPlayerCostumes)-1; j>=0; --j) {
			PlayerCostume *pCostume = GET_REF(pSet->eaPlayerCostumes[j]->hPlayerCostume);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPush(&(*peaInfos)[k]->eaSets, pSet);
						break;
					}
				}
			}
		}
	}

	// Tally up critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];

		for(j=eaSize(&pCritter->ppCostume)-1; j>=0; --j) {
			PlayerCostume *pCostume = GET_REF(pCritter->ppCostume[j]->hCostumeRef);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPush(&(*peaInfos)[k]->eaCritters, pCritter);
						break;
					}
				}
			}
		}
	}

	// Tally up items
	for(i=eaSize(&pItems->ppReferents)-1; i>=0; --i) {
		ItemDef *pItem = pItems->ppReferents[i];

		for(j=eaSize(&pItem->ppCostumes)-1; j>=0; --j) {
			PlayerCostume *pCostume = GET_REF(pItem->ppCostumes[j]->hCostumeRef);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPush(&(*peaInfos)[k]->eaItems, pItem);
						break;
					}
				}
			}
		}
	}

	// Tally up contacts
	for(i=eaSize(&pContacts->ppReferents)-1; i>=0; --i) {
		ContactDef *pContact = pContacts->ppReferents[i];
		PlayerCostume *pCostume;

		pCostume = GET_REF(pContact->costumePrefs.costumeOverride);
		if (pCostume) {
			for(k=eaSize(peaInfos)-1; k>=0; --k) {
				if ((*peaInfos)[k]->pCostume == pCostume) {
					eaPush(&(*peaInfos)[k]->eaContacts, pContact);
					break;
				}
			}
		}

		for(j=eaSize(&pContact->specialDialog)-1; j>=0; --j) {
			SpecialDialogBlock *pBlock = pContact->specialDialog[j];
			pCostume = GET_REF(pBlock->costumePrefs.costumeOverride);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPush(&(*peaInfos)[k]->eaContacts, pContact);
						break;
					}
				}
			}
		}
	}

	// Tally up powers
	for(i=eaSize(&pPowers->ppReferents)-1; i>=0; --i) {
		PowerDef *pPower = pPowers->ppReferents[i];

		for(j=eaSize(&pPower->ppMods)-1; j>=0; --j) {
			AttribModDef *pMod = pPower->ppMods[j];
			if (pMod->pParams && pMod->pParams->eType == kAttribType_SetCostume) {
				PlayerCostume *pCostume = GET_REF(((SetCostumeParams*)pMod->pParams)->hCostume);
				if (pCostume) {
					for(k=eaSize(peaInfos)-1; k>=0; --k) {
						if ((*peaInfos)[k]->pCostume == pCostume) {
							eaPush(&(*peaInfos)[k]->eaPowers, pPower);
							break;
						}
					}
				}
			}
		}
	}

	// Tally Reward Tables
	for(i=eaSize(&pRewards->ppReferents)-1; i>=0; --i) {
		RewardTable *pReward = pRewards->ppReferents[i];

		PlayerCostume *pCostume = GET_REF(pReward->hYoursCostumeRef);
		if (pCostume) {
			for(k=eaSize(peaInfos)-1; k>=0; --k) {
				if ((*peaInfos)[k]->pCostume == pCostume) {
					eaPushUnique(&(*peaInfos)[k]->eaRewards, pReward);
					break;
				}
			}
		}
		pCostume = GET_REF(pReward->hNotYoursCostumeRef);
		if (pCostume) {
			for(k=eaSize(peaInfos)-1; k>=0; --k) {
				if ((*peaInfos)[k]->pCostume == pCostume) {
					eaPushUnique(&(*peaInfos)[k]->eaRewards, pReward);
					break;
				}
			}
		}

		for(j=eaSize(&pReward->ppRewardEntry)-1; j>=0; --j) {
			RewardEntry *pEntry = pReward->ppRewardEntry[j];
			pCostume = GET_REF(pEntry->hCostumeDef);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPushUnique(&(*peaInfos)[k]->eaRewards, pReward);
						break;
					}
				}
			}
		}
	}

	// Tally up species
	for(i=eaSize(&pSpecies->ppReferents)-1; i>=0; --i) {
		SpeciesDef *pDef = pSpecies->ppReferents[i];

		for(j=eaSize(&pDef->eaPresets)-1; j>=0; --j) {
			PlayerCostume *pCostume = GET_REF(pDef->eaPresets[j]->hCostume);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPush(&(*peaInfos)[k]->eaSpecies, pDef);
						break;
					}
				}
			}
		}
	}

	// Tally up super critter pets
	for(i=eaSize(&pSuperPets->ppReferents)-1; i>=0; --i) {
		SuperCritterPetDef *pDef = pSuperPets->ppReferents[i];

		for(j=eaSize(&pDef->ppAltCostumes)-1; j>=0; --j) {
			PlayerCostume *pCostume = GET_REF(pDef->ppAltCostumes[j]->hCostume);
			if (pCostume) {
				for(k=eaSize(peaInfos)-1; k>=0; --k) {
					if ((*peaInfos)[k]->pCostume == pCostume) {
						eaPush(&(*peaInfos)[k]->eaSuperPets, pDef);
						break;
					}
				}
			}
		}
	}

}

typedef struct ReportBoneInfo {
	PCBoneDef *pBone;
	PlayerCostume **eaCostumes;
} ReportBoneInfo;

typedef struct ReportGeoInfo {
	PCGeometryDef *pGeo;
	PlayerCostume **eaCostumes;
} ReportGeoInfo;

typedef struct ReportMatInfo {
	PCMaterialDef *pMat;
	PlayerCostume **eaCostumes;
} ReportMatInfo;

typedef struct ReportTexInfo {
	PCTextureDef *pTex;
	PlayerCostume **eaCostumes;
} ReportTexInfo;

static void CostumePartAssetScan(ReportBoneInfo ***peaBoneInfos, ReportGeoInfo ***peaGeoInfos, ReportMatInfo ***peaMatInfos, ReportTexInfo ***peaTexInfos)
{
	DictionaryEArrayStruct *pCostumes = resDictGetEArrayStruct("PlayerCostume");
	DictionaryEArrayStruct *pBones = resDictGetEArrayStruct("CostumeBone");
	DictionaryEArrayStruct *pGeos = resDictGetEArrayStruct("CostumeGeometry");
	DictionaryEArrayStruct *pMats = resDictGetEArrayStruct("CostumeMaterial");
	DictionaryEArrayStruct *pTexs = resDictGetEArrayStruct("CostumeTexture");
	int i, j, k, n;

	// Create info structs
	for(i=0; i<eaSize(&pBones->ppReferents)-1; ++i) {
		ReportBoneInfo *pInfo = calloc(1, sizeof(ReportBoneInfo));
		pInfo->pBone = pBones->ppReferents[i];
		eaPush(peaBoneInfos, pInfo);
	}
	for(i=0; i<eaSize(&pGeos->ppReferents)-1; ++i) {
		ReportGeoInfo *pInfo = calloc(1, sizeof(ReportGeoInfo));
		pInfo->pGeo = pGeos->ppReferents[i];
		eaPush(peaGeoInfos, pInfo);
	}
	for(i=0; i<eaSize(&pMats->ppReferents)-1; ++i) {
		ReportMatInfo *pInfo = calloc(1, sizeof(ReportMatInfo));
		pInfo->pMat = pMats->ppReferents[i];
		eaPush(peaMatInfos, pInfo);
	}
	for(i=0; i<eaSize(&pTexs->ppReferents)-1; ++i) {
		ReportTexInfo *pInfo = calloc(1, sizeof(ReportTexInfo));
		pInfo->pTex = pTexs->ppReferents[i];
		eaPush(peaTexInfos, pInfo);
	}

	// Look at all costumes
	for(i=0; i<eaSize(&pCostumes->ppReferents); ++i) {
		PlayerCostume *pCostume = pCostumes->ppReferents[i];
		for(j=eaSize(&pCostume->eaParts)-1; j>=0; --j) {
			PCPart *pPart = pCostume->eaParts[j];
			PCBoneDef *pBone = GET_REF(pPart->hBoneDef);
			PCGeometryDef *pGeo = GET_REF(pPart->hGeoDef);
			PCMaterialDef *pMat = GET_REF(pPart->hMatDef);
			PCTextureDef *pTex1 = GET_REF(pPart->hDetailTexture);
			PCTextureDef *pTex2 = GET_REF(pPart->hDiffuseTexture);
			PCTextureDef *pTex3 = GET_REF(pPart->hPatternTexture);
			PCTextureDef *pTex4 = GET_REF(pPart->hSpecularTexture);
			PCTextureDef *pTex5 = pPart->pMovableTexture ? GET_REF(pPart->pMovableTexture->hMovableTexture) : NULL;

			if (pBone) {
				for(k=eaSize(peaBoneInfos)-1; k>=0; --k) {
					if ((*peaBoneInfos)[k]->pBone == pBone) {
						eaPushUnique(&(*peaBoneInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pGeo) {
				for(k=eaSize(peaGeoInfos)-1; k>=0; --k) {
					if ((*peaGeoInfos)[k]->pGeo == pGeo) {
						eaPushUnique(&(*peaGeoInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pMat) {
				for(k=eaSize(peaMatInfos)-1; k>=0; --k) {
					if ((*peaMatInfos)[k]->pMat == pMat) {
						eaPushUnique(&(*peaMatInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pTex1) {
				for(k=eaSize(peaTexInfos)-1; k>=0; --k) {
					if ((*peaTexInfos)[k]->pTex == pTex1) {
						eaPushUnique(&(*peaTexInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pTex2) {
				for(k=eaSize(peaTexInfos)-1; k>=0; --k) {
					if ((*peaTexInfos)[k]->pTex == pTex2) {
						eaPushUnique(&(*peaTexInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pTex3) {
				for(k=eaSize(peaTexInfos)-1; k>=0; --k) {
					if ((*peaTexInfos)[k]->pTex == pTex3) {
						eaPushUnique(&(*peaTexInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pTex4) {
				for(k=eaSize(peaTexInfos)-1; k>=0; --k) {
					if ((*peaTexInfos)[k]->pTex == pTex4) {
						eaPushUnique(&(*peaTexInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pTex5) {
				for(k=eaSize(peaTexInfos)-1; k>=0; --k) {
					if ((*peaTexInfos)[k]->pTex == pTex5) {
						eaPushUnique(&(*peaTexInfos)[k]->eaCostumes, pCostume);
						break;
					}
				}
			}
			if (pPart->pArtistData) {
				for(k=eaSize(&pPart->pArtistData->eaExtraTextures)-1; k>=0; --k) {
					PCTextureDef *pTex = GET_REF(pPart->pArtistData->eaExtraTextures[k]->hTexture);
					for(n=eaSize(peaTexInfos)-1; n>=0; --n) {
						if ((*peaTexInfos)[n]->pTex == pTex) {
							eaPushUnique(&(*peaTexInfos)[n]->eaCostumes, pCostume);
							break;
						}
					}
				}
			}
		}
	}
}

static void SkeletonDefAssetReport(ReportSkelDefInfo ***peaInfos, FILE *file)
{
	int i,j;

	for(i=0; i<eaSize(peaInfos); ++i) {
		ReportSkelDefInfo *pInfo = (*peaInfos)[i];
		if (pInfo) {
			fprintf(file, "---- Costume Skeleton: %s   (%s) ----\n", pInfo->pSkel->pcName, pInfo->pSkel->pcFileName);
			fprintf(file, "  Regions     = %d\n", eaSize(&pInfo->eaRegions));
			fprintf(file, "  Categories  = %d\n", eaSize(&pInfo->eaCategories));
			fprintf(file, "  Bones       = %d\n", eaSize(&pInfo->eaBones));
			fprintf(file, "  Geometries  = %d\n", eaSize(&pInfo->eaGeometries));
			if (eaSize(&pInfo->eaGeometries) < 5) {
				for(j=eaSize(&pInfo->eaGeometries)-1; j>=0; --j) {
					fprintf(file, "                > %s   (%s)\n", pInfo->eaGeometries[j]->pcName, pInfo->eaGeometries[j]->pcFileName);
				}
			}
			fprintf(file, "  Materials   = %d\n", eaSize(&pInfo->eaMaterials));
			if (eaSize(&pInfo->eaMaterials) < 5) {
				for(j=eaSize(&pInfo->eaMaterials)-1; j>=0; --j) {
					fprintf(file, "                > %s   (%s)\n", pInfo->eaMaterials[j]->pcName, pInfo->eaMaterials[j]->pcFileName);
				}
			}
			fprintf(file, "  Textures    = %d\n", eaSize(&pInfo->eaTextures));
			if (eaSize(&pInfo->eaTextures) < 5) {
				for(j=eaSize(&pInfo->eaTextures)-1; j>=0; --j) {
					fprintf(file, "                > %s   (%s)\n", pInfo->eaTextures[j]->pcName, pInfo->eaTextures[j]->pcFileName);
				}
			}
			fprintf(file, "  Costumes    = %d\n", eaSize(&pInfo->eaCostumes));
			if (eaSize(&pInfo->eaCostumes) < 5) {
				for(j=eaSize(&pInfo->eaCostumes)-1; j>=0; --j) {
					fprintf(file, "                > %s   (%s)\n", pInfo->eaCostumes[j]->pcName, pInfo->eaCostumes[j]->pcFileName);
				}
			}
			fprintf(file, "\n");
		}
	}
}

static void UnusedSkelInfoAssetReport(ReportSkelDefInfo ***peaInfos, FILE *file)
{
	DictionaryEArrayStruct *pSkelInfo = resDictGetEArrayStruct("SkelInfo");
	int i,j;
	bool bPrinted = false;

	for(i=eaSize(&pSkelInfo->ppReferents)-1; i>=0; --i) {
		SkelInfo *pInfo = pSkelInfo->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			PCSkeletonDef *pSkel = (*peaInfos)[j]->pSkel;
			if (stricmp(pSkel->pcSkeleton, pInfo->pcSkelInfoName) == 0) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unused SkelInfo Report ----\n");
				fprintf(file, "\nThe following SkelInfos are not used by any Costume Skeleton.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unused SkelInfo: %s   (%s)\n", pInfo->pcSkelInfoName, pInfo->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedRegionAssetReport(ReportSkelDefInfo ***peaInfos, FILE *file)
{
	DictionaryEArrayStruct *pRegions = resDictGetEArrayStruct("CostumeRegion");
	int i,j;
	bool bPrinted = false;

	for(i=eaSize(&pRegions->ppReferents)-1; i>=0; --i) {
		PCRegion *pRegion = pRegions->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			if (eaFind(&(*peaInfos)[j]->eaRegions, pRegion) != -1) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Region Report ----\n");
				fprintf(file, "\nThe following Costume Regions are not used by any Costume Skeleton.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced region: %s   (%s)\n", pRegion->pcName, pRegion->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedCategoryAssetReport(ReportSkelDefInfo ***peaInfos, FILE *file)
{
	DictionaryEArrayStruct *pCategories = resDictGetEArrayStruct("CostumeCategory");
	int i,j;
	bool bPrinted = false;

	for(i=eaSize(&pCategories->ppReferents)-1; i>=0; --i) {
		PCCategory *pCategory = pCategories->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			if (eaFind(&(*peaInfos)[j]->eaCategories, pCategory) != -1) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Category Report ----\n");
				fprintf(file, "\nThe following Costume Categories are not used by any referenced Costume Regions.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced category: %s   (%s)\n", pCategory->pcName, pCategory->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedBoneAssetReport(ReportSkelDefInfo ***peaInfos, ReportBoneInfo ***peaBoneInfos, FILE *file)
{
	DictionaryEArrayStruct *pBones = resDictGetEArrayStruct("CostumeBone");
	int i,j;
	bool bPrinted = false;

	for(i=eaSize(&pBones->ppReferents)-1; i>=0; --i) {
		PCBoneDef *pBone = pBones->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			if (eaFind(&(*peaInfos)[j]->eaBones, pBone) != -1) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Bone Report ----\n");
				fprintf(file, "\nThe following Costume Bones are not used by any Costume Skeleton.\n");
				fprintf(file, "This means the bone will not appear in the costume creator UI.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced bone: %s   (%s)\n", pBone->pcName, pBone->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}

	bPrinted = false;
	for(i=0; i<eaSize(peaBoneInfos); ++i) {
		PCBoneDef *pBone = (*peaBoneInfos)[i]->pBone;
		if (eaSize(&(*peaBoneInfos)[i]->eaCostumes) == 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unused Costume Bone Report ----\n");
				fprintf(file, "\nThe following Costume Bones are not used by any Costume.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unused bone: %s   (%s)\n", pBone->pcName, pBone->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedGeometryAssetReport(ReportSkelDefInfo ***peaInfos, ReportGeoInfo ***peaGeoInfos, FILE *file)
{
	DictionaryEArrayStruct *pGeos = resDictGetEArrayStruct("CostumeGeometry");
	int i,j;
	bool bPrinted = false;

	// Report on unreferenced geos
	for(i=eaSize(&pGeos->ppReferents)-1; i>=0; --i) {
		PCGeometryDef *pGeo = pGeos->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			if (eaFind(&(*peaInfos)[j]->eaGeometries, pGeo) != -1) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Geometry Report ----\n");
				fprintf(file, "\nThe following Costume Geometries are not referencing any Costume Bone that is part of a Costume Skeleton.\n");
				fprintf(file, "This means the geometry will not appear in the costume creator UI.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced geometry: %s   (%s)\n", pGeo->pcName, pGeo->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}

	// Report on unused geos
	bPrinted = false;
	for(i=0; i<eaSize(peaGeoInfos); ++i) {
		PCGeometryDef *pGeo = (*peaGeoInfos)[i]->pGeo;
		if ((eaSize(&(*peaGeoInfos)[i]->eaCostumes) == 0) && ((pGeo->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) == 0)) {
			if (!bPrinted) {
				fprintf(file, "---- Unused Costume Geometry Report ----\n");
				fprintf(file, "\nThe following Costume Geometries are not used by any Costume AND are not player legal.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unused geometry: %s   (%s)\n", pGeo->pcName, pGeo->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}

	// Report on geos that are player legal, but otherwise unused
	bPrinted = false;
	for(i=0; i<eaSize(peaGeoInfos); ++i) {
		PCGeometryDef *pGeo = (*peaGeoInfos)[i]->pGeo;
		if ((eaSize(&(*peaGeoInfos)[i]->eaCostumes) == 0) && ((pGeo->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) != 0)) {
			if (!bPrinted) {
				fprintf(file, "---- Unused Costume Geometry Report ----\n");
				fprintf(file, "\nThe following Costume Geometries are not used by any Costume BUT are  player legal so may possibly be used that way.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Likely Unused geometry: %s   (%s)\n", pGeo->pcName, pGeo->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedMaterialAssetReport(ReportSkelDefInfo ***peaInfos, ReportMatInfo ***peaMatInfos, FILE *file)
{
	DictionaryEArrayStruct *pMats = resDictGetEArrayStruct("CostumeMaterial");
	int i,j;
	bool bPrinted = false;

	for(i=eaSize(&pMats->ppReferents)-1; i>=0; --i) {
		PCMaterialDef *pMat = pMats->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			if (eaFind(&(*peaInfos)[j]->eaMaterials, pMat) != -1) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Material Report ----\n");
				fprintf(file, "\nThe following Costume Materials are not referenced by any Costume Geometry that is also referenced.\n");
				fprintf(file, "This means the material will not appear in the costume creator UI.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced material: %s   (%s)\n", pMat->pcName, pMat->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}

	bPrinted = false;
	for(i=0; i<eaSize(peaMatInfos); ++i) {
		PCMaterialDef *pMat = (*peaMatInfos)[i]->pMat;
		if ((eaSize(&(*peaMatInfos)[i]->eaCostumes) == 0) && ((pMat->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) == 0) ) {
			if (!bPrinted) {
				fprintf(file, "---- Unused Costume Material Report ----\n");
				fprintf(file, "\nThe following Costume Materials are not used by any Costume AND are not player legal.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unused material: %s   (%s)\n", pMat->pcName, pMat->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}

	bPrinted = false;
	for(i=0; i<eaSize(peaMatInfos); ++i) {
		PCMaterialDef *pMat = (*peaMatInfos)[i]->pMat;
		if ((eaSize(&(*peaMatInfos)[i]->eaCostumes) == 0) && ((pMat->eRestriction & (kPCRestriction_Player|kPCRestriction_Player_Initial)) != 0) ) {
			if (!bPrinted) {
				fprintf(file, "---- Unused Costume Material Report ----\n");
				fprintf(file, "\nThe following Costume Materials are not used by any Costume BUT are player legal so may be available that way.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Likely Unused material: %s   (%s)\n", pMat->pcName, pMat->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedTextureAssetReport(ReportSkelDefInfo ***peaInfos, ReportTexInfo ***peaTexInfos, FILE *file)
{
	DictionaryEArrayStruct *pTexs = resDictGetEArrayStruct("CostumeTexture");
	int i,j;
	bool bPrinted = false;

	for(i=eaSize(&pTexs->ppReferents)-1; i>=0; --i) {
		PCTextureDef *pTex = pTexs->ppReferents[i];
		for(j=eaSize(peaInfos)-1; j>=0; --j) {
			if (eaFind(&(*peaInfos)[j]->eaTextures, pTex) != -1) {
				break;
			}
		}
		if (j < 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Texture Report ----\n");
				fprintf(file, "\nThe following Costume Textures are not referenced by any Costume Material that is also referenced.\n");
				fprintf(file, "This means the texture will not appear in the costume creator UI.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced texture: %s   (%s)\n", pTex->pcName, pTex->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}

	bPrinted = false;
	for(i=0; i<eaSize(peaTexInfos); ++i) {
		PCTextureDef *pTex = (*peaTexInfos)[i]->pTex;
		if (eaSize(&(*peaTexInfos)[i]->eaCostumes) == 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unused Costume Texture Report ----\n");
				fprintf(file, "\nThe following Costume Textures are not used by any Costume.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unused texture: %s   (%s)\n", pTex->pcName, pTex->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
}

static void UnusedCostumeAssetReport(ReportCostumeInfo ***peaInfos, FILE *file)
{
	int i,count;
	bool bPrinted = false;

	for(i=0; i<eaSize(peaInfos); ++i) {
		ReportCostumeInfo *pInfo = (*peaInfos)[i];
		count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaCritterGroups) + eaSize(&pInfo->eaItems) + eaSize(&pInfo->eaPowers) + eaSize(&pInfo->eaRewards) + eaSize(&pInfo->eaContacts) + eaSize(&pInfo->eaSpecies) + eaSize(&pInfo->eaSets) + eaSize(&pInfo->eaSuperPets);
		if (count == 0) {
			if (!bPrinted) {
				fprintf(file, "---- Unreferenced Costume Report ----\n");
				fprintf(file, "\nThe following Costumes are not referenced by any Critter, Critter Group, Item, Power, SuperCritterPetDef, Contact, Species, Costume Set, or Reward Table.\n\n");
				bPrinted = true;
			}
			fprintf(file, "  Unreferenced costume: %s   (%s)\n", pInfo->pCostume->pcName, pInfo->pCostume->pcFileName);
		}
	}
	if (bPrinted) {
		fprintf(file, "\n");
	}
	//for(i=0; i<eaSize(peaInfos); ++i) {
	//	ReportCostumeInfo *pInfo = (*peaInfos)[i];
	//	count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaCritterGroups) + eaSize(&pInfo->eaItems) + eaSize(&pInfo->eaPowers) + eaSize(&pInfo->eaRewards);
	//	if (count > 0) {
	//		if (!bPrinted) {
	//			fprintf(file, "---- Referenced COREDEFAULT Costume Report ----\n");
	//			fprintf(file, "\nThe following Costumes are not referenced by any Critter, Critter Group, Item, Power, or Reward Table.\n\n");
	//			bPrinted = true;
	//		}
	//		if (stricmp(GET_REF(pInfo->pCostume->hSkeleton)->pcName, "CoreDefault") == 0) {
	//			fprintf(file, "  Referenced COREDEFAULT costume: %s   (%s)\n", pInfo->pCostume->pcName, pInfo->pCostume->pcFileName);
	//		}
	//	}
	//}
	//if (bPrinted) {
	//	fprintf(file, "\n");
	//}
}

static void GeometryFlagReport(ReportSkelDefInfo ***peaInfos, ReportGeoInfo ***peaGeoInfos, FILE *file)
{
	DictionaryEArrayStruct *pGeos = resDictGetEArrayStruct("CostumeGeometry");
	bool bPrinted = false;
	PCGeometryDef **player_initial_list = NULL;
	PCGeometryDef **player_unlock_list = NULL;
	PCGeometryDef **npc_only_list = NULL;
	PCGeometryDef **leftover_list = NULL;

	// Report on unreferenced geos
	FOR_EACH_IN_EARRAY(pGeos->ppReferents,PCGeometryDef,pGeo) {
		if (!!(pGeo->eRestriction & kPCRestriction_Player_Initial)) {
			eaPush(&player_initial_list,pGeo);
		} else if (!!(pGeo->eRestriction & kPCRestriction_Player)) {
			eaPush(&player_unlock_list,pGeo);
		} else if (!!(pGeo->eRestriction & kPCRestriction_NPC)) {
			eaPush(&npc_only_list,pGeo);
		} else {
			eaPush(&leftover_list,pGeo);
		}
	}
	FOR_EACH_END;

	fprintf(file, "---- Costume Geometry Flag Report ----------------------------------\n");
	fprintf(file, "---- Player Initial Flag Report ----\n");
	FOR_EACH_IN_EARRAY(player_initial_list,PCGeometryDef,pGeo) {
		fprintf(file, "%s\n", pGeo->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&player_initial_list);
	fprintf(file, "\n");

	fprintf(file, "---- Player Unlock Flag Report ----\n");
	FOR_EACH_IN_EARRAY(player_unlock_list,PCGeometryDef,pGeo) {
		fprintf(file, "%s\n", pGeo->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&player_unlock_list);
	fprintf(file, "\n");

	fprintf(file, "---- NPC Only Flag Report ----\n");
	FOR_EACH_IN_EARRAY(npc_only_list,PCGeometryDef,pGeo) {
		fprintf(file, "%s\n", pGeo->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&npc_only_list);
	fprintf(file, "\n");

	fprintf(file, "---- Leftover Flag Report ----\n");
	FOR_EACH_IN_EARRAY(leftover_list,PCGeometryDef,pGeo) {
		fprintf(file, "%s\n", pGeo->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&leftover_list);
	fprintf(file, "\n");

	fprintf(file, "\n");
}

static void MaterialFlagReport(ReportSkelDefInfo ***peaInfos, ReportMatInfo ***peaMatInfos, FILE *file)
{
	DictionaryEArrayStruct *pMats = resDictGetEArrayStruct("CostumeMaterial");
	bool bPrinted = false;
	PCMaterialDef **player_initial_list = NULL;
	PCMaterialDef **player_unlock_list = NULL;
	PCMaterialDef **npc_only_list = NULL;
	PCMaterialDef **leftover_list = NULL;

	// Report on unreferenced geos
	FOR_EACH_IN_EARRAY(pMats->ppReferents,PCMaterialDef,pMat) {
		if (!!(pMat->eRestriction & kPCRestriction_Player_Initial)) {
			eaPush(&player_initial_list,pMat);
		} else if (!!(pMat->eRestriction & kPCRestriction_Player)) {
			eaPush(&player_unlock_list,pMat);
		} else if (!!(pMat->eRestriction & kPCRestriction_NPC)) {
			eaPush(&npc_only_list,pMat);
		} else {
			eaPush(&leftover_list,pMat);
		}
	}
	FOR_EACH_END;

	fprintf(file, "---- Costume Material Flag Report ----------------------------------\n");
	fprintf(file, "---- Player Initial Flag Report ----\n");
	FOR_EACH_IN_EARRAY(player_initial_list,PCMaterialDef,pMat) {
		fprintf(file, "%s\n", pMat->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&player_initial_list);
	fprintf(file, "\n");

	fprintf(file, "---- Player Unlock Flag Report ----\n");
	FOR_EACH_IN_EARRAY(player_unlock_list,PCMaterialDef,pMat) {
		fprintf(file, "%s\n", pMat->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&player_unlock_list);
	fprintf(file, "\n");

	fprintf(file, "---- NPC Only Flag Report ----\n");
	FOR_EACH_IN_EARRAY(npc_only_list,PCMaterialDef,pMat) {
		fprintf(file, "%s\n", pMat->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&npc_only_list);
	fprintf(file, "\n");

	fprintf(file, "---- Leftover Flag Report ----\n");
	FOR_EACH_IN_EARRAY(leftover_list,PCMaterialDef,pMat) {
		fprintf(file, "%s\n", pMat->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&leftover_list);
	fprintf(file, "\n");

	fprintf(file, "\n");
}

static void TextureFlagReport(ReportSkelDefInfo ***peaInfos, ReportTexInfo ***peaTexInfos, FILE *file)
{
	DictionaryEArrayStruct *pTexs = resDictGetEArrayStruct("CostumeTexture");
	bool bPrinted = false;
	PCTextureDef **player_initial_list = NULL;
	PCTextureDef **player_unlock_list = NULL;
	PCTextureDef **npc_only_list = NULL;
	PCTextureDef **leftover_list = NULL;

	// Report on unreferenced geos
	FOR_EACH_IN_EARRAY(pTexs->ppReferents,PCTextureDef,pTex) {
		if (!!(pTex->eRestriction & kPCRestriction_Player_Initial)) {
			eaPush(&player_initial_list,pTex);
		} else if (!!(pTex->eRestriction & kPCRestriction_Player)) {
			eaPush(&player_unlock_list,pTex);
		} else if (!!(pTex->eRestriction & kPCRestriction_NPC)) {
			eaPush(&npc_only_list,pTex);
		} else {
			eaPush(&leftover_list,pTex);
		}
	}
	FOR_EACH_END;

	fprintf(file, "---- Costume Texture Flag Report ----------------------------------\n");
	fprintf(file, "---- Player Initial Flag Report ----\n");
	FOR_EACH_IN_EARRAY(player_initial_list,PCTextureDef,pTex) {
		fprintf(file, "%s\n", pTex->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&player_initial_list);
	fprintf(file, "\n");

	fprintf(file, "---- Player Unlock Flag Report ----\n");
	FOR_EACH_IN_EARRAY(player_unlock_list,PCTextureDef,pTex) {
		fprintf(file, "%s\n", pTex->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&player_unlock_list);
	fprintf(file, "\n");

	fprintf(file, "---- NPC Only Flag Report ----\n");
	FOR_EACH_IN_EARRAY(npc_only_list,PCTextureDef,pTex) {
		fprintf(file, "%s\n", pTex->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&npc_only_list);
	fprintf(file, "\n");

	fprintf(file, "---- Leftover Flag Report ----\n");
	FOR_EACH_IN_EARRAY(leftover_list,PCTextureDef,pTex) {
		fprintf(file, "%s\n", pTex->pcName);
	}
	FOR_EACH_END;
	eaDestroy(&leftover_list);
	fprintf(file, "\n");

	fprintf(file, "\n");
}

typedef struct AssetGeoModelPair {
	const char *pcGeo;
	const char *pcModel;
	int count;
} AssetGeoModelPair;

static int AssetReportCompareGeoModel(const AssetGeoModelPair** left, const AssetGeoModelPair** right)
{
	return stricmp((*left)->pcGeo,(*right)->pcGeo);
}

static void UsedSysGeometryAssetReport(ReportSkelDefInfo ***peaInfos, FILE *file)
{
	DictionaryEArrayStruct *pGeos = resDictGetEArrayStruct("CostumeGeometry");
	AssetGeoModelPair **eaGeos = NULL;
	int i,j;

	for(i=0; i<eaSize(&pGeos->ppReferents)-1; ++i) {
		PCGeometryDef *pGeo = pGeos->ppReferents[i];

		for(j=eaSize(&eaGeos)-1; j>=0; --j) {
			if ((stricmp(eaGeos[j]->pcGeo, pGeo->pcGeometry) == 0) &&
				((!eaGeos[j]->pcModel && !pGeo->pcModel) || (eaGeos[j]->pcModel && pGeo->pcModel && stricmp(eaGeos[j]->pcModel, pGeo->pcModel) == 0))) {
				++eaGeos[j]->count;
				break;
			}
		}
		if (j < 0) {
			AssetGeoModelPair *pPair = calloc(1, sizeof(AssetGeoModelPair));
			pPair->pcGeo = allocAddString(pGeo->pcGeometry);
			if (pGeo->pcModel) {
				pPair->pcModel = allocAddString(pGeo->pcModel);
			}
			pPair->count = 1;
			eaPush(&eaGeos, pPair);
		}
	}
	eaQSort(eaGeos, AssetReportCompareGeoModel);

	fprintf(file, "---- Used Geometries ----\n");
	fprintf(file, "\nThe following '.ModelHeader' files are referenced by a Costume Geometry.\n");
	fprintf(file, "The count indicates how many times the file is referenced.\n");
	fprintf(file, "The parenthetical value after the file is the name of the Model that is referenced.\n\n");
	for(i=0; i<eaSize(&eaGeos); ++i) {
		if (eaGeos[i]->pcModel) {
			fprintf(file, "  (%3d) %s   (%s)\n", eaGeos[i]->count, eaGeos[i]->pcGeo, eaGeos[i]->pcModel);
		} else {
			fprintf(file, "  (%3d) %s\n", eaGeos[i]->count, eaGeos[i]->pcGeo);
		}
	}
	fprintf(file, "\n");

	eaDestroyEx(&eaGeos, NULL);
}

typedef struct AssetMaterial {
	const char *pcMaterial;
	int count;
} AssetMaterial;

static int AssetReportCompareMaterial(const AssetMaterial** left, const AssetMaterial** right)
{
	return stricmp((*left)->pcMaterial,(*right)->pcMaterial);
}

static void UsedSysMaterialAssetReport(ReportSkelDefInfo ***peaInfos, AssetMaterial ***peaMats, ClientGraphicsLookupRequest *pRequest)
{
	DictionaryEArrayStruct *pMats = resDictGetEArrayStruct("CostumeMaterial");
	int i,j;

	for(i=eaSize(&pMats->ppReferents)-1; i>=0; --i) {
		PCMaterialDef *pMat = pMats->ppReferents[i];

		for(j=eaSize(peaMats)-1; j>=0; --j) {
			if (stricmp(pMat->pcMaterial, (*peaMats)[j]->pcMaterial) == 0) {
				++(*peaMats)[j]->count;
				break;
			}
		}
		if (j < 0) {
			AssetMaterial *pAsset = calloc(1,sizeof(AssetMaterial));
			pAsset->pcMaterial = allocAddString(pMat->pcMaterial);
			pAsset->count = 1;
			eaPush(peaMats, pAsset);
		}
	}

	// Collect data to send to client
	for(i=0; i<eaSize(peaMats); ++i ){
		AssetMaterial *pAsset = (*peaMats)[i];
		ClientGraphicsMaterialAsset *pClientData = StructCreate(parse_ClientGraphicsMaterialAsset);
		pClientData->pcMaterialName = StructAllocString(pAsset->pcMaterial);
		pClientData->count = pAsset->count;
		eaPush(&pRequest->eaMaterials, pClientData);
	}
}

typedef struct AssetTexture {
	const char *pcTexture;
	const char *pcFilename;
	int count;
} AssetTexture;

static int AssetReportCompareTexture(const AssetTexture** left, const AssetTexture** right)
{
	return stricmp((*left)->pcTexture,(*right)->pcTexture);
}

static void UsedSysTextureAssetReport(ReportSkelDefInfo ***peaInfos, AssetMaterial ***peaMats, ClientGraphicsLookupRequest *pRequest)
{
	DictionaryEArrayStruct *pTexs = resDictGetEArrayStruct("CostumeTexture");
	AssetTexture **eaTexs = NULL;
	int i,j,k;

	for(i=eaSize(&pTexs->ppReferents)-1; i>=0; --i) {
		PCTextureDef *pTex = pTexs->ppReferents[i];

		for(j=eaSize(&eaTexs)-1; j>=0; --j) {
			if (stricmp(pTex->pcNewTexture, eaTexs[j]->pcTexture) == 0) {
				++eaTexs[j]->count;
				break;
			}
		}
		if (j < 0) {
			AssetTexture *pAsset = calloc(1,sizeof(AssetTexture));
			pAsset->pcTexture = allocAddString(pTex->pcNewTexture);
			pAsset->count = 1;
			eaPush(&eaTexs, pAsset);
		}

		for(k=eaSize(&pTex->eaExtraSwaps)-1; k>=0; --k) {
			PCExtraTexture *pExtra = pTex->eaExtraSwaps[k];

			for(j=eaSize(&eaTexs)-1; j>=0; --j) {
				if (stricmp(pExtra->pcNewTexture, eaTexs[j]->pcTexture) == 0) {
					++eaTexs[j]->count;
					break;
				}
			}
			if (j < 0) {
				AssetTexture *pAsset = calloc(1,sizeof(AssetTexture));
				pAsset->pcTexture = allocAddString(pExtra->pcNewTexture);
				pAsset->count = 1;
				eaPush(&eaTexs, pAsset);
			}
		}
	}
	for(i=eaSize(peaMats)-1; i>=0; --i) {
		Material *pMaterial = materialFindNoDefault((*peaMats)[i]->pcMaterial, 0);
		if (pMaterial) {
			StashTable pStash;
			StashElement pElement;
			StashTableIterator pIter;

			// Get the texture names off the material
			pStash = stashTableCreateWithStringKeys(10, StashDefault);
			materialGetTextureNames(pMaterial, pStash, NULL);
			stashGetIterator(pStash, &pIter);
			while(stashGetNextElement(&pIter, &pElement)) {
				const char *pcTexName = stashElementGetStringKey(pElement);

				for(j=eaSize(&eaTexs)-1; j>=0; --j) {
					if (stricmp(pcTexName, eaTexs[j]->pcTexture) == 0) {
						++eaTexs[j]->count;
						break;
					}
				}
				if (j < 0) {
					AssetTexture *pAsset = calloc(1,sizeof(AssetTexture));
					pAsset->pcTexture = allocAddString(pcTexName);
					pAsset->count = 1;
					eaPush(&eaTexs, pAsset);
				}
			}
		}
	}

	// Collect data to send to client
	for(i=0; i<eaSize(&eaTexs); ++i ){
		AssetTexture *pAsset = eaTexs[i];
		ClientGraphicsTextureAsset *pClientData = StructCreate(parse_ClientGraphicsTextureAsset);
		pClientData->pcTextureName = StructAllocString(pAsset->pcTexture);
		pClientData->count = pAsset->count;
		eaPush(&pRequest->eaTextures, pClientData);
	}

	eaDestroyEx(&eaTexs, NULL);
}

static void UsedCostumeAssetReport(ReportCostumeInfo ***peaInfos, FILE *file)
{
	int i,count;

	fprintf(file, "---- Used Costume Report ----\n");
	fprintf(file, "\nThe following Costumes are referenced by Critter, Critter Group, Item, Power, Contact, Species, Costume Set, or Reward Table.\n\n");
	fprintf(file, "The count indicates how many times the costume is referenced.\n\n");

	for(i=0; i<eaSize(peaInfos); ++i) {
		ReportCostumeInfo *pInfo = (*peaInfos)[i];
		count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaCritterGroups) + eaSize(&pInfo->eaItems) + eaSize(&pInfo->eaPowers) + eaSize(&pInfo->eaRewards) + eaSize(&pInfo->eaContacts) + eaSize(&pInfo->eaSpecies) + eaSize(&pInfo->eaSets);
		if (count > 0) {
			fprintf(file, "  (%3d) %s   (%s)\n", count, pInfo->pCostume->pcName, pInfo->pCostume->pcFileName);
		}
	}
	fprintf(file, "\n");
}

static void SkeletonDefAssetCleanup(ReportSkelDefInfo ***peaInfos)
{
	int i;

	for(i=eaSize(peaInfos)-1; i>=0; --i) {
		ReportSkelDefInfo *pInfo = (*peaInfos)[i];
		if (pInfo) {
			eaDestroy(&pInfo->eaRegions);
			eaDestroy(&pInfo->eaCategories);
			eaDestroy(&pInfo->eaBones);
			eaDestroy(&pInfo->eaGeometries);
			eaDestroy(&pInfo->eaMaterials);
			eaDestroy(&pInfo->eaTextures);
			eaDestroy(&pInfo->eaCostumes);
		}
	}
	eaDestroyEx(peaInfos, NULL);
}

static void CostumeAssetCleanup(ReportCostumeInfo ***peaInfos)
{
	int i;

	for(i=eaSize(peaInfos)-1; i>=0; --i) {
		ReportCostumeInfo *pInfo = (*peaInfos)[i];
		if (pInfo) {
			eaDestroy(&pInfo->eaCritters);
			eaDestroy(&pInfo->eaCritterGroups);
			eaDestroy(&pInfo->eaItems);
			eaDestroy(&pInfo->eaPowers);
			eaDestroy(&pInfo->eaRewards);
		}
	}
	eaDestroyEx(peaInfos, NULL);
}

static void CostumePartAssetCleanup(ReportBoneInfo ***peaBoneInfos, ReportGeoInfo ***peaGeoInfos, ReportMatInfo ***peaMatInfos, ReportTexInfo ***peaTexInfos)
{
	int i;

	for(i=eaSize(peaBoneInfos)-1; i>=0; --i) {
		ReportBoneInfo *pInfo = (*peaBoneInfos)[i];
		if (pInfo) {
			eaDestroy(&pInfo->eaCostumes);
		}
	}
	eaDestroy(peaBoneInfos);

	for(i=eaSize(peaGeoInfos)-1; i>=0; --i) {
		ReportGeoInfo *pInfo = (*peaGeoInfos)[i];
		if (pInfo) {
			eaDestroy(&pInfo->eaCostumes);
		}
	}
	eaDestroy(peaGeoInfos);

	for(i=eaSize(peaMatInfos)-1; i>=0; --i) {
		ReportMatInfo *pInfo = (*peaMatInfos)[i];
		if (pInfo) {
			eaDestroy(&pInfo->eaCostumes);
		}
	}
	eaDestroy(peaMatInfos);

	for(i=eaSize(peaTexInfos)-1; i>=0; --i) {
		ReportTexInfo *pInfo = (*peaTexInfos)[i];
		if (pInfo) {
			eaDestroy(&pInfo->eaCostumes);
		}
	}
	eaDestroy(peaTexInfos);
}

AUTO_COMMAND ACMD_SERVERCMD;
char *ReportCostumeAssets(Entity *pEnt)
{
	FILE *file;
	ReportSkelDefInfo **eaSkelDefInfos = NULL;
	ReportCostumeInfo **eaCostumeInfos = NULL;
	ReportBoneInfo **eaBoneInfos = NULL;
	ReportGeoInfo **eaGeoInfos = NULL;
	ReportMatInfo **eaMatInfos = NULL;
	ReportTexInfo **eaTexInfos = NULL;
	AssetMaterial **eaAssetMats = NULL;
	ClientGraphicsLookupRequest *pRequest = NULL;

	file = fopen("c:\\CostumeAssetReport.txt", "w");
	
	if(!file){
		return "Can't open file 'c:\\CostumeAssetReport.txt'.\n";
	}

	pRequest = StructCreate(parse_ClientGraphicsLookupRequest);

	SkeletonDefAssetScan(&eaSkelDefInfos);
	CostumeAssetScan(&eaCostumeInfos);
	CostumePartAssetScan(&eaBoneInfos, &eaGeoInfos, &eaMatInfos, &eaTexInfos);

	fprintf(file, "==============================================================\n");
	fprintf(file, "Skeleton Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	SkeletonDefAssetReport(&eaSkelDefInfos, file);

	fprintf(file, "==============================================================\n");
	fprintf(file, "Unused and Unreferenced Assets\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	UnusedSkelInfoAssetReport(&eaSkelDefInfos, file);
	UnusedRegionAssetReport(&eaSkelDefInfos, file);
	UnusedCategoryAssetReport(&eaSkelDefInfos, file);
	UnusedBoneAssetReport(&eaSkelDefInfos, &eaBoneInfos, file);
	UnusedGeometryAssetReport(&eaSkelDefInfos, &eaGeoInfos, file);
	UnusedMaterialAssetReport(&eaSkelDefInfos, &eaMatInfos, file);
	UnusedTextureAssetReport(&eaSkelDefInfos, &eaTexInfos, file);
	UnusedCostumeAssetReport(&eaCostumeInfos, file);

	fprintf(file, "==============================================================\n");
	fprintf(file, "Used Binary Assets\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	UsedCostumeAssetReport(&eaCostumeInfos, file);
	UsedSysGeometryAssetReport(&eaSkelDefInfos, file);
	UsedSysMaterialAssetReport(&eaSkelDefInfos, &eaAssetMats, pRequest);
	UsedSysTextureAssetReport(&eaSkelDefInfos, &eaAssetMats, pRequest);

	SkeletonDefAssetCleanup(&eaSkelDefInfos);
	CostumeAssetCleanup(&eaCostumeInfos);
	CostumePartAssetCleanup(&eaBoneInfos, &eaGeoInfos, &eaMatInfos, &eaTexInfos);

	eaDestroyEx(&eaAssetMats, NULL);
	fclose(file);

	ClientCmd_CostumeAssetReportForMaterialsAndTextures(pEnt, "C:\\CostumeAssetReport.txt", pRequest);

	StructDestroy(parse_ClientGraphicsLookupRequest, pRequest);

	return "Costume asset report written to 'c:\\CostumeAssetReport.txt'\n";
}

AUTO_COMMAND ACMD_SERVERCMD;
char *ReportCostumeFlags(void)
{
	FILE *file;
	ReportSkelDefInfo **eaSkelDefInfos = NULL;
	ReportCostumeInfo **eaCostumeInfos = NULL;
	ReportBoneInfo **eaBoneInfos = NULL;
	ReportGeoInfo **eaGeoInfos = NULL;
	ReportMatInfo **eaMatInfos = NULL;
	ReportTexInfo **eaTexInfos = NULL;

	file = fopen("c:\\CostumeAssetFlags.txt", "w");

	if(!file){
		return "Can't open file 'c:\\CostumeAssetFlags.txt'.\n";
	}

	SkeletonDefAssetScan(&eaSkelDefInfos);
	CostumeAssetScan(&eaCostumeInfos);
	CostumePartAssetScan(&eaBoneInfos, &eaGeoInfos, &eaMatInfos, &eaTexInfos);

	fprintf(file, "==============================================================\n");
	fprintf(file, "Assets with Flags\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	GeometryFlagReport(&eaSkelDefInfos, &eaGeoInfos, file);
	MaterialFlagReport(&eaSkelDefInfos, &eaMatInfos, file);
	TextureFlagReport(&eaSkelDefInfos, &eaTexInfos, file);

	fclose(file);

	return "Costume asset report written to 'c:\\CostumeAssetFlags.txt'\n";
}


AUTO_COMMAND ACMD_SERVERCMD;
char *ReportNonUGCCostumeParts(void)
{
	FILE *file;
	DictionaryEArrayStruct *pSpecies = resDictGetEArrayStruct("SpeciesDef");
	DictionaryEArrayStruct *pSkeletons = resDictGetEArrayStruct("CostumeSkeleton");
	DictionaryEArrayStruct *pGeometries = resDictGetEArrayStruct("CostumeGeometry");
	DictionaryEArrayStruct *pMaterials = resDictGetEArrayStruct("CostumeMaterial");
	DictionaryEArrayStruct *pTextures = resDictGetEArrayStruct("CostumeTexture");
	int i;

	file = fopen("c:\\CostumeNonUGCParts.txt", "w");

	if(!file){
		return "Can't open file 'c:\\CostumeNonUGCParts.txt'.\n";
	}

	fprintf(file, "==============================================================\n");
	fprintf(file, "Costume Non-UGC Parts\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	for(i=0; i<eaSize(&pSpecies->ppReferents); ++i) {
		SpeciesDef *pSpec = pSpecies->ppReferents[i];
		if (((pSpec->eRestriction & kPCRestriction_Player_Initial) == 0) &&
			((pSpec->eRestriction & kPCRestriction_Player) != 0) &&
			((pSpec->eRestriction & kPCRestriction_UGC_Initial) == 0)) {
			fprintf(file, "Species: %s (%s)\n", pSpec->pcName, pSpec->pcFileName);
		}
	}
	fprintf(file, "\n");

	for(i=0; i<eaSize(&pSkeletons->ppReferents); ++i) {
		PCSkeletonDef *pSkel = pSkeletons->ppReferents[i];
		if (((pSkel->eRestriction & kPCRestriction_Player_Initial) == 0) &&
			((pSkel->eRestriction & kPCRestriction_Player) != 0) &&
			((pSkel->eRestriction & kPCRestriction_UGC_Initial) == 0)) {
			fprintf(file, "Skeleton: %s (%s)\n", pSkel->pcName, pSkel->pcFileName);
		}
	}
	fprintf(file, "\n");

	for(i=0; i<eaSize(&pGeometries->ppReferents); ++i) {
		PCGeometryDef *pGeo = pGeometries->ppReferents[i];
		if (((pGeo->eRestriction & kPCRestriction_Player_Initial) == 0) &&
			((pGeo->eRestriction & kPCRestriction_Player) != 0) &&
			((pGeo->eRestriction & kPCRestriction_UGC_Initial) == 0)) {
			fprintf(file, "Geometry: %s (%s)\n", pGeo->pcName, pGeo->pcFileName);

			/*
			PCGeometryDef *pCopy;
			pCopy = StructClone(parse_PCGeometryDef, pGeo);
			pCopy->eRestriction |= kPCRestriction_UGC_Initial;
			if (!ParserWriteTextFileFromSingleDictionaryStruct(pCopy->pcFileName, "CostumeGeometry", pCopy, 0, 0)) {
				fprintf(file, "Geometry FAILED: (%s)\n", pCopy->pcFileName);
			}
			StructDestroy(parse_PCGeometryDef, pCopy);
			*/
		}
	}
	fprintf(file, "\n");

	for(i=0; i<eaSize(&pMaterials->ppReferents); ++i) {
		PCMaterialDef *pMat = pMaterials->ppReferents[i];
		if (((pMat->eRestriction & kPCRestriction_Player_Initial) == 0) &&
			((pMat->eRestriction & kPCRestriction_Player) != 0) &&
			((pMat->eRestriction & kPCRestriction_UGC_Initial) == 0)) {
			fprintf(file, "Material: %s (%s)\n", pMat->pcName, pMat->pcFileName);

			/*
			PCMaterialDef *pCopy;
			pCopy = StructClone(parse_PCMaterialDef, pMat);
			pCopy->eRestriction |= kPCRestriction_UGC_Initial;
			if (!ParserWriteTextFileFromSingleDictionaryStruct(pCopy->pcFileName, "CostumeMaterial", pCopy, 0, 0)) {
				fprintf(file, "Material FAILED: (%s)\n", pCopy->pcFileName);
			}
			StructDestroy(parse_PCMaterialDef, pCopy);
			*/
		}
	}
	fprintf(file, "\n");

	for(i=0; i<eaSize(&pTextures->ppReferents); ++i) {
		PCTextureDef *pTex = pTextures->ppReferents[i];
		if (((pTex->eRestriction & kPCRestriction_Player_Initial) == 0) &&
			((pTex->eRestriction & kPCRestriction_Player) != 0) &&
			((pTex->eRestriction & kPCRestriction_UGC_Initial) == 0)) {
			fprintf(file, "Texture: %s (%s)\n", pTex->pcName, pTex->pcFileName);

			/*
			PCTextureDef *pCopy;
			pCopy = StructClone(parse_PCTextureDef, pTex);
			pCopy->eRestriction |= kPCRestriction_UGC_Initial;
			if (!ParserWriteTextFileFromSingleDictionaryStruct(pCopy->pcFileName, "CostumeTexture", pCopy, 0, 0)) {
				fprintf(file, "Texture FAILED: (%s)\n", pCopy->pcFileName);
			}
			StructDestroy(parse_PCTextureDef, pCopy);
			*/
		}
	}
	fprintf(file, "\n");

	fclose(file);

	return "Costume non-UGC report written to 'c:\\CostumeNonUGCParts.txt'\n";
}


// ---------------------- Code to dump an asset report on game assets  ---------------------------

typedef struct AssetPowerInfo {
	PowerDef *pPower;

	PowerDef **eaPowers;
	CritterDef **eaCritters;
	ItemPowerDef **eaItemPowers;
	PowerTreeDef **eaTrees;
	EncounterLayer **eaLayers;
	bool bFound;
} AssetPowerInfo;

static void PowersAssetAddPowerRef(AssetPowerInfo **eaInfos, PowerDef *pPower, PowerDef *pPowerRef)
{
	if(pPower && pPowerRef)
	{
		int i;
		for(i=eaSize(&eaInfos)-1; i>=0; i--)
		{
			if(eaInfos[i]->pPower == pPower)
			{
				eaPush(&eaInfos[i]->eaPowers, pPowerRef);
				break;
			}
		}
	}
}

static void PowersAssetReport(FILE *file, EncounterLayer ***peaLayers)
{
	DictionaryEArrayStruct *pPowers = resDictGetEArrayStruct("PowerDef");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pItemPowers = resDictGetEArrayStruct("ItemPowerDef");
	DictionaryEArrayStruct *pTrees = resDictGetEArrayStruct("PowerTreeDef");
	DictionaryEArrayStruct *pStates = resDictGetEArrayStruct("CombatPowerStateSwitchingDef");
	AssetPowerInfo **eaInfos = NULL;
	int i, j, k, n, m, iPow;

	// Create tracking data
	for(i=0; i<eaSize(&pPowers->ppReferents); ++i) {
		AssetPowerInfo *pInfo = calloc(1, sizeof(AssetPowerInfo));
		pInfo->pPower = pPowers->ppReferents[i];
		pInfo->bFound = false;
		eaPush(&eaInfos, pInfo);
	}

	// Tally powers
	for(i=eaSize(&pPowers->ppReferents)-1; i>=0; --i) {
		PowerDef *pPower = pPowers->ppReferents[i];
		const char *pcParentName = StructInherit_GetParentName(parse_PowerDef, pPower);
		if (pcParentName) {
			PowersAssetAddPowerRef(eaInfos,RefSystem_ReferentFromString("PowerDef", pcParentName),pPower);
		}

		// Combos
		for(j=eaSize(&pPower->ppCombos)-1; j>=0; --j) {
			// Not sure if this was working right before, since the combo child power name wasn't used
//			if (pPower->ppCombos[j]->pchPower) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pPower == GET_REF(pPower->ppCombos[j]->hPower)) {
						eaPush(&eaInfos[k]->eaPowers, pPower);
						break;
					}
				}
//			}
		}

		// AttribMods
		for(j=eaSize(&pPower->ppMods)-1; j>=0; --j) {
			AttribModDef *pMod = pPower->ppMods[j];
			if(pMod->offAttrib==kAttribType_ApplyPower)
			{
				ApplyPowerParams *pParams = (ApplyPowerParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}
			else if(pMod->offAttrib==kAttribType_DamageTrigger)
			{
				DamageTriggerParams *pParams = (DamageTriggerParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}
			else if(pMod->offAttrib==kAttribType_GrantPower)
			{
				GrantPowerParams *pParams = (GrantPowerParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}
			else if(pMod->offAttrib==kAttribType_KillTrigger)
			{
				KillTriggerParams *pParams = (KillTriggerParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}
			else if(pMod->offAttrib==kAttribType_RemovePower)
			{
				RemovePowerParams *pParams = (RemovePowerParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}
			else if(pMod->offAttrib==kAttribType_TriggerComplex)
			{
				TriggerComplexParams *pParams = (TriggerComplexParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}
			else if(pMod->offAttrib==kAttribType_TriggerSimple)
			{
				TriggerSimpleParams *pParams = (TriggerSimpleParams*)pMod->pParams;
				PowersAssetAddPowerRef(eaInfos,GET_REF(pParams->hDef),pPower);
			}

			if(pMod->pExpiration)
			{
				PowersAssetAddPowerRef(eaInfos,GET_REF(pMod->pExpiration->hDef),pPower);
			}
		}
	}

	// Tally critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		for(j=eaSize(&pCritter->ppPowerConfigs)-1; j>=0; --j) {
			CritterPowerConfig *pConfig = pCritter->ppPowerConfigs[j];
			PowerDef *pPower = GET_REF(pConfig->hPower);
			if (pPower) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pPower == pPower) {
						eaPush(&eaInfos[k]->eaCritters, pCritter);
						break;
					}
				}
			}
		}
	}

	// Tally states
	for(i=eaSize(&pStates->ppReferents)-1; i>=0; --i) {
		CombatPowerStateSwitchingDef *pState = pStates->ppReferents[i];
		for(j=eaSize(&pState->eaPowerSet)-1; j>=0; --j) {
			CombatPowerStatePowerSet *pSet = pState->eaPowerSet[j];
			for(iPow=eaSize(&pSet->eaPowers)-1; iPow>=0; --iPow) {
				PowerDef *pPower = GET_REF(pSet->eaPowers[iPow]->hPowerDef);
				if (pPower) {
					for(k=eaSize(&eaInfos)-1; k>=0; --k) {
						if (eaInfos[k]->pPower == pPower) {
							eaInfos[k]->bFound = true;
							break;
						}
					}
				}
			}
		}
		for(j=eaSize(&pState->eaStates)-1; j>=0; --j) {
			CombatPowerStateDef *pStateDef = pState->eaStates[j];
			PowerDef *pPower = GET_REF(pStateDef->hApplyPowerDef);
			if (pPower) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pPower == pPower) {
						eaInfos[k]->bFound = true;
						break;
					}
				}
			}
		}
	}

	// Tally item powers
	for(i=eaSize(&pItemPowers->ppReferents)-1; i>=0; --i) {
		ItemPowerDef *pItemPower = pItemPowers->ppReferents[i];
		PowerDef *pPower = GET_REF(pItemPower->hPower);
		if (pPower) {
			for(j=eaSize(&eaInfos)-1; j>=0; --j) {
				if (eaInfos[j]->pPower == pPower) {
					eaPush(&eaInfos[j]->eaItemPowers, pItemPower);
					break;
				}
			}
		}
	}

	// Tally power trees
	for(i=eaSize(&pTrees->ppReferents)-1; i>=0; --i) {
		PowerTreeDef *pTree = pTrees->ppReferents[i];
		for(j=eaSize(&pTree->ppGroups)-1; j>=0; --j) {
			PTGroupDef *pGroup = pTree->ppGroups[j];
			for(k=eaSize(&pGroup->ppNodes)-1; k>=0; --k) {
				PTNodeDef *pNode = pGroup->ppNodes[k];
				for(n=eaSize(&pNode->ppEnhancements)-1; n>=0; --n) {
					PowerDef *pPower = GET_REF(pNode->ppEnhancements[n]->hPowerDef);
					if (pPower) {
						for(m=eaSize(&eaInfos)-1; m>=0; --m) {
							if (eaInfos[m]->pPower == pPower) {
								eaPushUnique(&eaInfos[m]->eaTrees, pTree);
								break;
							}
						}
					}
				}
				for(n=eaSize(&pNode->ppRanks)-1; n>=0; --n) {
					PowerDef *pPower = GET_REF(pNode->ppRanks[n]->hPowerDef);
					if (pPower) {
						for(m=eaSize(&eaInfos)-1; m>=0; --m) {
							if (eaInfos[m]->pPower == pPower) {
								eaPushUnique(&eaInfos[m]->eaTrees, pTree);
								break;
							}
						}
					}
				}
			}

		}
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Powers Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Power Report ----\n");
	fprintf(file, "\nThe following Powers are not referenced by any Power, Critter, ItemPower, Power Tree, or Encounter Layer.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetPowerInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaPowers) + eaSize(&pInfo->eaItemPowers) + eaSize(&pInfo->eaTrees) + eaSize(&pInfo->eaLayers);
		if (count == 0 && !pInfo->bFound) {
			fprintf(file, "  Unused power: %s   (%s)\n", pInfo->pPower->pchName, pInfo->pPower->pchFile);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetPowerInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaCritters);
		eaDestroy(&pInfo->eaPowers);
		eaDestroy(&pInfo->eaItemPowers);
		eaDestroy(&pInfo->eaTrees);
		eaDestroy(&pInfo->eaLayers);
	}
	eaDestroyEx(&eaInfos, NULL);
}

typedef struct AssetPowerArtInfo {
	PowerAnimFX *pPowerAnimFX;

	PowerDef **eaPowers;
} AssetPowerArtInfo;

static void PowersArtAssetReport(FILE *file)
{
	DictionaryEArrayStruct *pPowerArts = resDictGetEArrayStruct("PowerAnimFX");
	DictionaryEArrayStruct *pPowers = resDictGetEArrayStruct("PowerDef");
	AssetPowerArtInfo **eaInfos = NULL;
	int i, j;

	// Create tracking data
	for(i=0; i<eaSize(&pPowerArts->ppReferents); ++i) {
		AssetPowerArtInfo *pInfo = calloc(1, sizeof(AssetPowerArtInfo));
		pInfo->pPowerAnimFX = pPowerArts->ppReferents[i];
		eaPush(&eaInfos, pInfo);
	}

	// Tally powers
	for(i=eaSize(&pPowers->ppReferents)-1; i>=0; --i) {
		PowerDef *pPower = pPowers->ppReferents[i];
		PowerAnimFX *pPowerAnimFX = GET_REF(pPower->hFX);
		if(pPowerAnimFX)
		{
			for(j=eaSize(&eaInfos)-1; j>=0; --j) {
				if (eaInfos[j]->pPowerAnimFX == pPowerAnimFX) {
					eaPush(&eaInfos[j]->eaPowers, pPower);
					break;
				}
			}
		}
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "PowerArts Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused PowerArt Report ----\n");
	fprintf(file, "\nThe following PowerArts are not referenced by any Power.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetPowerArtInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaPowers);
		if (count == 0) {
			fprintf(file, "  Unused powerart: %s\n", pInfo->pPowerAnimFX->cpchFile);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetPowerArtInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaPowers);
	}
	eaDestroyEx(&eaInfos, NULL);
}

typedef struct AssetCritterGroupInfo {
	CritterGroup *pGroup;

	CritterDef **eaCritters;
	EncounterDef **eaEncounters;
	EncounterLayer **eaLayers;
} AssetCritterGroupInfo;

static void CheckCritterGroupsInEncDef(AssetCritterGroupInfo ***peaInfos, EncounterLayer *pLayer, EncounterDef *pDef)
{
	int i,j,n;

	if (pDef) {
		CritterGroup *pGroup = GET_REF(pDef->critterGroup);
		if (pGroup) {
			for(i=eaSize(peaInfos)-1; i>=0; --i) {
				if ((*peaInfos)[i]->pGroup == pGroup) {
					eaPushUnique(&(*peaInfos)[i]->eaLayers, pLayer);
					break;
				}
			}
		}
		for(j=eaSize(&pDef->actors)-1; j>=0; --j) {
			OldActorInfo *pActorInfo = pDef->actors[j]->details.info;
			if (pActorInfo) {
				pGroup = GET_REF(pActorInfo->critterGroup);
				if (pGroup) {
					for(n=eaSize(peaInfos)-1; n>=0; --n) {
						if ((*peaInfos)[n]->pGroup == pGroup) {
							eaPushUnique(&(*peaInfos)[n]->eaLayers, pLayer);
							break;
						}
					}
				}
			}
		}
	}
}

static void CheckCritterGroupsInStaticEnc(AssetCritterGroupInfo ***peaInfos, EncounterLayer *pLayer, OldStaticEncounter *pEnc)
{
	int i;

	if (pEnc) {
		CritterGroup *pGroup = GET_REF(pEnc->encCritterGroup);
		if (pGroup) {
			for(i=eaSize(peaInfos)-1; i>=0; --i) {
				if ((*peaInfos)[i]->pGroup == pGroup) {
					eaPushUnique(&(*peaInfos)[i]->eaLayers, pLayer);
					break;
				}
			}
		}
		if (pEnc->defOverride) {
			CheckCritterGroupsInEncDef(peaInfos, pLayer, pEnc->defOverride);
		}
	}
}

static void CheckCritterGroupsInEncGroup(AssetCritterGroupInfo ***peaInfos, EncounterLayer *pLayer, OldStaticEncounterGroup *pEncGroup)
{
	int i;

	// Recurse
	for(i=eaSize(&pEncGroup->childList)-1; i>=0; --i) {
		CheckCritterGroupsInEncGroup(peaInfos, pLayer, pEncGroup->childList[i]);
	}

	for(i=eaSize(&pEncGroup->staticEncList)-1; i>=0; --i) {
		CheckCritterGroupsInStaticEnc(peaInfos, pLayer, pEncGroup->staticEncList[i]);
	}
}

static void CritterGroupAssetReport(FILE *file, EncounterLayer ***peaLayers)
{
	DictionaryEArrayStruct *pGroups = resDictGetEArrayStruct("CritterGroup");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	AssetCritterGroupInfo **eaInfos = NULL;
	int i, j, n;

	// Create tracking data
	for(i=0; i<eaSize(&pGroups->ppReferents); ++i) {
		AssetCritterGroupInfo *pInfo = calloc(1, sizeof(AssetCritterGroupInfo));
		pInfo->pGroup = pGroups->ppReferents[i];
		eaPush(&eaInfos, pInfo);
	}

	// Tally critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		CritterGroup *pGroup = GET_REF(pCritter->hGroup);
		if (pGroup) {
			for(j=eaSize(&eaInfos)-1; j>=0; --j) {
				if (eaInfos[j]->pGroup == pGroup) {
					eaPush(&eaInfos[j]->eaCritters, pCritter);
					break;
				}
			}
		}
	}

	// Tally encounters
	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pEnc = pEncounters->ppReferents[i];
		CritterGroup *pGroup = GET_REF(pEnc->critterGroup);
		if (pGroup) {
			for(n=eaSize(&eaInfos)-1; n>=0; --n) {
				if (eaInfos[n]->pGroup == pGroup) {
					eaPushUnique(&eaInfos[n]->eaEncounters, pEnc);
					break;
				}
			}
		}

		for(j=eaSize(&pEnc->actors)-1; j>=0; --j) {
			OldActorInfo *pActorInfo = pEnc->actors[j]->details.info;
			if (pActorInfo) {
				pGroup = GET_REF(pActorInfo->critterGroup);
				if (pGroup) {
					for(n=eaSize(&eaInfos)-1; n>=0; --n) {
						if (eaInfos[n]->pGroup == pGroup) {
							eaPushUnique(&eaInfos[n]->eaEncounters, pEnc);
							break;
						}
					}
				}
			}
		}
	}

	// Tally encounter layers
	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaLayers)[i];
		for(j=eaSize(&pLayer->staticEncounters)-1; j>=0; --j) {
			CheckCritterGroupsInStaticEnc(&eaInfos, pLayer, pLayer->staticEncounters[j]);
		}
		CheckCritterGroupsInEncGroup(&eaInfos, pLayer, &pLayer->rootGroup);
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Critter Group Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Critter Group Report ----\n");
	fprintf(file, "\nThe following Critter Groups are not referenced by any Critter, Encounter, or Encounter Layer.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetCritterGroupInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaEncounters) + eaSize(&pInfo->eaLayers);
		if (count == 0) {
			fprintf(file, "  Unused critter group: %s   (%s)\n", pInfo->pGroup->pchName, pInfo->pGroup->pchFileName);
		}
	}

	fprintf(file, "\n");

	fprintf(file, "---- Used Critter Group Report ----\n");
	fprintf(file, "\nThe count indicates how many objects reference each Critter Group.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetCritterGroupInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaEncounters) + eaSize(&pInfo->eaLayers);
		if (count > 0) {
			fprintf(file, "  (%3d) %s   (%s)\n", count, pInfo->pGroup->pchName, pInfo->pGroup->pchFileName);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetCritterGroupInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaCritters);
		eaDestroy(&pInfo->eaEncounters);
		eaDestroy(&pInfo->eaLayers);
	}
	eaDestroyEx(&eaInfos, NULL);
}

typedef struct AssetCritterFactionInfo {
	CritterFaction *pFaction;

	CritterDef **eaCritters;
	EncounterDef **eaEncounters;
	EncounterLayer **eaLayers;
} AssetCritterFactionInfo;

static void CheckCritterFactionsInEncDef(AssetCritterFactionInfo ***peaInfos, EncounterLayer *pLayer, EncounterDef *pDef)
{
	int i,j,n;

	if (pDef) {
		CritterFaction *pFaction = GET_REF(pDef->faction);
		if (pFaction) {
			for(i=eaSize(peaInfos)-1; i>=0; --i) {
				if ((*peaInfos)[i]->pFaction == pFaction) {
					eaPushUnique(&(*peaInfos)[i]->eaLayers, pLayer);
					break;
				}
			}
		}
		for(j=eaSize(&pDef->actors)-1; j>=0; --j) {
			OldActorInfo *pActorInfo = pDef->actors[j]->details.info;
			if (pActorInfo) {
				pFaction = GET_REF(pActorInfo->critterFaction);
				if (pFaction) {
					for(n=eaSize(peaInfos)-1; n>=0; --n) {
						if ((*peaInfos)[n]->pFaction == pFaction) {
							eaPushUnique(&(*peaInfos)[n]->eaLayers, pLayer);
							break;
						}
					}
				}
			}
		}
	}
}

static void CheckCritterFactionsInStaticEnc(AssetCritterFactionInfo ***peaInfos, EncounterLayer *pLayer, OldStaticEncounter *pEnc)
{
	int i;

	if (pEnc) {
		CritterFaction *pFaction = GET_REF(pEnc->encFaction);
		if (pFaction) {
			for(i=eaSize(peaInfos)-1; i>=0; --i) {
				if ((*peaInfos)[i]->pFaction == pFaction) {
					eaPushUnique(&(*peaInfos)[i]->eaLayers, pLayer);
					break;
				}
			}
		}
		if (pEnc->defOverride) {
			CheckCritterFactionsInEncDef(peaInfos, pLayer, pEnc->defOverride);
		}
	}
}

static void CheckCritterFactionsInEncGroup(AssetCritterFactionInfo ***peaInfos, EncounterLayer *pLayer, OldStaticEncounterGroup *pEncGroup)
{
	int i;

	// Recurse
	for(i=eaSize(&pEncGroup->childList)-1; i>=0; --i) {
		CheckCritterFactionsInEncGroup(peaInfos, pLayer, pEncGroup->childList[i]);
	}

	for(i=eaSize(&pEncGroup->staticEncList)-1; i>=0; --i) {
		CheckCritterFactionsInStaticEnc(peaInfos, pLayer, pEncGroup->staticEncList[i]);
	}
}

static void CritterFactionAssetReport(FILE *file, EncounterLayer ***peaLayers)
{
	DictionaryEArrayStruct *pFactions = resDictGetEArrayStruct("CritterFaction");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	AssetCritterFactionInfo **eaInfos = NULL;
	int i, j, n;

	// Create tracking data
	for(i=0; i<eaSize(&pFactions->ppReferents); ++i) {
		AssetCritterFactionInfo *pInfo = calloc(1, sizeof(AssetCritterFactionInfo));
		pInfo->pFaction = pFactions->ppReferents[i];
		eaPush(&eaInfos, pInfo);
	}

	// Tally critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		CritterFaction *pFaction = GET_REF(pCritter->hFaction);
		if (pFaction) {
			for(j=eaSize(&eaInfos)-1; j>=0; --j) {
				if (eaInfos[j]->pFaction == pFaction) {
					eaPush(&eaInfos[j]->eaCritters, pCritter);
					break;
				}
			}
		}
	}

	// Tally encounters
	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pEnc = pEncounters->ppReferents[i];
		CritterFaction *pFaction = GET_REF(pEnc->faction);
		if (pFaction) {
			for(n=eaSize(&eaInfos)-1; n>=0; --n) {
				if (eaInfos[n]->pFaction == pFaction) {
					eaPushUnique(&eaInfos[n]->eaEncounters, pEnc);
					break;
				}
			}
		}

		for(j=eaSize(&pEnc->actors)-1; j>=0; --j) {
			OldActorInfo *pActorInfo = pEnc->actors[j]->details.info;
			if (pActorInfo) {
				pFaction = GET_REF(pActorInfo->critterFaction);
				if (pFaction) {
					for(n=eaSize(&eaInfos)-1; n>=0; --n) {
						if (eaInfos[n]->pFaction == pFaction) {
							eaPushUnique(&eaInfos[n]->eaEncounters, pEnc);
							break;
						}
					}
				}
			}
		}
	}

	// Tally encounter layers
	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaLayers)[i];
		for(j=eaSize(&pLayer->staticEncounters)-1; j>=0; --j) {
			CheckCritterFactionsInStaticEnc(&eaInfos, pLayer, pLayer->staticEncounters[j]);
		}
		CheckCritterFactionsInEncGroup(&eaInfos, pLayer, &pLayer->rootGroup);
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Critter faction Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Critter Faction Report ----\n");
	fprintf(file, "\nThe following Critter Factions are not referenced by any Critter, Encounter, or Encounter Layer.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetCritterFactionInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaEncounters) + eaSize(&pInfo->eaLayers);
		if (count == 0) {
			fprintf(file, "  Unused critter faction: %s   (%s)\n", pInfo->pFaction->pchName, pInfo->pFaction->pchFileName);
		}
	}

	fprintf(file, "\n");

	fprintf(file, "---- Used Critter Faction Report ----\n");
	fprintf(file, "\nThe count indicates how many objects reference each Critter Faction.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetCritterFactionInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaEncounters) + eaSize(&pInfo->eaLayers);
		if (count > 0) {
			fprintf(file, "  (%3d) %s   (%s)\n", count, pInfo->pFaction->pchName, pInfo->pFaction->pchFileName);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetCritterFactionInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaCritters);
		eaDestroy(&pInfo->eaEncounters);
		eaDestroy(&pInfo->eaLayers);
	}
	eaDestroyEx(&eaInfos, NULL);
}

typedef struct AssetCritterInfo {
	CritterDef *pCritter;

	CritterDef **eaCritters;
	EncounterDef **eaEncounters;
	EncounterLayer **eaLayers;
} AssetCritterInfo;

static void CheckCrittersInStaticEnc(AssetCritterInfo ***peaInfos, EncounterLayer *pLayer, OldStaticEncounter *pEnc)
{
	int j,n;

	if (pEnc && pEnc->defOverride) {
		for(j=eaSize(&pEnc->defOverride->actors)-1; j>=0; --j) {
			OldActorInfo *pActorInfo = pEnc->defOverride->actors[j]->details.info;
			if (pActorInfo) {
				CritterDef *pCritter = GET_REF(pActorInfo->critterDef);
				if (pCritter) {
					for(n=eaSize(peaInfos)-1; n>=0; --n) {
						if ((*peaInfos)[n]->pCritter == pCritter) {
							eaPushUnique(&(*peaInfos)[n]->eaLayers, pLayer);
							break;
						}
					}
				}
			}
		}
	}
}

static void CheckCrittersInEncGroup(AssetCritterInfo ***peaInfos, EncounterLayer *pLayer, OldStaticEncounterGroup *pEncGroup)
{
	int i;

	// Recurse
	for(i=eaSize(&pEncGroup->childList)-1; i>=0; --i) {
		CheckCrittersInEncGroup(peaInfos, pLayer, pEncGroup->childList[i]);
	}

	for(i=eaSize(&pEncGroup->staticEncList)-1; i>=0; --i) {
		CheckCrittersInStaticEnc(peaInfos, pLayer, pEncGroup->staticEncList[i]);
	}
}

static void CritterAssetReport(FILE *file, EncounterLayer ***peaLayers)
{
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	AssetCritterInfo **eaInfos = NULL;
	int i, j, n;

	// Create tracking data
	for(i=0; i<eaSize(&pCritters->ppReferents); ++i) {
		AssetCritterInfo *pInfo = calloc(1, sizeof(AssetCritterInfo));
		pInfo->pCritter = pCritters->ppReferents[i];
		eaPush(&eaInfos, pInfo);
	}

	// Tally critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		const char *pcParentName = StructInherit_GetParentName(parse_CritterDef, pCritter);
		if (pcParentName) {
			CritterDef *pParent = RefSystem_ReferentFromString("CritterDef", pcParentName);
			if (pParent) {
				for(j=eaSize(&eaInfos)-1; j>=0; --j) {
					if (eaInfos[j]->pCritter == pParent) {
						eaPush(&eaInfos[j]->eaCritters, pCritter);
						break;
					}
				}
			}
		}
	}

	// Tally encounters
	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pEnc = pEncounters->ppReferents[i];
		for(j=eaSize(&pEnc->actors)-1; j>=0; --j) {
			OldActorInfo *pActorInfo = pEnc->actors[j]->details.info;
			if (pActorInfo) {
				CritterDef *pCritter = GET_REF(pActorInfo->critterDef);
				if (pCritter) {
					for(n=eaSize(&eaInfos)-1; n>=0; --n) {
						if (eaInfos[n]->pCritter == pCritter) {
							eaPushUnique(&eaInfos[n]->eaEncounters, pEnc);
							break;
						}
					}
				}
			}
		}
	}

	// Tally encounter layers
	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaLayers)[i];
		for(j=eaSize(&pLayer->staticEncounters)-1; j>=0; --j) {
			CheckCrittersInStaticEnc(&eaInfos, pLayer, pLayer->staticEncounters[j]);
		}
		CheckCrittersInEncGroup(&eaInfos, pLayer, &pLayer->rootGroup);
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Critter Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Critter Report ----\n");
	fprintf(file, "\nThe following Critters are not referenced by any Critter, Encounter, or EncounterLayer.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetCritterInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaEncounters) + eaSize(&pInfo->eaLayers);
		if (count == 0) {
			fprintf(file, "  Unused critter: %s   (%s)\n", pInfo->pCritter->pchName, pInfo->pCritter->pchFileName);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetCritterInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaCritters);
		eaDestroy(&pInfo->eaEncounters);
		eaDestroy(&pInfo->eaLayers);
	}
	eaDestroyEx(&eaInfos, NULL);
}

typedef struct AssetItemInfo {
	ItemDef *pItem;

	ItemDef **eaItems;
	RewardTable **eaRewards;
	CritterDef **eaCritters;
	bool bFound;
} AssetItemInfo;

static bool CheckItemsInInteractionProps(WorldInteractionProperties* pProps, ItemDef* pTargetDef)
{
	int k, m;
	for(k=eaSize(&pProps->eaEntries)-1; k>=0; --k) {
		WorldInteractionPropertyEntry* pEntry = pProps->eaEntries[k];
		if (pEntry->pActionProperties)
		{
			for(m=eaSize(&pEntry->pActionProperties->successActions.eaActions)-1; m>=0; --m) {
				if (pEntry->pActionProperties->successActions.eaActions[m]->pGiveItemProperties)
				{
					ItemDef* pDef = GET_REF(pEntry->pActionProperties->successActions.eaActions[m]->pGiveItemProperties->hItemDef);
					if (pTargetDef == pDef) {
						return true;
					}
				}
			}
			for(m=eaSize(&pEntry->pActionProperties->failureActions.eaActions)-1; m>=0; --m) {
				if (pEntry->pActionProperties->failureActions.eaActions[m]->pGiveItemProperties)
				{
					ItemDef* pDef = GET_REF(pEntry->pActionProperties->failureActions.eaActions[m]->pGiveItemProperties->hItemDef);
					if (pTargetDef == pDef) {
						return true;
					}
				}
			}
		}
	}
	return false;
}
static void ItemAssetReport(FILE *file, LibFileLoad*** peaGeoLayers)
{
	DictionaryEArrayStruct *pItems = resDictGetEArrayStruct("ItemDef");
	DictionaryEArrayStruct *pRewards = resDictGetEArrayStruct("RewardTable");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pStores = resDictGetEArrayStruct("StoreDef");
	DictionaryEArrayStruct *pMicrotransactions = resDictGetEArrayStruct("MicrotransactionDef");
	AssetItemInfo **eaInfos = NULL;
	int i, j, k, p;

	// Create tracking data
	for(i=0; i<eaSize(&pItems->ppReferents); ++i) {
		AssetItemInfo *pInfo = calloc(1, sizeof(AssetItemInfo));
		pInfo->pItem = pItems->ppReferents[i];
		pInfo->bFound = false;
		eaPush(&eaInfos, pInfo);
	}

	// Tally items
	for(i=eaSize(&pItems->ppReferents)-1; i>=0; --i) {
		ItemDef *pItem = pItems->ppReferents[i];
		ItemDef *pRecipe = GET_REF(pItem->hCraftRecipe);
		if (pRecipe) {
			for(k=eaSize(&eaInfos)-1; k>=0; --k) {
				if (eaInfos[k]->pItem == pRecipe) {
					eaPushUnique(&eaInfos[k]->eaItems, pItem);
					break;
				}
			}
		}
		if (pItem->pCraft) {
			for(j=eaSize(&pItem->pCraft->ppPart)-1; j>=0; --j) {
				ItemDef *pComponent = GET_REF(pItem->pCraft->ppPart[j]->hItem);
				if (pComponent) {
					for(k=eaSize(&eaInfos)-1; k>=0; --k) {
						if (eaInfos[k]->pItem == pComponent) {
							eaPushUnique(&eaInfos[k]->eaItems, pItem);
							break;
						}
					}
				}
			}
		}
	}

	// Tally Reward Tables
	for(i=eaSize(&pRewards->ppReferents)-1; i>=0; --i) {
		RewardTable *pReward = pRewards->ppReferents[i];
		for(j=eaSize(&pReward->ppRewardEntry)-1; j>=0; --j) {
			RewardEntry *pEntry = pReward->ppRewardEntry[j];
			ItemDef *pItem = GET_REF(pEntry->hItemDef);
			if (pItem) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pItem == pItem) {
						eaPushUnique(&eaInfos[k]->eaRewards, pReward);
						break;
					}
				}
			}
		}
	}

	// Tally Stores
	for(i=eaSize(&pStores->ppReferents)-1; i>=0; --i) {
		StoreDef *pStore = pStores->ppReferents[i];
		for(j=eaSize(&pStore->inventory)-1; j>=0; --j) {
			StoreItemDef *pItemForSale = pStore->inventory[j];
			ItemDef *pItem = GET_REF(pItemForSale->hItem);
			if (pItem) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pItem == pItem) {
						eaInfos[k]->bFound = true;
						break;
					}
				}
			}
		}
	}

	// Tally Microtransactions
	for(i=eaSize(&pMicrotransactions->ppReferents)-1; i>=0; --i) {
		MicroTransactionDef *pMicrotrans = pMicrotransactions->ppReferents[i];
		for(j=eaSize(&pMicrotrans->eaParts)-1; j>=0; --j) {
			MicroTransactionPart *pPart = pMicrotrans->eaParts[j];
			ItemDef *pItem = GET_REF(pPart->hItemDef);
			if (pItem) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pItem == pItem) {
						eaInfos[k]->bFound = true;
						break;
					}
				}
			}
		}
	}

	// Tally Geo Layers
	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad* pLibFile = (*peaGeoLayers)[i];
		for(j=eaSize(&pLibFile->defs)-1; j>=0; --j) {
			if (pLibFile->defs[j]->property_structs.interaction_properties)
			{
				for(p=eaSize(&eaInfos)-1; p>=0; --p) {
					if (CheckItemsInInteractionProps(pLibFile->defs[j]->property_structs.interaction_properties, eaInfos[p]->pItem)) {
						eaInfos[p]->bFound = true;
						break;
					}
				}
			}
			if (pLibFile->defs[j]->property_structs.server_volume.interaction_volume_properties)
			{
				for(p=eaSize(&eaInfos)-1; p>=0; --p) {
					if (CheckItemsInInteractionProps(pLibFile->defs[j]->property_structs.server_volume.interaction_volume_properties, eaInfos[p]->pItem)) {
						eaInfos[p]->bFound = true;
						break;
					}
				}
			}
		}
	}

	// Tally Critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		for(j=eaSize(&pCritter->ppCritterItems)-1; j>=0; --j) {
			DefaultItemDef *pDef = pCritter->ppCritterItems[j];
			ItemDef *pItem = pDef ? GET_REF(pDef->hItem) : NULL;
			if (pItem) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pItem == pItem) {
						eaPushUnique(&eaInfos[k]->eaCritters, pCritter);
						break;
					}
				}
			}
		}
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Item Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Item Report ----\n");
	fprintf(file, "\nThe following Items are not referenced by any Item, Reward Table, or CritterDef.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetItemInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaItems) + eaSize(&pInfo->eaRewards) + eaSize(&pInfo->eaCritters);
		if (count == 0 && !pInfo->bFound) {
			fprintf(file, "  Unused item: %s   (%s)\n", pInfo->pItem->pchName, pInfo->pItem->pchFileName);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetItemInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaItems);
		eaDestroy(&pInfo->eaRewards);
		eaDestroy(&pInfo->eaCritters);
	}
	eaDestroyEx(&eaInfos, NULL);
}

typedef struct AssetItemPowerInfo {
	ItemPowerDef *pItemPower;

	ItemDef **eaItems;
	RewardTable **eaRewards;
} AssetItemPowerInfo;

static void ItemPowerAssetReport(FILE *file)
{
	DictionaryEArrayStruct *pItemPowers = resDictGetEArrayStruct("ItemPowerDef");
	DictionaryEArrayStruct *pItems = resDictGetEArrayStruct("ItemDef");
	DictionaryEArrayStruct *pRewards = resDictGetEArrayStruct("RewardTable");
	AssetItemPowerInfo **eaInfos = NULL;
	int i, j, k;

	// Create tracking data
	for(i=0; i<eaSize(&pItemPowers->ppReferents); ++i) {
		AssetItemPowerInfo *pInfo = calloc(1, sizeof(AssetItemPowerInfo));
		pInfo->pItemPower = pItemPowers->ppReferents[i];
		eaPush(&eaInfos, pInfo);
	}

	// Tally items
	for(i=eaSize(&pItems->ppReferents)-1; i>=0; --i) {
		ItemDef *pItem = pItems->ppReferents[i];
		for(j=eaSize(&pItem->ppItemPowerDefRefs)-1; j>=0; --j) {
			ItemPowerDef *pItemPower = GET_REF(pItem->ppItemPowerDefRefs[j]->hItemPowerDef);
			if (pItemPower) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pItemPower == pItemPower) {
						eaPush(&eaInfos[k]->eaItems, pItem);
						break;
					}
				}
			}
		}
	}

	// Tally Reward Tables
	for(i=eaSize(&pRewards->ppReferents)-1; i>=0; --i) {
		RewardTable *pReward = pRewards->ppReferents[i];
		for(j=eaSize(&pReward->ppRewardEntry)-1; j>=0; --j) {
			RewardEntry *pEntry = pReward->ppRewardEntry[j];
			ItemPowerDef *pItemPower = GET_REF(pEntry->hItemPowerDef);
			if (pItemPower) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pItemPower == pItemPower) {
						eaPushUnique(&eaInfos[k]->eaRewards, pReward);
						break;
					}
				}
			}
		}
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Item Power Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Item Power Report ----\n");
	fprintf(file, "\nThe following Item Powers are not referenced by any Item or Reward Table.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetItemPowerInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaItems) + eaSize(&pInfo->eaRewards);
		if (count == 0) {
			fprintf(file, "  Unused item power: %s   (%s)\n", pInfo->pItemPower->pchName, pInfo->pItemPower->pchFileName);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetItemPowerInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaItems);
		eaDestroy(&pInfo->eaRewards);
	}
	eaDestroyEx(&eaInfos, NULL);
}


typedef struct AssetRewardTableInfo {
	RewardTable *pReward;

	MissionDef **eaMissions;
	RewardTable **eaRewards;
	CritterDef **eaCritters;
	EncounterLayer **eaLayers;
	bool bFound;
} AssetRewardTableInfo;


static bool MissionUsesRewardTable(MissionDef* pMission, RewardTable* pTable)
{
	int j;
	if (pMission->params) {

		for(j=eaSize(&pMission->params->missionDrops)-1; j>=0; --j) {
			MissionDrop *pDrop = pMission->params->missionDrops[j];
			if (pDrop->RewardTableName) {
				if (stricmp(pDrop->RewardTableName, pTable->pchName) == 0) {
					return true;
				}
			}
		}

		if (pMission->params->OnfailureRewardTableName) {
			if (stricmp(pMission->params->OnfailureRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
		if (pMission->params->OnreturnRewardTableName) {
			if (stricmp(pMission->params->OnreturnRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
		if (pMission->params->OnReplayReturnRewardTableName) {
			if (stricmp(pMission->params->OnReplayReturnRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
		if (pMission->params->ActivityReturnRewardTableName) {
			if (stricmp(pMission->params->ActivityReturnRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
		if (pMission->params->OnstartRewardTableName) {
			if (stricmp(pMission->params->OnstartRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
		if (pMission->params->OnsuccessRewardTableName) {
			if (stricmp(pMission->params->OnsuccessRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
		if (pMission->params->ActivitySuccessRewardTableName) {
			if (stricmp(pMission->params->ActivitySuccessRewardTableName, pTable->pchName) == 0) {
				return true;
			}
		}
	}
	for(j=eaSize(&pMission->subMissions)-1; j>=0; --j) {
		if (MissionUsesRewardTable(pMission->subMissions[j], pTable))
			return true;
	}
	return false;
}
static void RewardTableAssetReport(FILE *file, EncounterLayer ***peaLayers, LibFileLoad*** peaGeoLayers)
{
	DictionaryEArrayStruct *pRewardTables = resDictGetEArrayStruct("RewardTable");
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pZoneMaps = resDictGetEArrayStruct("ZoneMap");
	DictionaryEArrayStruct *pQueues = resDictGetEArrayStruct("QueueDef");
	DictionaryEArrayStruct *pStores = resDictGetEArrayStruct("StoreDef");
	DictionaryEArrayStruct *pMicrotransactions = resDictGetEArrayStruct("MicrotransactionDef");
	AssetRewardTableInfo **eaInfos = NULL;
	int i, j, k, m;

	// Create tracking data
	for(i=0; i<eaSize(&pRewardTables->ppReferents); ++i) {
		AssetRewardTableInfo *pInfo = calloc(1, sizeof(AssetRewardTableInfo));
		pInfo->pReward = pRewardTables->ppReferents[i];
		pInfo->bFound = false;
		eaPush(&eaInfos, pInfo);
	}

	// Tally missions
	for(i=eaSize(&pMissions->ppReferents)-1; i>=0; --i) {
		MissionDef *pMission = pMissions->ppReferents[i];
		for(k=eaSize(&eaInfos)-1; k>=0; --k) {
			if (MissionUsesRewardTable(pMission, eaInfos[k]->pReward))
			{
				eaPushUnique(&eaInfos[k]->eaMissions, pMission);
				eaInfos[k]->bFound = true;
				break;
			}
		}
	}

	// Tally Critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		RewardTable *pTargetReward = GET_REF(pCritter->hRewardTable);
		if (pTargetReward) {
			for(k=eaSize(&eaInfos)-1; k>=0; --k) {
				if (eaInfos[k]->pReward == pTargetReward) {
					eaInfos[k]->bFound = true;
					eaPushUnique(&eaInfos[k]->eaCritters, pCritter);
					break;
				}
			}
		}
	}

	// Tally Reward Tables
	for(i=eaSize(&pRewardTables->ppReferents)-1; i>=0; --i) {
		RewardTable *pReward = pRewardTables->ppReferents[i];
		for(j=eaSize(&pReward->ppRewardEntry)-1; j>=0; --j) {
			RewardEntry *pEntry = pReward->ppRewardEntry[j];
			RewardTable *pTargetReward = GET_REF(pEntry->hRewardTable);
			if (pTargetReward) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pReward == pTargetReward) {
						eaInfos[k]->bFound = true;
						eaPushUnique(&eaInfos[k]->eaRewards, pReward);
						break;
					}
				}
			}
			if (pEntry->ChoiceType == kRewardChoiceType_CharacterBasedInclude)
			{
				char** eaNames = NULL;
				int iResult;
				rewardentry_GetAllPossibleCharBasedTableNames(pEntry, &eaNames);
				for(iResult=eaSize(&eaNames)-1; iResult>=0; --iResult) {
					for(k=eaSize(&eaInfos)-1; k>=0; --k) {
						if (stricmp(eaInfos[k]->pReward->pchName, eaNames[iResult]) == 0) {
							eaInfos[k]->bFound = true;
							eaPushUnique(&eaInfos[k]->eaRewards, pReward);
							break;
						}
					}
				}
				eaDestroyEx(&eaNames, NULL);
			}
		}
	}

	// Tally Queues
	for(i=eaSize(&pQueues->ppReferents)-1; i>=0; --i) {
		QueueDef *pQueue = pQueues->ppReferents[i];
		for(j=eaSize(&pQueue->eaRewardTables)-1; j>=0; --j) {
			RewardTable *pTargetReward = GET_REF(pQueue->eaRewardTables[j]->hRewardTable);
			if (pTargetReward) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pReward == pTargetReward) {
						eaInfos[k]->bFound = true;
						break;
					}
				}
			}
		}
	}


	// Tally Stores
	for(i=eaSize(&pStores->ppReferents)-1; i>=0; --i) {
		StoreDef *pStore = pStores->ppReferents[i];
		for(j=eaSize(&pStore->eaRestockDefs)-1; j>=0; --j) {
			StoreRestockDef *pRestock = pStore->eaRestockDefs[j];
			for(k=eaSize(&eaInfos)-1; k>=0; --k) {
				if (eaInfos[k]->pReward == GET_REF(pRestock->hRewardTable)) {
					eaInfos[k]->bFound = true;
					break;
				}
			}
		}
	}

	// Tally Microtransactions
	for(i=eaSize(&pMicrotransactions->ppReferents)-1; i>=0; --i) {
		MicroTransactionDef *pMicrotrans = pMicrotransactions->ppReferents[i];
		for(j=eaSize(&pMicrotrans->eaParts)-1; j>=0; --j) {
			MicroTransactionPart *pPart = pMicrotrans->eaParts[j];
			RewardTable *pTable = GET_REF(pPart->hRewardTable);
			if (pTable) {
				for(k=eaSize(&eaInfos)-1; k>=0; --k) {
					if (eaInfos[k]->pReward == pTable) {
						eaInfos[k]->bFound = true;
						break;
					}
				}
			}
		}
	}

	// Tally Zonemaps
	for(i=eaSize(&pZoneMaps->ppReferents)-1; i>=0; --i) {
		ZoneMap *pMap = pZoneMaps->ppReferents[i];
		ZoneMapInfo *pMapInfo = &pMap->map_info;

		RewardTable *pTargetReward = GET_REF(pMapInfo->player_reward_table);
		for(k=eaSize(&eaInfos)-1; k>=0; --k) {
			if (eaInfos[k]->pReward == pTargetReward) {
				eaInfos[k]->bFound = true;
				break;
			}
		}

		pTargetReward = GET_REF(pMapInfo->reward_table);
		for(k=eaSize(&eaInfos)-1; k>=0; --k) {
			if (eaInfos[k]->pReward == pTargetReward) {
				eaInfos[k]->bFound = true;
				break;
			}
		}
	}

	// TODO: Tally Geo Layers
	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad* pLibFile = (*peaGeoLayers)[i];
		for(j=eaSize(&pLibFile->defs)-1; j>=0; --j) {
			if (pLibFile->defs[j]->property_structs.interaction_properties)
			{
				for(k=eaSize(&pLibFile->defs[j]->property_structs.interaction_properties->eaEntries)-1; k>=0; --k) {
					WorldInteractionPropertyEntry* pEntry = pLibFile->defs[j]->property_structs.interaction_properties->eaEntries[k];
					if (pEntry->pRewardProperties)
					{
						RewardTable *pTargetReward = GET_REF(pEntry->pRewardProperties->hRewardTable);
						for(m=eaSize(&eaInfos)-1; m>=0; --m) {
							if (eaInfos[m]->pReward == pTargetReward) {
								eaInfos[m]->bFound = true;
								break;
							}
						}
					}
				}
			}
		}
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "Reward Table Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");
	fprintf(file, "---- Unused Reward Table Report ----\n");
	fprintf(file, "\nThe following Reward Tables are not referenced by any Mission, Critter, Enc Layer, or Reward Table.\n\n");

	for(i=0; i<eaSize(&eaInfos); ++i) {
		AssetRewardTableInfo *pInfo = eaInfos[i];
		int count = eaSize(&pInfo->eaMissions) + eaSize(&pInfo->eaRewards) + eaSize(&pInfo->eaCritters) + eaSize(&pInfo->eaLayers);
		if (count == 0 && !pInfo->bFound) {
			fprintf(file, "  Unused reward table: %s   (%s)\n", pInfo->pReward->pchName, pInfo->pReward->pchFileName);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	for(i=eaSize(&eaInfos)-1; i>=0; --i) {
		AssetRewardTableInfo *pInfo = eaInfos[i];
		eaDestroy(&pInfo->eaMissions);
		eaDestroy(&pInfo->eaRewards);
		eaDestroy(&pInfo->eaCritters);
		eaDestroy(&pInfo->eaLayers);
	}
	eaDestroyEx(&eaInfos, NULL);
}


static void ScanStaticGroupForUsedDefs(EncounterLayer *pLayer, OldStaticEncounterGroup *pGroup, EncounterDef ***peaUsedDefs)
{
	int i;

	// Encounters at this level
	for(i=eaSize(&pGroup->staticEncList)-1; i>=0; --i) {
		if (GET_REF(pGroup->staticEncList[i]->baseDef)) {
			eaPushUnique(peaUsedDefs, GET_REF(pGroup->staticEncList[i]->baseDef));
		}
	}
	// Recurse
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		ScanStaticGroupForUsedDefs(pLayer, pGroup->childList[i], peaUsedDefs);
	}
}

static void EncounterDefAssetReport(FILE *file, EncounterLayer ***peaLayers)
{
	DictionaryEArrayStruct *pEncDefs = resDictGetEArrayStruct("EncounterDef");
	EncounterDef **eaUsedDefs = NULL;
	int i;

	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaLayers)[i];
		ScanStaticGroupForUsedDefs(pLayer, &pLayer->rootGroup, &eaUsedDefs);
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "EncounterDef Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	fprintf(file, "---- Unused Encounter Def Report ----\n");
	fprintf(file, "\nThe following Encounter Defs are not referenced by any Encounter.\n\n");

	for(i=0; i<eaSize(&pEncDefs->ppReferents); ++i) {
		EncounterDef *pDef = pEncDefs->ppReferents[i];
		if (eaFind(&eaUsedDefs, pDef) < 0) {
			fprintf(file, "  Unused Encounter Def: %s   (%s)\n", pDef->name, pDef->filename);
		}
	}

	fprintf(file, "\n");

	// Cleanup
	eaDestroy(&eaUsedDefs);
}


//typedef struct ActorExprInfo {
//	EncounterLayer *pLayer;
//	StaticEncounterGroup *pGroup;
//	EncounterDef *pDef;
//	Actor *pActor;
//	ContactDef *pContact;
//	char *pcExpr;
//} ActorExprInfo;

static void ScanEncounterDefForUsedContacts(EncounterLayer *pLayer, OldStaticEncounterGroup *pGroup, EncounterDef *pDef, ContactDef ***peaUsedDefs)
{
	int i;

	for(i=eaSize(&pDef->actors)-1; i>=0; --i) {
		OldActor *pActor = pDef->actors[i];
		if (pActor->details.info) {
			if (GET_REF(pActor->details.info->contactScript)) {
				eaPushUnique(peaUsedDefs, GET_REF(pActor->details.info->contactScript));
			}
			//if (pActor->details.info->interactProps.interactCond) {
			//	char *pText = exprGetCompleteString(pActor->details.info->interactProps.interactCond);
			//	if (pText) {
			//		ActorExprInfo *pInfo = calloc(1, sizeof(ActorExprInfo));
			//		pInfo->pLayer = pLayer;
			//		pInfo->pGroup = pGroup;
			//		pInfo->pDef = pDef;
			//		pInfo->pActor = pActor;
			//		pInfo->pContact = GET_REF(pActor->details.info->contactScript);
			//		pInfo->pcExpr = strdup(pText);
			//		eaPush(peaActorExprs, pInfo);
			//	}
			//}
		}
	}
}


static void ScanStaticGroupForUsedContacts(EncounterLayer *pLayer, OldStaticEncounterGroup *pGroup, ContactDef ***peaUsedDefs)
{
	int i;

	// Encounters at this level
	for(i=eaSize(&pGroup->staticEncList)-1; i>=0; --i) {
		if (pGroup->staticEncList[i]->defOverride) {
			ScanEncounterDefForUsedContacts(pLayer, pGroup, pGroup->staticEncList[i]->defOverride, peaUsedDefs);
		}
	}
	// Recurse
	for(i=eaSize(&pGroup->childList)-1; i>=0; --i) {
		ScanStaticGroupForUsedContacts(pLayer, pGroup->childList[i], peaUsedDefs);
	}
}


static void ContactAssetReport(FILE *file, EncounterLayer ***peaLayers)
{
	DictionaryEArrayStruct *pContactDefs = resDictGetEArrayStruct("Contact");
	DictionaryEArrayStruct *pEncDefs = resDictGetEArrayStruct("EncounterDef");
	ContactDef **eaUsedDefs = NULL;
	//ActorExprInfo **eaActorExprs = NULL;
	//ContactDef **eaExprContacts = NULL;
	int i;

	//// Scan contacts
	//for(i=eaSize(&pContactDefs->ppReferents)-1; i>=0; --i) {
	//	ContactDef *pContactDef = pContactDefs->ppReferents[i];
	//	if (pContactDef->interactReqs) {
	//		char *pText = exprGetCompleteString(pContactDef->interactReqs);
	//		if (pText) {
	//			eaPush(&eaExprContacts, pContactDef);
	//		}
	//	}
	//}

	// Scan encounters
	for(i=eaSize(&pEncDefs->ppReferents)-1; i>=0; --i) {
		EncounterDef *pEncDef = pEncDefs->ppReferents[i];
		ScanEncounterDefForUsedContacts(NULL, NULL, pEncDef, &eaUsedDefs);
	}

	// Scan layers
	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		EncounterLayer *pLayer = (*peaLayers)[i];
		ScanStaticGroupForUsedContacts(pLayer, &pLayer->rootGroup, &eaUsedDefs);
	}

	// Report
	fprintf(file, "==============================================================\n");
	fprintf(file, "ContactDef Information\n");
	fprintf(file, "==============================================================\n");
	fprintf(file, "\n");

	fprintf(file, "---- Unused Contact Def Report ----\n");
	fprintf(file, "\nThe following Contact Defs are not referenced by any Encounter Def or Encounter Layer.\n\n");

	for(i=0; i<eaSize(&pContactDefs->ppReferents); ++i) {
		ContactDef *pDef = pContactDefs->ppReferents[i];
		if (eaFind(&eaUsedDefs, pDef) < 0) {
			fprintf(file, "  Unused Contact Def: %s   (%s)\n", pDef->name, pDef->filename);
		}
	}

	fprintf(file, "\n");

	//fprintf(file, "---- Contact Expression Report ----\n");
	//fprintf(file, "%d contact interact expressions\n", eaSize(&eaExprContacts));
	//fprintf(file, "%d actor interact expressions\n", eaSize(&eaActorExprs));
	//fprintf(file, "\n");

	//for(i=0; i<eaSize(&eaExprContacts); ++i) {
	//	ContactDef *pContact = eaExprContacts[i];
	//	fprintf(file, "  Contact File: %s\n", pContact->filename);
	//	fprintf(file, "  Contact Expr: %s\n", exprGetCompleteString(pContact->interactReqs));
	//	fprintf(file, "\n");
	//}
	//for(i=0; i<eaSize(&eaActorExprs); ++i) {
	//	ActorExprInfo *pInfo = eaActorExprs[i];
	//	if (pInfo->pLayer) {
	//		fprintf(file, "  Actor Layer File: %s\n", pInfo->pLayer->filename);
	//		fprintf(file, "  Actor Group/Def/Name: %s/%s/%s\n", pInfo->pGroup->groupName, pInfo->pDef->name, pInfo->pActor->name);
	//	} else {
	//		fprintf(file, "  Actor EncDef File: %s\n", pInfo->pDef->filename);
	//		fprintf(file, "  Actor Name: %s\n", pInfo->pActor->name);
	//	}
	//	fprintf(file, "  Actor Expr: %s\n", pInfo->pcExpr);
	//	if (pInfo->pContact) {
	//		char *pText = exprGetCompleteString(pInfo->pContact->interactReqs);
	//		fprintf(file, "  Contact File: %s\n", pInfo->pContact->filename);
	//		if (pText) {
	//			fprintf(file, "  Contact Expr: %s\n", pText);
	//		}
	//	}
	//	fprintf(file, "\n");
	//}

	fprintf(file, "\n");

	// Cleanup
	eaDestroy(&eaUsedDefs);
}


AUTO_COMMAND ACMD_SERVERCMD;
char *ReportSystemAssets(void)
{
	LibFileLoad** eaGeoLayers = NULL;
	EncounterLayer **eaLayers = NULL;
	FILE *file;

	file = fopen("c:\\SystemAssetReport.txt", "w");

	AssetLoadEncLayers(&eaLayers);
	AssetLoadGeoLayers(&eaGeoLayers);

	PowersAssetReport(file, &eaLayers);
	PowersArtAssetReport(file);
	CritterGroupAssetReport(file, &eaLayers);
	CritterFactionAssetReport(file, &eaLayers);
	CritterAssetReport(file, &eaLayers);
	ItemAssetReport(file, &eaGeoLayers);
	ItemPowerAssetReport(file);
	RewardTableAssetReport(file, &eaLayers, &eaGeoLayers);
	// TODO: Power Trees
	// TODO: Critter Overrides
	EncounterDefAssetReport(file, &eaLayers);
	ContactAssetReport(file, &eaLayers);

	AssetCleanupEncLayers(&eaLayers);
	AssetCleanupGeoLayers(&eaGeoLayers);

	fclose(file);
	return "Game asset report written to 'c:\\SystemAssetReport.txt'";
}

// ---------------------------------------------------------------------
//  Item Reports
// ---------------------------------------------------------------------

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportMissionItemsByType(void)
{
	DictionaryEArrayStruct *pItems = resDictGetEArrayStruct("ItemDef");
	int i, iItemType;
	FILE *file;

	file = fopen("c:\\MissionItemsByType.txt", "w");
	fprintf(file, "This is a list of Mission Items sorted by Item Type\n");
	fprintf(file, "Generate by typing 'ReportMissionItemsByType'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	for (iItemType = 0; iItemType < kItemType_None; iItemType++){
		bool bFound = false;

		for(i=0; i < eaSize(&pItems->ppReferents); i++)
		{
			ItemDef *pDef = pItems->ppReferents[i];

			if (pDef->eType == iItemType && IS_HANDLE_ACTIVE(pDef->hMission)){
				if (!bFound){
					bFound = true;
					fprintf(file, "Item Type %s\n", StaticDefineIntRevLookup(ItemTypeEnum, iItemType));
					fprintf(file, "----------------------------------------------------------\n");
				}
				fprintf(file, "%-40s %s\n", pDef->pchName, REF_STRING_FROM_HANDLE(pDef->hMission));
			}
		}
		if (bFound)
			fprintf(file, "\n\n");
	}

	fclose(file);
	return "List of Mission Items written to 'c:\\MissionItemsByType.txt'";
}

// ---------------------------------------------------------------------
// Mission Reports
// ---------------------------------------------------------------------

// This is a report that finds suspicious non-player-scoped Events on Missions

static void FindSuspiciousMissionDefsRecursive(MissionDef ***pSuspiciousDefs, MissionDef *pMission)
{
	int i;
	if (missiondef_GetType(pMission) == MissionType_OpenMission){
		// For Open Missions, Scoreboard events should be player-scoped, check those
		// The regular Events should NOT be player-scoped, so don't check those
		for (i=eaSize(&pMission->eaOpenMissionScoreEvents)-1; i>=0; --i)
		{
			GameEvent *ev = pMission->eaOpenMissionScoreEvents[i]->pEvent;
			if (ev && !(ev->tMatchSource == TriState_Yes || ev->tMatchSourceTeam == TriState_Yes || ev->tMatchTarget == TriState_Yes || ev->tMatchTargetTeam == TriState_Yes))
			{
				eaPushUnique(pSuspiciousDefs, pMission);
				break;
			}
		}
	} else {
		// For regular Missions, the TrackedEvents should be player- or team-scoped
		for (i=eaSize(&pMission->eaTrackedEvents)-1; i>=0; --i)
		{
			GameEvent *ev = pMission->eaTrackedEvents[i];
			if (!(ev->tMatchSource == TriState_Yes || ev->tMatchSourceTeam == TriState_Yes || ev->tMatchTarget == TriState_Yes || ev->tMatchTargetTeam == TriState_Yes))
			{
				eaPushUnique(pSuspiciousDefs, pMission);
				break;
			}
		}
	}

	for (i = 0; i < eaSize(&pMission->subMissions); i++)
	{
		FindSuspiciousMissionDefsRecursive(pSuspiciousDefs, pMission->subMissions[i]);
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportSuspiciousMissionEvents(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	MissionDef **ppSuspiciousDefs = NULL;
	int i;
	FILE *file;

	file = fopen("c:\\SuspiciousMissionEventList.txt", "w");
	fprintf(file, "This is a list of Missions with non-player-scoped Events.\n");
	fprintf(file, "Generate by typing 'ReportSuspiciousMissionEvents'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	// Find all suspicious Missions
	for(i=0; i < eaSize(&pMissions->ppReferents); i++)
	{
		FindSuspiciousMissionDefsRecursive(&ppSuspiciousDefs, pMissions->ppReferents[i]);
	}

	// Write all suspicious Missions to file
	for (i = 0; i < eaSize(&ppSuspiciousDefs); i++)
	{
		fprintf(file, "%s\n", ppSuspiciousDefs[i]->pchRefString);
	}

	eaDestroy(&ppSuspiciousDefs);
	fclose(file);
	return "List of Missions with suspect Events written to 'c:\\SuspiciousMissionEventList.txt'";
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportSuspectedMissionItems(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	ItemDef **ppSuspectedMissionItems = NULL;
	int i;
	FILE *file;

	file = fopen("c:\\SuspectedMissionItemList.txt", "w");
	fprintf(file, "This is a list of Items that are referenced by a Mission, but are not\n");
	fprintf(file, "Mission Items belonging to that Mission.\n");
	fprintf(file, "Generate by typing 'ReportSuspectedMissionItems'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	// Find all suspicious Missions
	for(i=0; i < eaSize(&pMissions->ppReferents); i++)
	{
		MissionDef *pDef = pMissions->ppReferents[i];
		int j;
		missiondef_fixup_FindItemsForMissionRecursive(pDef, &ppSuspectedMissionItems);

		// Write all suspicious Items to file
		for (j = 0; j < eaSize(&ppSuspectedMissionItems); j++)
		{
			ItemDef *pItemDef = ppSuspectedMissionItems[j];
			if (!(GET_REF(pItemDef->hMission) == pDef))
			{
				fprintf(file, "%s: %s\n", pDef->name, pItemDef->pchName);
			}
		}

		eaClear(&ppSuspectedMissionItems);
	}

	eaDestroy(&ppSuspectedMissionItems);
	fclose(file);
	return "List of suspected Mission Items written to 'c:\\SuspectedMissionItemList.txt'";
}

// Report of all Missions that can fail
static bool MissionDefHasFailureCondRecursive(MissionDef *pMission)
{
	int i;
	if (pMission->meFailureCond)
		return true;

	for (i = 0; i < eaSize(&pMission->subMissions); i++){
		if (MissionDefHasFailureCondRecursive(pMission->subMissions[i]))
			return true;
	}
	return false;
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportFailableMissions(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	MissionDef **ppDefs = NULL;
	int i;
	FILE *file;

	file = fopen("c:\\FailableMissionList.txt", "w");
	fprintf(file, "This is a list of Missions that have a Failure condition.\n");
	fprintf(file, "Generate by typing 'ReportFailableMissions'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	// Find all failable Missions
	for(i=0; i < eaSize(&pMissions->ppReferents); i++)
	{
		if (MissionDefHasFailureCondRecursive(pMissions->ppReferents[i]))
			eaPush(&ppDefs, pMissions->ppReferents[i]);
	}

	// Write all suspicious Missions to file
	for (i = 0; i < eaSize(&ppDefs); i++)
	{
		fprintf(file, "%s\n", ppDefs[i]->pchRefString);
	}

	eaDestroy(&ppDefs);
	fclose(file);
	return "List of Failable Missions written to 'c:\\FailableMissionList.txt'";
}

bool MissionHasDiscoverCondition(MissionDef *pMission)
{
	if(pMission && pMission->pDiscoverCond)
	{
		S32 i;
		
		for(i = 0; i < eaSize(&pMission->pDiscoverCond->lines); ++i)
		{
			ExprLine *pExprLine = pMission->pDiscoverCond->lines[i];		
			if(pExprLine && pExprLine->origStr && strlen(pExprLine->origStr) > 1)
			{
				return true;
			}
		}
	}
	
	return false;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7);
char* ReportPerkDiscoverableMissions(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	MissionDef **ppDefs = NULL;
	int i;
	FILE *file;
	U32 uCount = 0;

	file = fopen("c:\\PerkDiscoverableMissionList.txt", "w");
	fprintf(file, "This is a list of Missions that are a perk and can be discoverd condition without an event count.\n");
	fprintf(file, "Generate by typing 'ReportPerkDiscoverableMissions'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	// Find all perk mission with discover expression Missions
	for(i=0; i < eaSize(&pMissions->ppReferents); i++)
	{
		MissionDef *pMission = pMissions->ppReferents[i];
		if(pMission && pMission->missionType == MissionType_Perk && MissionHasDiscoverCondition(pMission))
		{
			if(eaSize(&pMission->eaTrackedEvents) < 1)
			{
				eaPush(&ppDefs, pMissions->ppReferents[i]);
				++uCount;
			}
		}
	}

	// Write all qualifying Missions to file
	for (i = 0; i < eaSize(&ppDefs); i++)
	{
		fprintf(file, "%s\n", ppDefs[i]->pchRefString);
	}
	
	fprintf(file, "\nCount = %d\n\n", uCount);

	eaClear(&ppDefs);
	uCount = 0;

	fprintf(file, "This is a list of Missions that are perk and can be discoverd condition that have a eventtracker.\n");
	fprintf(file, "----------------------------------------------------------\n");

	// Find all perk mission with discover expression Missions that are not player stat count
	for(i=0; i < eaSize(&pMissions->ppReferents); i++)
	{
		MissionDef *pMission = pMissions->ppReferents[i];
		if(pMission && pMission->missionType == MissionType_Perk && MissionHasDiscoverCondition(pMission))
		{
			if(eaSize(&pMission->eaTrackedEvents) > 0)
			{
				eaPush(&ppDefs, pMissions->ppReferents[i]);
				++uCount;
			}
		}
	}

	// Write all qualifying Missions to file
	for (i = 0; i < eaSize(&ppDefs); i++)
	{
		fprintf(file, "%s\n", ppDefs[i]->pchRefString);
	}
	
	fprintf(file, "\nCount = %d\n\n", uCount);

	eaDestroy(&ppDefs);
	fclose(file);
	return "List of perk discover Missions written to 'c:\\PerkDiscoverableMissionList.txt'";
}

AUTO_COMMAND;
void ListSuspiciousMissionRewards(void)
{
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	FILE *outFile=NULL;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);

	outFile = fopen("suspiciousMissionRewards.txt", "w");

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		// Perks should rarely or never have mission rewards
		if(pDef->missionType == MissionType_Perk && pDef->params && pDef->params->NumericRewardScale)
		{
			printf("%s is a perk but has a numeric reward scale of %f\n", pDef->name, pDef->params->NumericRewardScale);
			fprintf(outFile, "%s\tis a perk but has a numeric reward scale of %f\n", pDef->name, pDef->params->NumericRewardScale);
		}
		else if(pDef->missionType != MissionType_Perk && pDef->params && pDef->params->NumericRewardScale == 0)
		{
			printf("%s has a numeric reward scale of zero\n", pDef->name);
			fprintf(outFile, "%s\thas a numeric reward scale of zero\n", pDef->name);
		}
		else if(pDef->params && pDef->params->NumericRewardScale > 10)
		{
			printf("%s has a very large numeric reward scale: %f\n", pDef->name, pDef->params->NumericRewardScale);
			fprintf(outFile, "%s\thas a very large numeric reward scale: %f\n", pDef->name, pDef->params->NumericRewardScale);
		}
	}
	if(outFile)
		fclose(outFile);
}

// List missions with rewards that don't fit Champions's model.  These are less suspicious than
// the missions in ListSuspiciousMissionRewards, but may still help us catch some missions that
// would otherwise slip through the cracks
AUTO_COMMAND;
void ListSuspiciousChampionsMissionRewards(void)
{
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	FILE *outFile=NULL;

	outFile = fopen("suspiciousChampsMissionRewards.txt", "w");

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		// Perks should rarely or never have mission rewards
		if(pDef->params && pDef->missionType != MissionType_Perk
			&& pDef->params->NumericRewardScale != 0 && !(pDef->params->NumericRewardScale > 0.099 && pDef->params->NumericRewardScale < 0.101)
			&& pDef->params->NumericRewardScale != 0.25 && pDef->params->NumericRewardScale != 1 && pDef->params->NumericRewardScale != 2)
		{
			printf("%s has an unusual numeric reward scale: %f\n", pDef->name, pDef->params->NumericRewardScale);
			fprintf(outFile, "%s has an unusual numeric reward scale: %f\n", pDef->name, pDef->params->NumericRewardScale);
		}
	}
	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		if(pDef->params && pDef->missionType == MissionType_OpenMission && (!pDef->params->pchGoldRewardTable || !pDef->params->pchSilverRewardTable || !pDef->params->pchBronzeRewardTable || !pDef->params->pchDefaultRewardTable))
		{
			printf("%s is an open mission with no rewards.\n", pDef->name);
			fprintf(outFile, "%s is an open mission with no rewards.\n", pDef->name);
		}
	}
	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		if(pDef->params && pDef->missionType == MissionType_Normal && pDef->params->NumericRewardScale > .99 && pDef->needsReturn && !pDef->params->OnreturnRewardTableName && !pDef->params->OnReplayReturnRewardTableName && !pDef->params->ActivityReturnRewardTableName)
		{
			printf("%s is a mission with a numeric reward scale but no OnReturn, OnReplayReturn, or ActivityReturn reward table.\n", pDef->name);
			fprintf(outFile, "%s is a mission with a numeric reward scale but no OnReturn or OnReplayReturn reward table.\n", pDef->name);
		}
	}

		// Perks should usually be given at the player level
	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);
	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		if(pDef->params && pDef->missionType == MissionType_Perk && pDef->levelDef.eLevelType != MissionLevelType_PlayerLevel)
		{
			printf("%s is a perk that doesn't use the player level.\n", pDef->name);
			fprintf(outFile, "%s is a perk that doesn't use the player level.\n", pDef->name);
		}
	}

	if(outFile)
		fclose(outFile);
}

AUTO_COMMAND;
void ListSuspiciousMissionClickableOverrides(void)
{
	RefDictIterator iterator = {0};
	MissionDef *pDef = NULL;
	FILE *outFile=NULL;
	int i, n;

	RefSystem_InitRefDictIterator(g_MissionDictionary, &iterator);

	outFile = fopen("suspiciousMissionClickableOverrides.txt", "w");

	while (pDef = RefSystem_GetNextReferentFromIterator(&iterator))
	{
		n = eaSize(&pDef->ppInteractableOverrides);
		for(i=0; i<n; i++)
		{
			InteractableOverride* pOverride = pDef->ppInteractableOverrides[i];
			if(pOverride->pPropertyEntry->pTimeProperties && pOverride->pPropertyEntry->pTimeProperties->bNoRespawn)
			{
				printf("Interactable Override %s on mission %s is flagged Do Not Respawn\n", pOverride->pcInteractableName, pDef->name);
				fprintf(outFile, "Interactable Override %s on mission %s is flagged Do Not Respawn\n", pOverride->pcInteractableName, pDef->name);
			}
		}
	}
	if(outFile)
		fclose(outFile);
}

// This reports any Missions that don't have a Category attached
AUTO_COMMAND ACMD_SERVERCMD;
char* ReportUncategorizedMissions(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	MissionDef **ppSuspiciousDefs = NULL;
	int i;
	FILE *file;

	file = fopen("c:\\UncategorizedMissionList.txt", "w");
	fprintf(file, "This is a list of non-invisible Missions with no Category attached.\n");
	fprintf(file, "Generate by typing 'ReportUncategorizedMissions'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	// Find all suspicious Missions
	for(i=0; i < eaSize(&pMissions->ppReferents); i++)
	{
		MissionDef *pDef = (MissionDef*)pMissions->ppReferents[i];
		if (!GET_REF(pDef->hCategory) && missiondef_HasDisplayName(pDef))
			eaPush(&ppSuspiciousDefs, pDef);
	}

	// Write all suspicious Missions to file
	for (i = 0; i < eaSize(&ppSuspiciousDefs); i++)
	{
		fprintf(file, "%s\n", ppSuspiciousDefs[i]->pchRefString);
	}

	eaDestroy(&ppSuspiciousDefs);
	fclose(file);
	return "List of Missions without Categories written to 'c:\\UncategorizedMissionList.txt'";
}


// --- Report to find dependencies between Missions ---

typedef struct MissionDepTracker MissionDepTracker;

typedef struct MissionDepTracker{
	const char *pchMissionName;						// POOL_STRING

	MissionDepTracker** eaParents;					// UNOWNED
	MissionDepTracker** eaChildren;					// UNOWNED

} MissionDepTracker;

void FreeMissionDepTracker(MissionDepTracker *pDep)
{
	eaDestroy(&pDep->eaParents);
	eaDestroy(&pDep->eaChildren);
	free(pDep);
}

MissionDepTracker* MissionDepTrackerFindOrCreate(MissionDepTracker*** peaDeps, const char *pchPooledMissionName)
{
	MissionDepTracker *pDep = NULL;
	int i;
	for (i = eaSize(peaDeps)-1; i>=0; --i){
		if ((*peaDeps)[i]->pchMissionName == pchPooledMissionName){
			return (*peaDeps)[i];
		}
	}
	pDep = calloc(1, sizeof(MissionDepTracker));
	pDep->pchMissionName = pchPooledMissionName;
	eaPush(peaDeps, pDep);
	return pDep;
}

void MissionDepTrackerPrintRecursive(FILE *file, MissionDepTracker *pDep, int iIndentLevel)
{
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pDep->pchMissionName);
	int i;
	for (i = 0; i < iIndentLevel; i++){
		fprintf(file, "  ");
	}
	if (pDep){
		if (pDef->eaObjectiveMaps && eaSize(&pDef->eaObjectiveMaps)){
			fprintf(file, "%s  (%s)\n", pDef->name, pDef->eaObjectiveMaps[0]->pchMapName);
		} else {
			fprintf(file, "%s\n", pDef->name);
		}
	} else {
		fprintf(file, "UNKNOWN (%s)\n", pDep->pchMissionName);
	}

	for (i = 0; i < eaSize(&pDep->eaChildren); i++){
		MissionDepTrackerPrintRecursive(file, pDep->eaChildren[i], iIndentLevel+1);
	}
}

AUTO_COMMAND;
char* ReportMissionDependencies(void)
{
	MissionDepTracker** eaAllDeps = NULL;
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	int i;
	FILE *file;

	file = fopen("c:\\MissionDependencyList.txt", "w");
	fprintf(file, "This is a report of all Mission dependency chains.\n");
	fprintf(file, "Generate by typing 'ReportMissionDependencies'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	for (i = eaSize(&pStruct->ppReferents)-1; i>=0; --i){
		MissionDef *pDef = pStruct->ppReferents[i];
		MissionDepTracker *pDep = MissionDepTrackerFindOrCreate(&eaAllDeps, pDef->name);

		if (pDef && pDef->missionReqs){
			char *pchExpression = strdup(exprGetCompleteString(pDef->missionReqs));
			if (pchExpression) {
				char *pchNext = pchExpression;
				char *pchEnd;
				ANALYSIS_ASSUME(pchExpression);
				while (pchNext){
					MissionDef *pDependency = NULL;
					pchNext = strstri(pchNext, "HasCompletedMission(");
					if (!pchNext)
					{
						break;
					}
					ANALYSIS_ASSUME(pchNext != NULL);
					pchNext += strlen("HasCompletedMission(");
					while (pchNext && (*pchNext == '"' || *pchNext == ' ')){
						pchNext++;
					}
					ANALYSIS_ASSUME(pchNext != NULL); // this one is somewhat suspect
					pchEnd = strchr(pchNext, ')');
					if (pchEnd){
						while (pchEnd && pchEnd >= pchNext && (*pchEnd == ')' || *pchEnd == '"' || *pchEnd == ' ')){
							--pchEnd;
						}
						pchEnd++;
						*pchEnd = '\0';

						pDependency = RefSystem_ReferentFromString(g_MissionDictionary, pchNext);
						if (pDependency){
							MissionDepTracker *pDep2 = MissionDepTrackerFindOrCreate(&eaAllDeps, pDependency->name);
							eaPush(&pDep->eaParents, pDep2);
							eaPush(&pDep2->eaChildren, pDep);
						} else {
							fprintf(file, "Error: Mission %s appears to reference invalid mission %s", pDef->name, pchNext);
						}
					}
				}

				free(pchExpression);
			}
		}
	}

	// All dependencies have been found; now print results
	for (i = 0; i < eaSize(&eaAllDeps); i++){
		// only print root missions with children for now
		MissionDepTracker *pDep = eaAllDeps[i];
		if (eaSize(&pDep->eaChildren) && !eaSize(&pDep->eaParents)){
			fprintf(file, "\n--------------------------------\n");
			MissionDepTrackerPrintRecursive(file, pDep, 1);
		}
	}

	eaDestroyEx(&eaAllDeps, FreeMissionDepTracker);
	fclose(file);
	return "List of Mission dependency chains written to 'c:\\MissionDependencyList.txt'";
}


// ---------------------------------------------------------------------
// Encounter Layer Reports
// ---------------------------------------------------------------------

#ifdef SDANGELO_FIX_TO_SCAN_GEO_LAYERS

// This reports all Clickables that have a Direct reward table (which is unsafe)
static bool ClickableCheckDirectReward(ClickableObject *pClickable, EncounterLayer *pLayer, const char *pchPathString, FILE *file)
{
	RewardTable *pTable = GET_REF(pClickable->hRewardTable);
	if (pTable && rewardTable_HasItemsWithType(pTable, kRewardPickupType_Direct))
	{
		fprintf(file, "%s:  Clickable %s references non-numeric Direct reward table %s.\n", pLayer->pchFilename, pClickable->name, REF_STRING_FROM_HANDLE(pClickable->hRewardTable));
		return true;
	}
	return false;
}

AUTO_ COMMAND ACMD_ SERVERCMD;
char* ReportClickableDirectRewards(void)
{
	EncounterLayer **ppEncounterLayers = NULL;
	FILE *file;
	int i, n;

	AssetLoadEncLayers(&ppEncounterLayers);
	file = fopen("c:\\ClickablesWithDirectRewards.txt", "w");

	n = eaSize(&ppEncounterLayers);
	for (i = 0; i < n; i++)
	{
		ParserScanForSubstruct(parse_EncounterLayer, ppEncounterLayers[i], parse_ClickableObject, 0, 0, ClickableCheckDirectReward, file);
	}

	AssetCleanupEncLayers(&ppEncounterLayers);

	fclose(file);
	return "List of invalid Clickables written to 'c:\\ClickablesWithDirectRewards.txt'";
}

#endif

#define MAX_ACTOR_DIST_SQ 10000
// Make sure the actors aren't too far from the encounter center
static bool CheckDistanceFromEncounterCenter(OldStaticEncounter *pData, EncounterLayer *pLayer, const char *pcPathString, FILE* outFile)
{
	EncounterDef* def = NULL;
	Vec3 encCenter;
	int i,n,total = 0;

	// Generate the encounter's spawn rule
	oldencounter_UpdateStaticEncounterSpawnRule(pData, pLayer);

	def = pData->spawnRule;
	zeroVec3(encCenter);

	// Find the center of the actors and points
	n = eaSize(&def->actors);
	for(i=0; i<n; i++)
	{
		addVec3(encCenter, def->actors[i]->details.position->posOffset, encCenter);
	}
	total += n;

	n = eaSize(&def->namedPoints);
	for(i=0; i<n; i++)
	{
		addVec3(encCenter, def->namedPoints[i]->relLocation[3], encCenter);
	}
	total += n;

	scaleVec3(encCenter, 1.0 / total, encCenter);

	// encCenter is the center of the encounter.  Make sure no points are too far from it
	n = eaSize(&def->actors);
	for(i=0; i<n; i++)
	{
		if(distance3Squared(def->actors[i]->details.position->posOffset, encCenter) > MAX_ACTOR_DIST_SQ)
		{
			printf("Actor %s (%d) is too far from the center of encounter %s on layer %s\n", def->actors[i]->name, def->actors[i]->uniqueID, def->name, pLayer->name);
			if(outFile)
				fprintf(outFile, "Actor %s (%d) is too far from the center of encounter %s on layer %s\n", def->actors[i]->name, def->actors[i]->uniqueID, def->name, pLayer->name);
		}
	}

	n = eaSize(&def->namedPoints);
	for(i=0; i<n; i++)
	{
		if(distance3Squared(def->namedPoints[i]->relLocation[3], encCenter) > MAX_ACTOR_DIST_SQ)
		{
			printf("Named point %s is too far from the center of encounter %s on layer %s\n", def->namedPoints[i]->pointName, def->name, pLayer->name);
			if(outFile)
				fprintf(outFile, "Named point %s is too far from the center of encounter %s on layer %s\n", def->namedPoints[i]->pointName, def->name, pLayer->name);
		}
	}

	return true;
}

// Report actors that are far from their encounters
AUTO_COMMAND ACMD_SERVERCMD;
void ReportSuspiciousActorLocs(void)
{
	EncounterLayer **eaLayers = NULL;
	int i, n;
	FILE *outFile=NULL;

	outFile = fopen("suspiciousActorLocations.txt", "w");

	AssetLoadEncLayers(&eaLayers);

	n = eaSize(&eaLayers);
	for (i = 0; i < n; i++)
	{
		EncounterLayer *pLayer = eaLayers[i];

		ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_OldStaticEncounter, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, CheckDistanceFromEncounterCenter, outFile);
	}

	AssetCleanupEncLayers(&eaLayers);

	if(outFile)
		fclose(outFile);
}


static bool FindMissingBaseActors(OldStaticEncounter *pData, EncounterLayer *pLayer, const char *pcPathString, FILE* outFile)
{
	EncounterDef* defOverride = NULL;
	EncounterDef* baseDef = NULL;
	int i,n;

	defOverride = pData->defOverride;
	baseDef = GET_REF(pData->baseDef);

	if(!defOverride)
		return true;

	n = eaSize(&defOverride->actors);
	for(i=0; i<n; i++)
	{
		OldActor* pActor = defOverride->actors[i];

		// IDs >= 0 are based on actors in the base def.
		if(pActor->uniqueID >= 0)
		{
			// Is there a corresponding basedef actor?
			if(!baseDef)
			{
				printf("Base def is missing, but actor %d in static encounter %s on layer %s tries to override an actor in it\n", pActor->uniqueID, pData->name, pLayer->name);
				if(outFile)
					fprintf(outFile, "Base def is missing, but actor %d in static encounter %s on layer %s tries to override an actor in it\n", pActor->uniqueID, pData->name, pLayer->name);
			}
			else if(!oldencounter_FindDefActorByID(baseDef, pActor->uniqueID, false))
			{
				// Only actors that have info, AI Info, and a position will actually be spawned
				if (pActor->details.info && pActor->details.aiInfo
					&& pActor->details.position)
				{
					printf("Actor %d in static encounter %s on layer %s has no corresponding actor in the base def\n", pActor->uniqueID, pData->name, pLayer->name);
					if(outFile)
						fprintf(outFile, "Actor %d in static encounter %s on layer %s has no corresponding actor in the base def\n", pActor->uniqueID, pData->name, pLayer->name);
				}
				// else this is excess data; there's some data for this actor, but it's never spawned
			}
		} 
	}
	// It's hard to tell which named points "should" have overrides, and generally less important if their base is missing.
	// Ignore them for now.
	return true;
}

// Find actors that are overridden in a static encounter but don't have corresponding actors in the base def.
AUTO_COMMAND ACMD_SERVERCMD;
void ReportMissingBaseActors(void)
{
	EncounterLayer **eaLayers = NULL;
	int i, n;
	FILE *outFile=NULL;

	outFile = fopen("missingBaseActors.txt", "w");

	AssetLoadEncLayers(&eaLayers);

	n = eaSize(&eaLayers);
	for (i = 0; i < n; i++)
	{
		EncounterLayer *pLayer = eaLayers[i];

		ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_OldStaticEncounter, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, FindMissingBaseActors, outFile);
	}

	AssetCleanupEncLayers(&eaLayers);

	if(outFile)
		fclose(outFile);
}

// Actually, this function checks for mismatched subranks, not ranks.
static bool FindMismatchedActorSubranks(OldStaticEncounter *pData, EncounterLayer *pLayer, const char *pcPathString, FILE* outFile)
{
	EncounterDef* def = NULL;
	int i,n;
	const char *pcSubRank = NULL;

	// Generate the encounter's spawn rule
	oldencounter_UpdateStaticEncounterSpawnRule(pData, pLayer);

	def = pData->spawnRule;

	n = eaSize(&def->actors);
	for(i=0; i<n; i++)
	{
		OldActorInfo* pActorInfo = def->actors[i]->details.info;

		if(pcSubRank == NULL)
		{
			pcSubRank = pActorInfo->pcCritterSubRank;
		}
		else
		{
			if(pcSubRank != pActorInfo->pcCritterSubRank)
			{
				printf("Encounter %s on layer %s: Actor %d's subrank is %s, but another actor in the encounter has subrank %s\n", def->name, pLayer->name, def->actors[i]->uniqueID, pActorInfo->pcCritterSubRank, pcSubRank);
				if(outFile)
					fprintf(outFile, "Encounter %s on layer %s: Actor %d's subrank is %s, but another actor in the encounter has subrank %s\n", def->name, pLayer->name, def->actors[i]->uniqueID, pActorInfo->pcCritterSubRank, pcSubRank);

				// We're done with this encounter, so that we don't spam the console with error messages if the first subrank is the one that's wrong
				return true;
			}
		}
	}
	return true;
}

// Find encounters that have actors of different ranks (weak/tough).
AUTO_COMMAND ACMD_SERVERCMD;
void ReportMismatchedActorRanks(void)
{
	EncounterLayer **eaLayers = NULL;
	int i, n;
	FILE *outFile=NULL;

	outFile = fopen("mismatchedRanks.txt", "w");

	AssetLoadEncLayers(&eaLayers);

	n = eaSize(&eaLayers);
	for (i = 0; i < n; i++)
	{
		EncounterLayer *pLayer = eaLayers[i];

		ParserScanForSubstruct(parse_EncounterLayer, pLayer, parse_OldStaticEncounter, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE, FindMismatchedActorSubranks, outFile);
	}

	AssetCleanupEncLayers(&eaLayers);

	if(outFile)
		fclose(outFile);
}


// ----------------------------------------------------------------------------
// Message memory usage reporting
// ----------------------------------------------------------------------------


typedef struct MessageFileInfo {
	const char *pcFilename;
	int count;
	int size;
	int otherSize;
	int keySize;
} MessageFileInfo;


static int reportMessagesSort(const MessageFileInfo** left, const MessageFileInfo** right)
{
	if ((*left)->pcFilename && (*right)->pcFilename) {
		return stricmp((*left)->pcFilename,(*right)->pcFilename);
	} else if ((*left)->pcFilename) {
		return -1;
	} else {
		return 1;
	}
}


AUTO_COMMAND;
void ReportMessages(void) 
{
	RefDictIterator iter;
	Message *pMessage;
	MessageFileInfo **eaFileInfo = NULL;
	MessageFileInfo **eaDirInfo = NULL;
	char buf[260];
	char *ptr;
	int i,j;
	int count = 0;
	int totalSize = 0;
	int totalOtherSize = 0;
	int totalKeySize = 0;

	RefSystem_InitRefDictIterator("Message", &iter);
	while(pMessage = RefSystem_GetNextReferentFromIterator(&iter)) {
		bool bFound = false;

		// Count
		++count;

		// Record filename
		for(j=eaSize(&eaFileInfo)-1; j>=0; --j) {
			if (pMessage->pcFilename == eaFileInfo[j]->pcFilename) {
				++eaFileInfo[j]->count;
				eaFileInfo[j]->size += (int)(pMessage->pcDefaultString ? strlen(pMessage->pcDefaultString)+1 : 0);
				eaFileInfo[j]->keySize += (int)(pMessage->pcMessageKey ? strlen(pMessage->pcMessageKey)+1 : 0);
				eaFileInfo[j]->otherSize += (int)(pMessage->pcDescription ? strlen(pMessage->pcDescription)+1 : 0) + (int)(pMessage->pcScope ? strlen(pMessage->pcScope)+1 : 0); 
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			MessageFileInfo *pInfo = calloc(1, sizeof(MessageFileInfo));
			pInfo->pcFilename = pMessage->pcFilename;
			pInfo->count = 1;
			pInfo->size = (int)(pMessage->pcDefaultString ? strlen(pMessage->pcDefaultString)+1 : 0);
			pInfo->keySize = (int)(pMessage->pcMessageKey ? strlen(pMessage->pcMessageKey)+1 : 0);
			pInfo->otherSize = (int)(pMessage->pcDescription ? strlen(pMessage->pcDescription)+1 : 0) + (int)(pMessage->pcScope ? strlen(pMessage->pcScope)+1 : 0); 
			eaPush(&eaFileInfo, pInfo);
		}

		// Record directory
		buf[0] = '\0';
		if (pMessage->pcFilename) {
			ptr = strchr(pMessage->pcFilename, '/');
			if (ptr) {
				ptr = strchr(ptr+1, '/');
			}
			if (ptr) {
				strncpy(buf, pMessage->pcFilename, ptr - pMessage->pcFilename);
				buf[ptr - pMessage->pcFilename] = '\0';
			}
		}
		bFound = false;
		for(j=eaSize(&eaDirInfo)-1; j>=0; --j) {
			if (stricmp(buf, eaDirInfo[j]->pcFilename) == 0) {
				++eaDirInfo[j]->count;
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			MessageFileInfo *pInfo = calloc(1, sizeof(MessageFileInfo));
			pInfo->pcFilename = allocAddString(buf);
			pInfo->count = 1;
			eaPush(&eaDirInfo, pInfo);
		}
	}

	eaQSort(eaFileInfo, reportMessagesSort);
	eaQSort(eaDirInfo, reportMessagesSort);

	printf("--start----------------------------------------------\n");
	printf("FILES\n");
	for(i=eaSize(&eaFileInfo)-1; i>=0; --i) {
		printf("  %d %s\n", eaFileInfo[i]->count, eaFileInfo[i]->pcFilename);
		totalSize += eaFileInfo[i]->size;
		totalKeySize += eaFileInfo[i]->keySize;
		totalOtherSize += eaFileInfo[i]->otherSize;
	}

	printf("DIRS\n");
	for(i=eaSize(&eaDirInfo)-1; i>=0; --i) {
		printf("  %d %s\n", eaDirInfo[i]->count, eaDirInfo[i]->pcFilename);
	}

	printf("%d files\n", eaSize(&eaFileInfo));
	printf("%d dirs\n", eaSize(&eaDirInfo));
	printf("%d messages\n", count);
	printf("%d size\n", totalSize);
	printf("%d keySize\n", totalKeySize);
	printf("%d otherSize\n", totalOtherSize);
	printf("--end------------------------------------------------\n");

	// Cleanup
	for(i=eaSize(&eaFileInfo)-1; i>=0; --i) {
		free(eaFileInfo[i]);
	}
	eaDestroy(&eaFileInfo);
	for(i=eaSize(&eaDirInfo)-1; i>=0; --i) {
		free(eaDirInfo[i]);
	}
	eaDestroy(&eaDirInfo);
}

// ----------------------------------------------------------------------------
// ZoneMap reports
// ----------------------------------------------------------------------------

AUTO_COMMAND;
char* ReportMapDisplayNames(void)
{
	int len = 0;
	FILE *file;
	RefDictIterator iter;
	ZoneMapInfo *zminfo;

	file = fopen("c:\\MapDisplayNameList.txt", "w");
	fprintf(file, "This is a list of all maps with Public Names and their corresponding Display Names.\n");
	fprintf(file, "Generate by typing 'ReportMapDisplayNames'.\n");
	fprintf(file, "----------------------------------------------------------\n");

	worldGetZoneMapIterator(&iter);
	while (zminfo = worldGetNextZoneMap(&iter))
	{
		const char *pchPublicName = zmapInfoGetPublicName(zminfo);
		Message *pDisplayNameMsg = zmapInfoGetDisplayNameMessagePtr(zminfo);
		if (pDisplayNameMsg){
			printf("%s ", pchPublicName);
			fprintf(file, "%s ", pchPublicName);
			len = (int)strlen(pchPublicName);
			while (len++ < 40){
				printf(" ");
				fprintf(file, " ");
			}
			printf("%s\n", pDisplayNameMsg->pcDefaultString);
			fprintf(file, "%s\n", pDisplayNameMsg->pcDefaultString);
		} else {
			printf("%s\n", pchPublicName);
			fprintf(file, "%s\n", pchPublicName);
		}
	}
	fclose(file);
	return "List of Map Names written server console, and to 'c:\\MapDisplayNameList.txt'";
}

// ----------------------------------------------------------------------------
// Message reports
// ----------------------------------------------------------------------------
AUTO_COMMAND ACMD_SERVERCMD;
char* ReportSuspiciousMessages(void)
{
	FILE* file;

	file = fopen( "c:/MessageList.txt", "w" );
	
	fprintf( file, "LIST OF ALL BAD MESSAGES:\n" );
	fprintf( file, "KEY,FILENAME,SCOPE,DESCRIPTION,RESPONSIBLE USER\n" );

	FOR_EACH_IN_REFDICT(gMessageDict, Message, msg) {
		if (!msg->pcDefaultString || !msg->pcDescription || !msg->pcMessageKey || !msg->pcScope)
		{
			char messageInfoBuffer[1024];

			sprintf( messageInfoBuffer, "%s,%s,%s,%s,%s",
					 msg->pcMessageKey,
					 msg->pcFilename,
					 msg->pcScope,
					 msg->pcDescription,
					 gimmeDLLQueryLastAuthor( msg->pcFilename ));

			fprintf( file, "%s\n", messageInfoBuffer );
			printf( "%s\n", messageInfoBuffer );
		}
	} FOR_EACH_END;

	fprintf( file, "END OF LIST\n" );
	fclose(file);
	return "List of incomplete messages written to server console and c:\\MessageList.txt";
}

// ----------------------------------------------------------------------------
// Text dumps
// ----------------------------------------------------------------------------

// Helper function
void report_DumpMissionOfferText(ContactDef* pContactDef, ContactMissionOffer* pOffer, FILE *file)
{
	MissionDef* pMissionDef = GET_REF(pOffer->missionDef);
	Message* pMessage;

	if(pMissionDef)
	{
		fprintf(file, "  Mission Offer: %s\n", pMissionDef->name);

		pMessage = GET_REF(pMissionDef->displayNameMsg.hMessage);
		fprintf(file, "    Display Name: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = GET_REF(pMissionDef->uiStringMsg.hMessage);
		fprintf(file, "    UI String: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = GET_REF(pMissionDef->summaryMsg.hMessage);
		fprintf(file, "    Summary: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = GET_REF(pMissionDef->detailStringMsg.hMessage);
		fprintf(file, "    Detail: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = GET_REF(pMissionDef->failureMsg.hMessage);
		if(pMessage)
			fprintf(file, "    Failure: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = GET_REF(pMissionDef->msgReturnStringMsg.hMessage);
		fprintf(file, "    Return: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = GET_REF(pMissionDef->failReturnMsg.hMessage);
		fprintf(file, "    Failed Return: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");


		if(pOffer->allowGrantOrReturn==ContactMissionAllow_GrantAndReturn || pOffer->allowGrantOrReturn==ContactMissionAllow_GrantOnly)
		{
			pMessage = eaSize(&pOffer->offerDialog) ? GET_REF(pOffer->offerDialog[0]->displayTextMesg.hMessage) : NULL;
			if(pMessage)
				fprintf(file, "    Contact Offer: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");
			pMessage = eaSize(&pOffer->failureDialog) ? GET_REF(pOffer->failureDialog[0]->displayTextMesg.hMessage) : NULL;
			if(pMessage)
				fprintf(file, "    Contact Failure: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");
		}
		pMessage = eaSize(&pOffer->inProgressDialog) ? GET_REF(pOffer->inProgressDialog[0]->displayTextMesg.hMessage) : NULL;
		fprintf(file, "    Contact In Progress: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		if(pOffer->allowGrantOrReturn==ContactMissionAllow_GrantAndReturn || pOffer->allowGrantOrReturn==ContactMissionAllow_ReturnOnly)
		{
			pMessage = eaSize(&pOffer->completedDialog) ? GET_REF(pOffer->completedDialog[0]->displayTextMesg.hMessage) : NULL;
			fprintf(file, "    Contact Completed: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");
		}

		fprintf(file, "\n");
	}
}

// This dumps all message text from missions and their contacts to a file so that
// our writer can view all of a mission's text together
AUTO_COMMAND ACMD_SERVERCMD;
char* ReportDumpMissionTextFiltered(const char* nameFilter, const char* scopeFilter)
{
	DictionaryEArrayStruct *pContactDefs = resDictGetEArrayStruct("Contact");
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	MissionDef **ppSuspiciousDefs = NULL;
	int i;
	FILE *file;

	file = fopen("c:\\MissionTextDump.txt", "w");
	fprintf(file, "This is a list of all Contacts and their mission text.\n");
	fprintf(file, "   (No guarantee that any given contact is actually used.)\n");
	fprintf(file, "Generate by typing 'ReportDumpMissionText'.\n");
	fprintf(file, "----------------------------------------------------------\n\n");
	if(nameFilter)
		fprintf(file, "Name Filter: %s\n", nameFilter);
	if(scopeFilter)
		fprintf(file, "Scope Filter: %s\n", scopeFilter);


	// Find all contacts
	for(i=0; i < eaSize(&pContactDefs->ppReferents); i++)
	{
		int j,k;
		ContactDef *pDef = (ContactDef*)pContactDefs->ppReferents[i];
		Message* pMessage;

		// Filter contacts; if this contact's name doesn't match the substring, skip it
		if(nameFilter)
		{
			if(!(pDef->name && strstri_safe(pDef->name, nameFilter)))
			{
				continue;
			}
		}
		if(scopeFilter)
		{
			if(!(pDef->scope && strstri_safe(pDef->scope, scopeFilter)))
			{
				continue;
			}
		}

		// Dump text on contact
		fprintf(file, "%s\n", pDef->name);
		pMessage = GET_REF(pDef->displayNameMsg.hMessage);
		fprintf(file, "Display name: %s\n", pMessage ? pMessage->pcDefaultString : "*UNKNOWN*");

		pMessage = eaSize(&pDef->greetingDialog) ? GET_REF(pDef->greetingDialog[0]->displayTextMesg.hMessage) : NULL;
		if(pMessage)
			fprintf(file, "Greeting: %s\n", pMessage ? pMessage->pcDefaultString : "*NONE*");

		pMessage = eaSize(&pDef->missionListDialog) ? GET_REF(pDef->missionListDialog[0]->displayTextMesg.hMessage) : NULL;
		if(pMessage)
			fprintf(file, "Mission List: %s\n", pMessage ? pMessage->pcDefaultString : "*NONE*");

		pMessage = eaSize(&pDef->noMissionsDialog) ? GET_REF(pDef->noMissionsDialog[0]->displayTextMesg.hMessage) : NULL;
		if(pMessage)
			fprintf(file, "No Missions: %s\n", pMessage ? pMessage->pcDefaultString : "*NONE*");

		pMessage = eaSize(&pDef->exitDialog) ? GET_REF(pDef->exitDialog[0]->displayTextMesg.hMessage) : NULL;
		if(pMessage)
			fprintf(file, "Farewell: %s\n", pMessage ? pMessage->pcDefaultString : "*NONE*");


		for(j=0; j<eaSize(&pDef->offerList); j++)
		{
			report_DumpMissionOfferText(pDef, pDef->offerList[j], file);
		}

		for(k=0; k<eaSize(&pDef->specialDialog); k++)
		{
			pMessage = GET_REF(pDef->specialDialog[k]->displayNameMesg.hMessage);
			fprintf(file, "Special Dialog Name: %s\n", pMessage ? pMessage->pcDefaultString : "*MISSING*");

			pMessage = pDef->specialDialog[k]->dialogBlock[0] ? GET_REF(pDef->specialDialog[k]->dialogBlock[0]->displayTextMesg.hMessage) : NULL;
			fprintf(file, "Special Dialog: %s\n", pMessage ? pMessage->pcDefaultString : "*MISSING*");

			fprintf(file, "\n");
		}
		fprintf(file, "\n");
	}

	fclose(file);
	return "Mission and contact text dumped to 'c:\\MissionTextDump.txt'";
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportDumpMissionText(const char* nameFilter, const char* scopeFilter)
{
	return ReportDumpMissionTextFiltered(NULL, NULL);
}


// This dumps all message text from missions and their contacts to a file so that
// our writer can view all of a mission's text together
AUTO_COMMAND ACMD_SERVERCMD;
char* ReportMissionsPerLevel(void)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	int iLevel, i, iMaxLevel = 0;
	FILE *file;

	file = fopen("c:\\MissionsPerLevel.txt", "w");
	fprintf(file, "This is a list of the number of Missions in the game at each level.\n");
	fprintf(file, "This doesn't guarantee that the mission is actually in the world somewhere,\n");
	fprintf(file, "or that it's available for all players.\n");
	fprintf(file, "Generate by typing 'ReportMissionsPerLevel'.\n");
	fprintf(file, "----------------------------------------------------------\n\n");

	// Loop through once to find the max level
	for (i = 0; i < eaSize(&pMissions->ppReferents); i++){
		MissionDef *pDef = (MissionDef*)pMissions->ppReferents[i];
		if (missiondef_GetType(pDef) == MissionType_Normal || missiondef_GetType(pDef) == MissionType_AutoAvailable){
			iMaxLevel = MAX(iMaxLevel, pDef->levelDef.missionLevel);
		}
	}

	fprintf(file, "level\tnum\ttotal rewards\n\n");
	for (iLevel = 1; iLevel < iMaxLevel; iLevel++){
		int iNumMissionsAtLevel = 0;
		F32 fTotalRewardScaleAtLevel = 0.f;
		for(i=0; i < eaSize(&pMissions->ppReferents); i++){
			MissionDef *pDef = (MissionDef*)pMissions->ppReferents[i];
			if ((missiondef_GetType(pDef) == MissionType_Normal || missiondef_GetType(pDef) == MissionType_AutoAvailable)&& pDef->levelDef.missionLevel == iLevel && pDef->levelDef.eLevelType == MissionLevelType_Specified){
				iNumMissionsAtLevel++;
				if (pDef->params){
					fTotalRewardScaleAtLevel += pDef->params->NumericRewardScale;
				}
			}
		}
		fprintf(file, "%d:\t%d\t%.2f\n", iLevel, iNumMissionsAtLevel, fTotalRewardScaleAtLevel);
	}

	fclose(file);
	return "Missions per level dumped to 'c:\\MissionsPerLevel.txt'";
}

static void PrintMissionNames(FILE *file, MissionDef*** peaMissionDefs)
{
	int i;
	for (i = 0; i < eaSize(peaMissionDefs); i++){
		fprintf(file, "     %s\n", (*peaMissionDefs)[i]->name);
	}
}

// This dumps a report of how many missions are shareable, and why other missions aren't shareable
AUTO_COMMAND ACMD_SERVERCMD;
char* ReportShareableMissions(int iPrintAllNames)
{
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	MissionDef **eaShareableMissionsWithNoReqs = NULL;
	MissionDef **eaShareableMissionsWithReqs = NULL;
	MissionDef **eaTimedMissions = NULL;
	MissionDef **eaMissionsWithOnStartRewards = NULL;
	MissionDef **eaNonShareableButDontKnowReason = NULL;
	MissionDef **eaWrongType = NULL;
	int iNumMissions = 0;
	int i;
	FILE *file;

	file = fopen("c:\\MissionSharingReport.txt", "w");
	if (!iPrintAllNames){
		fprintf(file, "This reports how many shareable missions there are, and how many missions\n");
		fprintf(file, "meet each restriction against mission sharing.\n");
		fprintf(file, "Generate by typing 'ReportShareableMissions 0'.\n");
		fprintf(file, "'ReportShareableMissions 1' generates the same report, but with all mission names included.\n");
		fprintf(file, "----------------------------------------------------------\n\n");
	} else {
		fprintf(file, "This reports a list of all shareable missions, and all the missions\n");
		fprintf(file, "that meet each restriction against mission sharing.\n");
		fprintf(file, "Generate by typing 'ReportShareableMissions 1'.\n");
		fprintf(file, "'ReportShareableMissions 0' generates the same report, but with just\n");
		fprintf(file, "a count instead of printing each mission name\n");
		fprintf(file, "----------------------------------------------------------\n\n");
	}

	for(i=0; i < eaSize(&pMissions->ppReferents); i++){
		MissionDef *pDef = (MissionDef*)pMissions->ppReferents[i];
		if (missiondef_GetType(pDef) != MissionType_Perk 
			&& missiondef_GetType(pDef) != MissionType_OpenMission
			&& missiondef_HasDisplayName(pDef)){
			iNumMissions++;

			if(pDef->missionType == MissionType_Nemesis 
				|| pDef->missionType == MissionType_NemesisArc 
				|| pDef->missionType == MissionType_NemesisSubArc 
				|| pDef->missionType == MissionType_TourOfDuty 
				|| pDef->missionType == MissionType_AutoAvailable){
				eaPush(&eaWrongType, pDef);
			} else if (missiondef_HasOnStartTimerRecursive(pDef)){
				eaPush(&eaTimedMissions, pDef);
			} else if (missiondef_HasOnStartRewards(pDef)){
				eaPush(&eaMissionsWithOnStartRewards, pDef);
			} else if (!missiondef_IsShareable(pDef)){
				eaPush(&eaNonShareableButDontKnowReason, pDef);
			} else {
				if (pDef->missionReqs){
					eaPush(&eaShareableMissionsWithReqs, pDef);
				} else {
					eaPush(&eaShareableMissionsWithNoReqs, pDef);
				}
			}
		}
	}

	if (eaSize(&eaShareableMissionsWithNoReqs)){
		fprintf(file, "Missions that are always shareable (except level requirement): %d\n", eaSize(&eaShareableMissionsWithNoReqs));
		if (iPrintAllNames){
			PrintMissionNames(file, &eaShareableMissionsWithNoReqs);
			fprintf(file, "\n");
		}
	}
	if (eaSize(&eaShareableMissionsWithReqs)){
		fprintf(file, "Missions that may be shareable, but have mission requirements: %d\n", eaSize(&eaShareableMissionsWithReqs));
		if (iPrintAllNames){
			PrintMissionNames(file, &eaShareableMissionsWithReqs);
			fprintf(file, "\n");
		}
	}
	if (eaSize(&eaTimedMissions)){
		fprintf(file, "Missions with OnStart timers (NOT shareable): %d\n", eaSize(&eaTimedMissions));
		if (iPrintAllNames){
			PrintMissionNames(file, &eaTimedMissions);
			fprintf(file, "\n");
		}
	}
	if (eaSize(&eaMissionsWithOnStartRewards)){
		fprintf(file, "Missions with OnStart rewards (NOT shareable): %d\n", eaSize(&eaMissionsWithOnStartRewards));
		if (iPrintAllNames){
			PrintMissionNames(file, &eaMissionsWithOnStartRewards);
			fprintf(file, "\n");
		}
	}
	if (eaSize(&eaWrongType)){
		fprintf(file, "Missions that aren't shareable because of the MissionType (probably AutoAvailable): %d\n", eaSize(&eaWrongType));
		if (iPrintAllNames){
			PrintMissionNames(file, &eaWrongType);
			fprintf(file, "\n");
		}
	}
	if (eaSize(&eaNonShareableButDontKnowReason)){
		fprintf(file, "Missions that aren't shareable, but I don't know why: %d\n", eaSize(&eaNonShareableButDontKnowReason));
		if (iPrintAllNames){
			PrintMissionNames(file, &eaNonShareableButDontKnowReason);
			fprintf(file, "\n");
		}
	}

	fclose(file);
	eaDestroy(&eaShareableMissionsWithNoReqs);
	eaDestroy(&eaShareableMissionsWithReqs);
	eaDestroy(&eaTimedMissions);
	eaDestroy(&eaMissionsWithOnStartRewards);
	eaDestroy(&eaNonShareableButDontKnowReason);
	eaDestroy(&eaWrongType);

	return "Missions per level dumped to 'c:\\MissionSharingReport.txt'";
}


// ---------------------------------------------------------------------
// Functions to search for all Interactables
// ---------------------------------------------------------------------

// Reports door expressions from a set of encounter layers
void ReportMapMoveDoorExpressionsFromLayers(LibFileLoad ***peaGeoLayers, FILE *file)
{
	int i,j,k;

	for(i=eaSize(peaGeoLayers)-1; i>=0; --i) {
		LibFileLoad *pLayer = (*peaGeoLayers)[i];
		bool bFoundDoor = false;

		for(j=eaSize(&pLayer->defs)-1; j>=0; --j) {
			GroupDef *pDef = pLayer->defs[j];
			bool bIsEvent = false;

			if (pDef->property_structs.interaction_properties){

				// Check all property entries
				for (k=eaSize(&pDef->property_structs.interaction_properties->eaEntries)-1; k>=0; --k){
					WorldInteractionPropertyEntry *pEntry = pDef->property_structs.interaction_properties->eaEntries[k];

					// Check if this interactable is a door
					if (pEntry && pEntry->pDoorProperties && pEntry->pDoorProperties->eDoorType == WorldDoorType_MapMove) {
						if (pEntry->pInteractCond){
							const char *pchCondition = exprGetCompleteString(pEntry->pInteractCond);
							fprintf(file, "Layer:   %-80s %-40s %s", pLayer->defs[0]->filename, pDef->name_str, pchCondition);
							if (pEntry->pDoorProperties->bPerPlayer || pEntry->pDoorProperties->bSinglePlayer){
								fprintf(file, " (evaluated for each player)");
							}
							fprintf(file, "\n");
						}
					}
				}
			}
		}
		if (bFoundDoor){
			fprintf(file, "\n");
		}
	}
}

void ReportMapMoveDoorExpressionsFromMissionRecursive(MissionDef *pDef, FILE *file)
{
	if (pDef){
		int i;
		bool bFound = false;

		for (i = 0; i < eaSize(&pDef->ppInteractableOverrides); i++){
			WorldInteractionPropertyEntry *pEntry = pDef->ppInteractableOverrides[i]->pPropertyEntry;
			// Check if this interactable is a door
			if (pEntry && pEntry->pDoorProperties) {
				if (pEntry->pInteractCond){
					const char *pchCondition = exprGetCompleteString(pEntry->pInteractCond);

					fprintf(file, "Mission: %-80s %-40s %s", pDef->pchRefString, pDef->ppInteractableOverrides[i]->pcInteractableName, pchCondition);
					if (pEntry->pDoorProperties->bPerPlayer || pEntry->pDoorProperties->bSinglePlayer){
						fprintf(file, " (evaluated for each player)");
					}
					fprintf(file, "\n");
				} else {
					//fprintf(file, "  %s\t<none>\n", pDef->name);
				}
			}
		}

		for (i = 0; i < eaSize(&pDef->subMissions); i++){
			ReportMapMoveDoorExpressionsFromMissionRecursive(pDef->subMissions[i], file);
		}
	}
}

// Prints out the interact requirement for every door in the game
AUTO_COMMAND ACMD_SERVERCMD;
char* ReportMapMoveDoorExpressions(void)
{
	LibFileLoad **eaGeoLayers = NULL;
	DictionaryEArrayStruct *pMissions = resDictGetEArrayStruct("MissionDef");
	FILE *file;
	int i;

	AssetLoadGeoLayers(&eaGeoLayers);

	file = fopen("c:\\DoorExpressions.txt", "w");
	fprintf(file, "This is a list of all of the MapMove doors in the game that have conditions on\n");
	fprintf(file, "them, and the conditions for each.\n");
	fprintf(file, "Generate by typing 'ReportMapMoveDoorExpressions'.\n");
	fprintf(file, "----------------------------------------------------------\n\n");

	ReportMapMoveDoorExpressionsFromLayers(&eaGeoLayers, file);

	AssetCleanupGeoLayers(&eaGeoLayers);

	// Loop through missions for extra properties
	for (i = 0; i < eaSize(&pMissions->ppReferents); i++){
		MissionDef *pDef = (MissionDef*)pMissions->ppReferents[i];
		ReportMapMoveDoorExpressionsFromMissionRecursive(pDef, file);
	}

	fclose(file);
	return "Report written to 'c:\\DoorExpressions.txt'";
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportExternalLayersWithExcludes(char *exclude_list)
{
	int i;
	char **exclude_paths = NULL;
	ZoneMapInfo *zminfo;
	RefDictIterator zmap_iter = {0};
	FILE *file;

	if(exclude_list)
		DivideString(exclude_list, ",", &exclude_paths, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	file = fopen("c:\\ExternalLayers.txt", "w");
	fprintf(file, "This is a list of all zone maps that reference layers outside\n");
	fprintf(file, "the directory they are in, or one of its subdirectories.\n");
	fprintf(file, "Generate by typing 'ReportExternalLayers' or\n");
	fprintf(file, "'ReportExternalLayersWithExcludes' followed by\n");
	fprintf(file, "a comma decimated list of zone map paths.\n");
	if(exclude_list)
	{
		fprintf(file, "Excluded Paths: ");
		for ( i=0; i < eaSize(&exclude_paths); i++ )
		{
			fprintf(file, "%s", exclude_paths[i]);
			if(i < eaSize(&exclude_paths)-1)
				fprintf(file, ", ");
		}
		fprintf(file, "\n");
	}
	fprintf(file, "----------------------------------------------------------\n\n");

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter))
	{
		bool excluded = false;
		bool found = false;
		char zmap_path[MAX_PATH];

		for ( i=0; i < eaSize(&exclude_paths); i++ )
		{
			if(strStartsWith(zminfo->filename, exclude_paths[i]))
			{
				excluded = true;
				break;
			}
		}
		if(excluded)
			continue;

		strcpy(zmap_path, zminfo->filename);
		getDirectoryName(zmap_path);
		
		for ( i=0; i < eaSize(&zminfo->layers); i++ )
		{
			ZoneMapLayerInfo *layer = zminfo->layers[i];
			if(strStartsWith(layer->filename, "maps/") && !strStartsWith(layer->filename, zmap_path))
			{
				if(!found)
				{
					found = true;
					fprintf(file, "\nZone Map: %s\n", zminfo->filename);
				}
				fprintf(file, "--- Layer: %s\n", layer->filename);
			}
		}
	}
	fclose(file);
	eaDestroyEx(&exclude_paths, NULL);

	return "Report written to 'c:\\ExternalLayers.txt'"; 
}

AUTO_COMMAND ACMD_SERVERCMD;
char* ReportExternalLayers()
{
	return ReportExternalLayersWithExcludes(NULL);
}

///////////////////////////////////////////////////////////////////////////
// Audio Asset Commands
///////////////////////////////////////////////////////////////////////////

static bool ReportAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

static bool ReportAudioAssets_HandleWorldInteractionProperties(const WorldInteractionProperties *pWorldInteractionProperties, const char ***peaStrings)
{
	bool bResourceHasAudio = false;
	if (pWorldInteractionProperties) {
		FOR_EACH_IN_EARRAY(pWorldInteractionProperties->eaEntries, WorldInteractionPropertyEntry, pWorldInteractionPropertyEntry) {
			if (pWorldInteractionPropertyEntry->pSoundProperties)
			{
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchAttemptSound,	peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchFailureSound,	peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchInterruptSound,peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchSuccessSound,	peaStrings);

				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementReturnEndSound,	peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementReturnStartSound,	peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementTransEndSound,		peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldInteractionPropertyEntry->pSoundProperties->pchMovementTransStartSound,	peaStrings);
			}
		} FOR_EACH_END;
	}
	return bResourceHasAudio;
}

static FileScanAction ReportAudioAssets_ScanLayerFiles(char* dir, struct _finddata32_t* data, char*** filenames)
{
	char fullPath[ MAX_PATH ];
	sprintf(fullPath, "%s/%s", dir, data->name);

	if(!(data->attrib & _A_SUBDIR) && strEndsWith(data->name, ".layer")) {
		eaPush(filenames, strdup(fullPath));
	}

	return FSA_EXPLORE_DIRECTORY;
}

static FileScanAction ReportAudioAssets_ScanModelnamesFiles(char* dir, struct _finddata32_t* data, char*** filenames)
{
	char fullPath[ MAX_PATH ];
	sprintf(fullPath, "%s/%s", dir, data->name);

	if(!(data->attrib & _A_SUBDIR) && strEndsWith(data->name, MODELNAMES_EXTENSION)) {
		eaPush(filenames, strdup(fullPath));
	}

	return FSA_EXPLORE_DIRECTORY;
}

static FileScanAction ReportAudioAssets_ScanObjlibFiles(char* dir, struct _finddata32_t* data, char*** filenames)
{
	char fullPath[ MAX_PATH ];
	sprintf(fullPath, "%s/%s", dir, data->name);

	if(!(data->attrib & _A_SUBDIR) && strEndsWith(data->name, OBJLIB_EXTENSION)) {
		eaPush(filenames, strdup(fullPath));
	}

	return FSA_EXPLORE_DIRECTORY;
}

static FileScanAction ReportAudioAssets_ScanRootmodsFiles(char* dir, struct _finddata32_t* data, char*** filenames)
{
	char fullPath[ MAX_PATH ];
	sprintf(fullPath, "%s/%s", dir, data->name);

	if(!(data->attrib & _A_SUBDIR) && strEndsWith(data->name, ROOTMODS_EXTENSION)) {
		eaPush(filenames, strdup(fullPath));
	}

	return FSA_EXPLORE_DIRECTORY;
}

static void ReportAudioAssets_LibFile_GetAudioAssets(const char *pcDirectory, const char *pcFileType, FileScanProcessor fileScanner, const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	char** objectFiles = NULL;

	*ppcType = strdup(pcFileType);

	fileScanAllDataDirs(pcDirectory, fileScanner, &objectFiles);

	FOR_EACH_IN_EARRAY(objectFiles, char, filename)
	{
		LibFileLoad libFile = {0};
		bool bResourceHasAudio = false;

		if( !ParserReadTextFile(filename, parse_LibFileLoad, &libFile, 0)) {
			Errorf("%s -- Unable to read when building audio asset report\n", filename);
			continue;
		}

		FOR_EACH_IN_EARRAY(libFile.defs, GroupDef, pGroupDef)
		{
			if (pGroupDef->property_structs.encounter_properties) {
				FOR_EACH_IN_EARRAY(pGroupDef->property_structs.encounter_properties->eaActors, WorldActorProperties, pWorldActorProperties) {
					bResourceHasAudio |= ReportAudioAssets_HandleWorldInteractionProperties(pWorldActorProperties->pInteractionProperties, peaStrings);
				} FOR_EACH_END;
			}
			if (pGroupDef->property_structs.interaction_properties) {
				bResourceHasAudio |= ReportAudioAssets_HandleWorldInteractionProperties(pGroupDef->property_structs.interaction_properties, peaStrings);
			}
			if (pGroupDef->property_structs.sound_sphere_properties) {
				bResourceHasAudio |= ReportAudioAssets_HandleString(pGroupDef->property_structs.sound_sphere_properties->pcEventName, peaStrings);
			}

			if (pGroupDef->property_structs.client_volume.sound_volume_properties) {
				bResourceHasAudio |= ReportAudioAssets_HandleString(pGroupDef->property_structs.client_volume.sound_volume_properties->event_name, peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pGroupDef->property_structs.client_volume.sound_volume_properties->event_name_override_param, peaStrings);
				bResourceHasAudio |= ReportAudioAssets_HandleString(pGroupDef->property_structs.client_volume.sound_volume_properties->music_name, peaStrings);
			}

			if (pGroupDef->property_structs.server_volume.event_volume_properties &&
				pGroupDef->property_structs.server_volume.event_volume_properties->first_entered_action)
			{
				FOR_EACH_IN_EARRAY(pGroupDef->property_structs.server_volume.event_volume_properties->first_entered_action->eaActions, WorldGameActionProperties, pWorldGameActionProperties) {
					if (pWorldGameActionProperties->pSendNotificationProperties) {
						bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldGameActionProperties->pSendNotificationProperties->pchSound, peaStrings);
					}
				} FOR_EACH_END;
			}
			if (pGroupDef->property_structs.server_volume.interaction_volume_properties) {
				bResourceHasAudio |= ReportAudioAssets_HandleWorldInteractionProperties(pGroupDef->property_structs.server_volume.interaction_volume_properties, peaStrings);
			}
			if (pGroupDef->property_structs.server_volume.obsolete_optionalaction_properties) {
				FOR_EACH_IN_EARRAY(pGroupDef->property_structs.server_volume.obsolete_optionalaction_properties->entries, WorldOptionalActionVolumeEntry, pWorldOptionalActionVolumeEntry) {
					FOR_EACH_IN_EARRAY(pWorldOptionalActionVolumeEntry->actions.eaActions, WorldGameActionProperties, pWorldGameActionProperties) {
						if (pWorldGameActionProperties->pSendNotificationProperties) {
							bResourceHasAudio |= ReportAudioAssets_HandleString(pWorldGameActionProperties->pSendNotificationProperties->pchSound, peaStrings);
						}
					} FOR_EACH_END;
				} FOR_EACH_END;
			}
			if (pGroupDef->property_structs.server_volume.neighborhood_volume_properties) {
				bResourceHasAudio |= ReportAudioAssets_HandleString(pGroupDef->property_structs.server_volume.neighborhood_volume_properties->sound_effect, peaStrings);
			}
		}
		FOR_EACH_END;

		StructDeInit(parse_LibFileLoad, &libFile);

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	FOR_EACH_END;

	eaDestroyEx(&objectFiles, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_HIDE;
void ReportAudioAssets_ProcessOnServer(EntityRef clientEntityRef)
{
	AudioAssets serverAudioAssets;
	AudioAssetComponents *pComponent;

	ZeroStruct(&serverAudioAssets);

	pComponent = StructCreate(parse_AudioAssetComponents);
	reportAudioAssets_AiCivilian_Callback(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	contact_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	cutscene_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	encounter_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	fsm_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	interaction_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	ReportAudioAssets_LibFile_GetAudioAssets("maps/", "Map Layer", ReportAudioAssets_ScanLayerFiles, &pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	msg_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	mission_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	ReportAudioAssets_LibFile_GetAudioAssets("object_library/", "Modelnames", ReportAudioAssets_ScanModelnamesFiles, &pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	ReportAudioAssets_LibFile_GetAudioAssets("object_library/", "Objlib", ReportAudioAssets_ScanObjlibFiles, &pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	ReportAudioAssets_LibFile_GetAudioAssets("object_library/", "Rootmods", ReportAudioAssets_ScanRootmodsFiles, &pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	sndCommon_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	pComponent = StructCreate(parse_AudioAssetComponents);
	ugcResource_GetAudioAssets(&pComponent->pcType, &pComponent->eaStrings, &pComponent->uiNumData, &pComponent->uiNumDataWithAudio);
	eaPush(&serverAudioAssets.eaComponents, pComponent);

	ClientCmd_ReportAudioAssets_ProcessOnClient(entFromEntityRefAnyPartition(clientEntityRef), &serverAudioAssets);
}

#include "cmdClientReport_h_ast.c"
