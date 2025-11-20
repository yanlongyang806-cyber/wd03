#include "gclTransformation.h"
#include "gclTransformation_h_ast.h"
#include "TransformationCommon.h"

#include "Entity.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "Color.h"

#include "dynFxInfo.h"
#include "dynFxInterface.h"

#include "file.h"

#include "TextParserEnums.h"
#include "ResourceManager.h"
#include "rgb_hsv.h"
#include "StashTable.h"
#include "StringCache.h"

#include "AutoGen/CostumeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static TransformationPartMapsDef s_transformationPartMapDef = {0};
static EntityRef *s_eaEntCostumeUpdates = NULL;

// ----------------------------------------------------------------------------------------
static void TransformationPartMapsDef_Fixup(TransformationPartMapsDef *pDef, const char *filename)
{
	if (eaSize(&pDef->eaParts) == 0 || eaSize(&pDef->eaCGeoMaps) == 0)
		return;

	pDef->geoPartMap = stashTableCreateAddress(8);

	FOR_EACH_IN_EARRAY(pDef->eaParts, TransformationPart, pPart)
	{
		if (!pPart->pchName)
		{
			ErrorFilenamef(filename, "A TransformPart has no name specified.");
		}
		else if (!pPart->pPCPart)
		{
			ErrorFilenamef(filename, "No Part defined for TransformPart %s", pPart->pchName);
		}

		if (pPart->color_0 == ETransformColorOrigin_SOURCE || pPart->color_1 == ETransformColorOrigin_SOURCE || 
			pPart->color_2 == ETransformColorOrigin_SOURCE || pPart->color_3 == ETransformColorOrigin_SOURCE)
		{
			pPart->bHasColorSrc = true;
		}

		if (pPart->color_0 == ETransformColorOrigin_DEST || pPart->color_1 == ETransformColorOrigin_DEST || 
			pPart->color_2 == ETransformColorOrigin_DEST || pPart->color_3 == ETransformColorOrigin_DEST)
		{
			pPart->bHasColorDest = true;
		}
	}
	FOR_EACH_END

	if (pDef->geoPartMap)
	{
		FOR_EACH_IN_EARRAY(pDef->eaCGeoMaps, TransformationGeoMap, pGeoMap)
		{
			if (pGeoMap->pchCGeoName)
			{
				if (pGeoMap->pchTransformPartName)
				{
					FOR_EACH_IN_EARRAY(pDef->eaParts, TransformationPart, pPart)
					{
						if (pPart->pchName == pGeoMap->pchTransformPartName)
						{
							pGeoMap->pPart = pPart;
							break;
						}
					}
					FOR_EACH_END

					if (pGeoMap->pPart)
					{
						stashAddressAddPointer(pDef->geoPartMap, pGeoMap->pchCGeoName, pGeoMap, false);
					}
					else
					{
						ErrorFilenamef(filename, "GeoMap(%s) could not find part %s", pGeoMap->pchCGeoName, pGeoMap->pchTransformPartName);
					}
				}
				else
				{
					ErrorFilenamef(filename, "No PartName specified for GeoMap %s", pGeoMap->pchCGeoName);
				}
			}
			else
			{
				ErrorFilenamef(filename, "CGeoName not specified for a GeoMap");
			}
		}
		FOR_EACH_END
	}
}


// ---------------------------------------------------------------------------------------------------
AUTO_STARTUP(gclTransformation);
void gclTransformation_Startup(void)
{
	ParserLoadFiles(NULL,"defs/transformation/transformMappings.xmap","transformMappings.bin",
					PARSER_OPTIONALFLAG,parse_TransformationPartMapsDef,&s_transformationPartMapDef);

	TransformationPartMapsDef_Fixup(&s_transformationPartMapDef, "transformMappings.xmap");
}

// ---------------------------------------------------------------------------------------------------
void gclTransformation_BeginTransformation(Entity *e)
{
	if (e->costumeRef.pTransformation && e->costumeRef.pTransformation->pSourceCostume)
	{
		CostumeTransformation *pTransformation = e->costumeRef.pTransformation;
		
		if (pTransformation->pCurrentCostume)
		{
			StructDestroy(parse_PlayerCostume, pTransformation->pCurrentCostume);
			pTransformation->pCurrentCostume = NULL;
		}

		pTransformation->pCurrentCostume = StructClone(parse_PlayerCostume, pTransformation->pSourceCostume);

		if (pTransformation->pCurrentCostume)
		{
			pTransformation->fCurTime = 0.f;
			pTransformation->bIsTransforming = true;
		}
	}

}

// ---------------------------------------------------------------------------------------------------
// input to function will be assumed a POOL_STRING
static TransformationPart* gclTransformation_GetTransformPartByCGeoName(const char *pchName)
{
	TransformationGeoMap *pGeoMap = NULL;
		
	stashAddressFindPointer(s_transformationPartMapDef.geoPartMap, pchName, &pGeoMap);
	return (pGeoMap) ? pGeoMap->pPart : NULL;
}

// ---------------------------------------------------------------------------------------------------
static PlayerCostume* gclTransformation_GetDestCostume(Entity *e)
{
	PlayerCostume *pCurrentCostume = e->costumeRef.pTransformation->pCurrentCostume;
	PlayerCostume *pDestCostume;
	
	e->costumeRef.pTransformation->pCurrentCostume = NULL;

	pDestCostume = costumeEntity_GetEffectiveCostume(e);

	e->costumeRef.pTransformation->pCurrentCostume = pCurrentCostume;

	return pDestCostume;
}


#define copyColorU8(src,dst)	((dst)[0]=(src)[0],(dst)[1]=(src)[1],(dst)[2]=(src)[2],(dst)[3]=(src)[3])
// ---------------------------------------------------------------------------------------------------
static U8* gclTransformation_GetColorFromPart(PCPart *pPart, S32 index)
{
	switch (index)
	{
		case 0:  return (U8*)pPart->color0;
		xcase 1: return (U8*)pPart->color1;
		xcase 2: return (U8*)pPart->color2;
		xcase 3: return (U8*)pPart->color3;
		xdefault: return (U8*)pPart->color0;
	}
}

// ---------------------------------------------------------------------------------------------------
// returns the first found part by bone
static PCPart* gclTransformation_GetPartByBone(PlayerCostume *pCostume, ReferenceHandle hBoneDef)
{
	FOR_EACH_IN_EARRAY(pCostume->eaParts, PCPart, pPart)
	{
		if (RefSystem_CompareHandles(REF_HANDLEPTR(pPart->hBoneDef), hBoneDef))
			return pPart;
	}
	FOR_EACH_END

	return NULL;
}

// ---------------------------------------------------------------------------------------------------
static void gclTransformation_ApplyColorFromOrigin(PCPart *pOriginPart, PCPart *pDestPart, 
												   TransformationPart *pTransformPart, ETransformColorOrigin origin)
{
	ETransformColorOrigin *pClrOrigin = &pTransformPart->color_0;
	S32 x;
	for (x = 0; x < 4; ++x, ++pClrOrigin)
	{
		if (*pClrOrigin == origin)
		{
			U8 *pColorDest = gclTransformation_GetColorFromPart((PCPart*)pDestPart, x);
			U8 *pColorOrigin = gclTransformation_GetColorFromPart((PCPart*)pOriginPart, x);
			copyColorU8(pColorOrigin, pColorDest);
		}
	}
}

// ---------------------------------------------------------------------------------------------------
static int gclTransformation_CreateNewParts(PlayerCostume *pCostume, 
											ETransformColorOrigin costumeOrigin,
											int bGetEffectColor, 
											TransformationEventDef *pEvent,
											NOCONST(PCPart) ***peaPartsToAdd, 
											TransformationPart ***peaXFormParts,
											U8 *effectColor)
{
	int bHasEffectColor = false;
	int bNeedsColorFromOppositeCostume = false;
	S32 i;

	// go through the costume and find all the parts with the given bone
	// create the appropriate parts, and save them on a list for later
	for (i = eaSize(&pCostume->eaParts) - 1; i >= 0; --i)
	{
		PCPart* pPCPart = pCostume->eaParts[i];
		if (REF_HANDLE_COMPARE(pPCPart->hBoneDef, pEvent->hBoneDef))
		{
			TransformationPart *pTransformPart;
			NOCONST(PCPart) * pNewPCPart;
			PCGeometryDef *pGeoDef = GET_REF(pPCPart->hGeoDef);

			if (!pGeoDef)
				continue;

			if (!bHasEffectColor && bGetEffectColor)
			{
				S32 index = Transformation_GetColorIndex(pEvent->effectColorOrigin);
				const U8 *clr = gclTransformation_GetColorFromPart(pPCPart, index);
				copyColorU8(clr, effectColor);
				bHasEffectColor = true;
			}

			pTransformPart = gclTransformation_GetTransformPartByCGeoName(pGeoDef->pcName);
			if (!pTransformPart)
				continue;

			// create a PC part for the player costume
			pNewPCPart = StructCloneDeConst(parse_PCPart, pTransformPart->pPCPart);
			if (!pNewPCPart)
				continue;

			// fixup the parts to match the destination costume if needed
			if ((costumeOrigin == ETransformColorOrigin_SOURCE) ? pTransformPart->bHasColorSrc : pTransformPart->bHasColorDest)
				gclTransformation_ApplyColorFromOrigin(pPCPart, (PCPart*)pNewPCPart, pTransformPart, costumeOrigin);

			if (!bNeedsColorFromOppositeCostume)
				bNeedsColorFromOppositeCostume = (costumeOrigin == ETransformColorOrigin_SOURCE) 
													? pTransformPart->bHasColorDest : pTransformPart->bHasColorSrc;

			// save the parts we are adding and transform part associated
			eaPush(peaXFormParts, pTransformPart);
			eaPush(peaPartsToAdd, pNewPCPart);
		}
	}

	return bNeedsColorFromOppositeCostume;
}

// ---------------------------------------------------------------------------------------------------
static void gclTransformation_GetColorsFromCostume(PlayerCostume *pCostume, 
												   ETransformColorOrigin costumeOrigin,
												   NOCONST(PCPart) **eaPartsToAdd, 
												   TransformationPart **eaXFormParts,
												   TransformationEventDef *pEvent,
												   int bGetEffectColor,
												   U8 *effectColor)
{
	FOR_EACH_IN_EARRAY(pCostume->eaParts, PCPart, pPCPart)
	{
		if (REF_HANDLE_COMPARE(pPCPart->hBoneDef, pEvent->hBoneDef))
		{
			if (costumeOrigin != ETransformColorOrigin_NONE)
			{
				S32 iPart;
				for(iPart = eaSize(&eaPartsToAdd) - 1; iPart >= 0; --iPart)
				{
					TransformationPart *pTransformPart = eaXFormParts[iPart];
					NOCONST(PCPart) *pNewPCPart = eaPartsToAdd[iPart];
					if ((costumeOrigin == ETransformColorOrigin_SOURCE) ? pTransformPart->bHasColorSrc : pTransformPart->bHasColorDest)
						gclTransformation_ApplyColorFromOrigin(pPCPart, (PCPart*)pNewPCPart, pTransformPart, costumeOrigin);
				}
			}

			if (bGetEffectColor)
			{
				S32 index = Transformation_GetColorIndex(pEvent->effectColorOrigin);
				// the color for the effect is taken from the source
				const U8 *clr = gclTransformation_GetColorFromPart((PCPart*)pPCPart, index);
				copyColorU8(clr, effectColor);
			}

			break;
		}
	}
	FOR_EACH_END
}

// ---------------------------------------------------------------------------------------------------
// returns true if a costume swap occurred. 
static int gclTransformation_TriggerEvent(Entity *e, NOCONST(PlayerCostume) *pEffectiveCostume, 
										  PlayerCostume *pSrcCostume, PlayerCostume *pDestCostume, 
										  TransformationDef *pDef, TransformationEventDef *pEvent)
{
	U8 effectColor[4] = {0};
	bool bChangedCostume = false;
	

	if (IS_HANDLE_ACTIVE(pEvent->hBoneDef))
	{
		S32 i;
		bool bHandledSourceColor = false;
		NOCONST(PCPart) **eaPartsToAdd = NULL;
		TransformationPart **eaXFormParts = NULL;

		// create the new parts using the right costume as the key
		if (pDef->bKeyFromSourceCostume)
		{
			int bNeedsColorFromDest;
			bNeedsColorFromDest = gclTransformation_CreateNewParts(pSrcCostume, ETransformColorOrigin_SOURCE,
																	Transformation_IsColorOriginSource(pEvent->effectColorOrigin), 
																	pEvent, &eaPartsToAdd, &eaXFormParts, effectColor);
			// if we need colors from the other costume 
			if (bNeedsColorFromDest || Transformation_IsColorOriginDest(pEvent->effectColorOrigin))
			{
				gclTransformation_GetColorsFromCostume(pDestCostume, 
														bNeedsColorFromDest ? ETransformColorOrigin_DEST : ETransformColorOrigin_NONE, 
														eaPartsToAdd, eaXFormParts, pEvent,
														Transformation_IsColorOriginDest(pEvent->effectColorOrigin),
														effectColor);
			}
		}
		else
		{
			int bNeedsColorFromSrc;
			bNeedsColorFromSrc = gclTransformation_CreateNewParts(pDestCostume, ETransformColorOrigin_DEST, 
																	Transformation_IsColorOriginDest(pEvent->effectColorOrigin), 
																	pEvent, &eaPartsToAdd, &eaXFormParts, effectColor);

			if (bNeedsColorFromSrc || Transformation_IsColorOriginSource(pEvent->effectColorOrigin))
			{
				gclTransformation_GetColorsFromCostume(pSrcCostume,
														bNeedsColorFromSrc ? ETransformColorOrigin_SOURCE : ETransformColorOrigin_NONE,
														eaPartsToAdd, eaXFormParts, pEvent,
														Transformation_IsColorOriginSource(pEvent->effectColorOrigin),
														effectColor);

			}
		}

		// go through the effective costume and remove any costume pieces attached to the given bone
		for (i = eaSize(&pEffectiveCostume->eaParts) - 1; i >= 0; --i)
		{
			NOCONST(PCPart)* pPCPart = pEffectiveCostume->eaParts[i];
			if (REF_HANDLE_COMPARE(pPCPart->hBoneDef, pEvent->hBoneDef))
			{
				// remove this part from the costume and destroy it
				eaRemove(&pEffectiveCostume->eaParts, i);
				StructDestroyNoConst(parse_PCPart, pPCPart); 
			}
		}

		// finally add all the new costume parts we created
		if (eaSize(&eaPartsToAdd))
		{
			eaPushEArray(&pEffectiveCostume->eaParts, &eaPartsToAdd);
			bChangedCostume = true;

			eaDestroy(&eaPartsToAdd);
			eaDestroy(&eaXFormParts);
		}
	}

	if (pEvent->swapSkinColor)
	{
		copyColorU8(pDestCostume->skinColor, pEffectiveCostume->skinColor);
	}

	// if we have an effect
	if (pEvent->pchEffect)
	{
		F32 hue = 0.f;

		if (pEvent->effectColorOrigin != ETransformEffectColorOrigin_NONE)
		{
			Vec3 HSV, rgb;
			u8ColorToVec3(rgb, effectColor);
			rgbToHsv(rgb, HSV);
			hue = HSV[0];
		}

		dtAddFx(e->dyn.guidFxMan, pEvent->pchEffect, NULL, 0, 0, hue, 0, false, eDynFxSource_Costume, NULL, NULL);
	}

	return bChangedCostume;
}

// ---------------------------------------------------------------------------------------------------
static void gclTransformation_TerminateUpdate(Entity *e)
{
	// do the final costume swap
	if (e->costumeRef.pTransformation->pCurrentCostume)
	{
		StructDestroy(parse_PlayerCostume, e->costumeRef.pTransformation->pCurrentCostume);
		e->costumeRef.pTransformation->pCurrentCostume = NULL;
	}
	
	e->costumeRef.pTransformation->bIsTransforming = false;

	eaiPush(&s_eaEntCostumeUpdates, entGetRef(e));
}

// ---------------------------------------------------------------------------------------------------
void gclTransformation_Update(Entity *e, F32 fDTime)
{
	CostumeTransformation* pTransformation;
	TransformationDef *pDef;
	PlayerCostume *pDestCostume;
	PlayerCostume *pSrcCostume;
	NOCONST(PlayerCostume) *pCurrentCostume;
	F32 fPrevTime;
	bool bEventTriggered = false;
	if (!e->costumeRef.pTransformation || !e->costumeRef.pTransformation->bIsTransforming)
		return;
	
	pTransformation = e->costumeRef.pTransformation;
	
	pDestCostume = gclTransformation_GetDestCostume(e);
	pCurrentCostume = CONTAINER_NOCONST(PlayerCostume, pTransformation->pCurrentCostume);
	pSrcCostume = pTransformation->pSourceCostume;

	pDef = GET_REF(pTransformation->hDef);
	if (!pDef || !pDestCostume || !pCurrentCostume || !pSrcCostume)
	{	// stop update
		gclTransformation_TerminateUpdate(e);
		return;
	}

	fPrevTime = pTransformation->fCurTime;
	pTransformation->fCurTime += fDTime;
	if (pTransformation->fCurTime > pDef->fTotalTime)
	{
		gclTransformation_TerminateUpdate(e);
		return;
	}

	FOR_EACH_IN_EARRAY_FORWARDS(pDef->eaEventDef, TransformationEventDef, pEventDef)
	{
		if (fPrevTime <= pEventDef->fTime && pTransformation->fCurTime > pEventDef->fTime)
		{
			// trigger the event
			if (gclTransformation_TriggerEvent(e, pCurrentCostume, pSrcCostume, pDestCostume, pDef, pEventDef))
				bEventTriggered = true;
		}
	}	
	FOR_EACH_END	

	if (bEventTriggered)
	{
		// put off the costume fix-up until later in the client update frame 
		// as when updating here the costume visibly disappears for a frame
		eaiPush(&s_eaEntCostumeUpdates, entGetRef(e));
	}
}

// ---------------------------------------------------------------------------------------------------
// fix-up all entity costumes that were done this frame
void gclTransformation_OncePerFrame()
{
	S32 i;
	for (i = eaiSize(&s_eaEntCostumeUpdates) - 1; i >= 0; --i)
	{
		EntityRef ref = s_eaEntCostumeUpdates[i];
		Entity *e = entFromEntityRefAnyPartition(ref);
		if (e)
		{
			costumeGenerate_FixEntityCostume(e);
		}
	}

	eaiClear(&s_eaEntCostumeUpdates);
}

#include "gclTransformation_h_ast.c"
