/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "NumericConversionCommon.h"
#include "ResourceManager.h"
#include "GameBranch.h"
#include "file.h"
#include "itemCommon.h"
#include "timing.h"
#include "Entity.h"
#include "GamePermissionsCommon.h"
#include "Player.h"
#include "GameAccountDataCommon.h"

#include "AutoGen/NumericConversionCommon_h_ast.h"

#include "error.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_NumericConversionDictionary = NULL;

AUTO_TRANS_HELPER;
S32
NumericConversion_trh_GetBonusQuantity(ATH_ARG NOCONST(Entity) *pEnt, NumericConversionDef *conversionDef, GameAccountDataExtract* pExtract)
{
	int i;
	S32 sReturn = 0;

	for(i=0;i<eaSize(&conversionDef->BonusDefs);i++)
	{
		if(conversionDef->BonusDefs[i]->GamePermissionValue)
		{
			char *estrBuffer = NULL;
			bool ret = false;

			GenerateGameTokenKey(&estrBuffer,
				kGameToken_Inv,
				GAME_PERMISSION_OWNED,
				conversionDef->BonusDefs[i]->GamePermissionValue);

			if (pExtract)
				ret = (eaIndexedGetUsingString(&pExtract->eaTokens, estrBuffer) != NULL);

			if(ret == false)
				continue;
		}

		sReturn += conversionDef->BonusDefs[i]->bonusQuantity;
	}

	return sReturn;
}

S32
NumericConversion_QuantityRemaining(Entity *pEnt, NumericConversionState * const * const * hConversionStates, NumericConversionDef *conversionDef)
{
    U32 lastConversionTime = 0;
    S32 lastConversionQuantity = 0;
    U32 curTime = timeSecondsSince2000();
    U32 startOfCurrentInterval;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S32 sQuantityPerInterval = conversionDef->quantityPerInterval + NumericConversion_GetBonusQuantity(pEnt, conversionDef, pExtract);
	NumericConversionState *conversionState = eaIndexedGetUsingString(hConversionStates, conversionDef->name);

    if ( conversionState )
    {
        lastConversionTime = conversionState->lastConversionTime;
        lastConversionQuantity = conversionState->quantityConverted;
    }

    // compute the start time of the current time interval
    startOfCurrentInterval = ( curTime / conversionDef->timeIntervalSeconds ) * conversionDef->timeIntervalSeconds;

    if ( lastConversionTime < startOfCurrentInterval )
    {
        // last conversion was in a previous interval
        return sQuantityPerInterval;
    }

    if ( sQuantityPerInterval >= lastConversionQuantity )
    {
        return sQuantityPerInterval - lastConversionQuantity;
    }

    // if we get here it means that a previous conversion was too large
	ErrorDetailsf("EntityID=%u, curTime=%u, lastConversionTime=%u, lastConversionQuantity=%d, conversionName=%s", pEnt->myContainerID, curTime, lastConversionTime, lastConversionQuantity, conversionDef->name);
    Errorf("Numeric conversion state quantity is too large");

    return 0;
}

bool 
NumericConversion_Validate(NumericConversionDef *pDef)
{
    if( !resIsValidName(pDef->name) )
    {
        ErrorFilenamef( pDef->filename, "NumericConversion name is illegal: '%s'", pDef->name );
        return 0;
    }

    if (!GET_REF(pDef->hSourceNumeric) && REF_STRING_FROM_HANDLE(pDef->hSourceNumeric)) {
        ErrorFilenamef(pDef->filename, "NumericConversion references non-existent source numeric item '%s'", REF_STRING_FROM_HANDLE(pDef->hSourceNumeric));
    }
    else
    {
        ItemDef *sourceItemDef = GET_REF(pDef->hSourceNumeric);
        if ( sourceItemDef->eType != kItemType_Numeric )
        {
            ErrorFilenamef(pDef->filename, "NumericConversion references source item that is not a numeric '%s'", REF_STRING_FROM_HANDLE(pDef->hSourceNumeric));
        }
    }

    if (!GET_REF(pDef->hDestNumeric) && REF_STRING_FROM_HANDLE(pDef->hDestNumeric)) {
        ErrorFilenamef(pDef->filename, "NumericConversion references non-existent destination numeric item '%s'", REF_STRING_FROM_HANDLE(pDef->hDestNumeric));
    }
    else
    {
        ItemDef *destItemDef = GET_REF(pDef->hDestNumeric);
        if ( destItemDef->eType != kItemType_Numeric )
        {
            ErrorFilenamef(pDef->filename, "NumericConversion references destination item that is not a numeric '%s'", REF_STRING_FROM_HANDLE(pDef->hDestNumeric));
        }
    }

    if ( pDef->timeIntervalSeconds == 0 )
    {
        ErrorFilenamef(pDef->filename, "NumericConversion time interval must be non-zero");
    }

    if ( pDef->quantityPerInterval <= 0 )
    {
        ErrorFilenamef(pDef->filename, "NumericConversion quantity per interval must be greater than zero");
    }

    return 1;
}

static int 
NumericConversion_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, NumericConversionDef *pNumericConversion, U32 userID)
{
    switch(eType)
    {
        xcase RESVALIDATE_POST_TEXT_READING:	
            NumericConversion_Validate(pNumericConversion);
            return VALIDATE_HANDLED;
        xcase RESVALIDATE_FIX_FILENAME:
            {
                char *pchPath = NULL;
                resFixPooledFilename(&pNumericConversion->filename, GameBranch_GetDirectory(&pchPath, "defs/numericConversion"), NULL, pNumericConversion->name, ".numericConversion");
                estrDestroy(&pchPath);
            }
            return VALIDATE_HANDLED;
    }
    return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int 
RegisterNumericConversionDictionary(void)
{

    g_NumericConversionDictionary = RefSystem_RegisterSelfDefiningDictionary("NumericConversion", false, parse_NumericConversionDef, true, true, NULL);

    if (IsGameServerSpecificallly_NotRelatedTypes())
    {
        resDictManageValidation(g_NumericConversionDictionary, NumericConversion_ResValidateCB);
    }
    if (IsServer())
    {
        resDictProvideMissingResources(g_NumericConversionDictionary);
        if (isDevelopmentMode() || isProductionEditMode()) {
            resDictMaintainInfoIndex(g_NumericConversionDictionary, ".Name", NULL, NULL, NULL, NULL);
        }
    } 
    else if (IsClient())
    {
        resDictRequestMissingResources(g_NumericConversionDictionary, 8, false, resClientRequestSendReferentCommand);
    }

    return 1;
}

void 
NumericConversion_Load(void)
{
    char *pchPath = NULL;
    char *pchBinFile = NULL;

    resLoadResourcesFromDisk(g_NumericConversionDictionary, 
        GameBranch_GetDirectory(&pchPath, "defs/numericConversion"), ".numericConversion",
        GameBranch_GetFilename(&pchBinFile, "NumericConversion.bin"), 
        PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

    estrDestroy(&pchPath);
    estrDestroy(&pchBinFile);
}

AUTO_STARTUP(NumericConversion) ASTRT_DEPS(Items);
void 
NumericConversion_LoadDefs(void)
{
    NumericConversion_Load();
}

NumericConversionDef* 
NumericConversion_DefFromName(const char* numericConversionName)
{
    if (numericConversionName)
    {
        return (NumericConversionDef*)RefSystem_ReferentFromString(g_NumericConversionDictionary, numericConversionName);
    }
    return NULL;
}

#include "NumericConversionCommon_h_ast.c"
