#include "TransformationCommon.h"
#include "TransformationCommon_h_ast.h"

#include "Entity.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"

#include "file.h"

#include "TextParserEnums.h"
#include "ResourceManager.h"

#include "AutoGen/CostumeCommon_h_ast.h"



static DictionaryHandle s_TransformationDict;


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


AUTO_FIXUPFUNC;
TextParserResult fixupTransformationDef(TransformationDef* formation, enumTextParserFixupType eType, void *pExtraData)
{
	return PARSERESULT_SUCCESS;
}

// ---------------------------------------------------------------------------------------------------
static int Transformation_Validate(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, void* pResource, U32 userID)
{
	TransformationDef* pDef = pResource;
	
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			F32 lastEventTime = 0.f; 

			if (eaSize(&pDef->eaEventDef) == 0)
			{
				ErrorFilenamef(pResourceName, "No events defined for transformation.");
				return VALIDATE_HANDLED;
			}

			FOR_EACH_IN_EARRAY(pDef->eaEventDef, TransformationEventDef, pEvent)
			{
				if (pEvent->fTime < 0.f)
				{
					ErrorFilenamef(pResourceName, "Invalid event time specified.");
				}
				else if (pEvent->fTime > lastEventTime)
				{
					lastEventTime = pEvent->fTime;
				} 

				// todo:would like to validate that the effect exists
				if (!pEvent->swapSkinColor && !pEvent->pchEffect && !IS_HANDLE_ACTIVE(pEvent->hBoneDef))
				{
					ErrorFilenamef(pResourceName, "Event has no effect and/or no bone found.");
				}
			}
			FOR_EACH_END

			if (lastEventTime > pDef->fTotalTime)
			{
				ErrorFilenamef(pResourceName, "Bad or unspecified total transformation time- Events triggering past the transformation 'TotalTime'.");
				pDef->fTotalTime = lastEventTime + 2.f/30.f;
			}
		}
		return VALIDATE_HANDLED;
	}
	
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterTransformationDict(void)
{
	s_TransformationDict = RefSystem_RegisterSelfDefiningDictionary("TransformationDef", false, parse_TransformationDef, true, false, NULL);
	resDictManageValidation(s_TransformationDict, Transformation_Validate);
}

// ---------------------------------------------------------------------------------------------------
AUTO_STARTUP(Transformation);
void Transformation_Startup(void)
{
	// Don't load on app servers, other than login server and auction server
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer()) {
		return;
	}

	resLoadResourcesFromDisk(s_TransformationDict, "defs/transformation", ".xfm", "Transformation.bin", 
								PARSER_FORCEREBUILD | RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
}

// ---------------------------------------------------------------------------------------------------
void Transformation_SetTransformation(Entity *e, const char *pchTransformDef)
{
	PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(e);
	if (pCostume)
	{
		if (!e->costumeRef.pTransformation)
		{
			e->costumeRef.pTransformation = calloc(1, sizeof(CostumeTransformation));
			if (!e->costumeRef.pTransformation)
				return;
		}

		REF_HANDLE_SET_FROM_STRING(s_TransformationDict, pchTransformDef, e->costumeRef.pTransformation->hDef);
		if (IS_HANDLE_ACTIVE(e->costumeRef.pTransformation->hDef))
		{
			e->costumeRef.transformation++;
			e->costumeRef.pTransformation->pSourceCostume = StructClone(parse_PlayerCostume, pCostume);
		}
	}
}


// ---------------------------------------------------------------------------------------------------
void Transformation_Destroy(CostumeTransformation **ppTrans)
{
	CostumeTransformation *pTrans = *ppTrans;
	if (pTrans)
	{
		if (pTrans->pCurrentCostume)
			StructDestroy(parse_PlayerCostume, pTrans->pCurrentCostume);
		if (pTrans->pSourceCostume)
			StructDestroy(parse_PlayerCostume, pTrans->pSourceCostume);

		REF_HANDLE_REMOVE(pTrans->hDef);

		free(pTrans);
		*ppTrans = NULL;
	}
}



#include "TransformationCommon_h_ast.c"