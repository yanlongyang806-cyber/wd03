/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "missionset_common.h"

#include "Error.h"
#include "Estring.h"
#include "file.h"
#include "GlobalTypes.h"
#include "ResourceManager.h"

#include "missionset_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ----------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------

DictionaryHandle g_MissionSetDictionary = NULL;

// ----------------------------------------------------------------------------
//  Auto-runs/Start-up code
// ----------------------------------------------------------------------------

static int missionset_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, MissionSet *pSet, U32 userID)
{
	switch(eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		missionset_Validate(pSet);
		return VALIDATE_HANDLED;
	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename(&pSet->pchFilename, MISSIONSET_BASE_DIR, pSet->pchScope, pSet->pchName, MISSIONSET_EXTENSION);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterMissionSetDictionary(void)
{
	g_MissionSetDictionary = RefSystem_RegisterSelfDefiningDictionary("MissionSet", false, parse_MissionSet, true, true, NULL);

	resDictManageValidation(g_MissionSetDictionary, missionset_ResValidateCB);

	if (IsServer())
	{
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_MissionSetDictionary, ".name", ".scope", NULL, ".notes", NULL);
		}

		resDictProvideMissingResources(g_MissionSetDictionary);
	} 
	else
	{
		resDictRequestMissingResources(g_MissionSetDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

// ----------------------------------------------------------------------------
//  Data validation
// ----------------------------------------------------------------------------

bool missionset_Validate(MissionSet *pSet)
{
	char *estrBuffer = NULL;
	bool bSuccess = true;
	int i;
	estrStackCreate(&estrBuffer);

	// Basic Name/Scope validation
	if( !resIsValidName(pSet->pchName) ){
		ErrorFilenamef( pSet->pchFilename, "MissionSet name is illegal: '%s'", pSet->pchName );
		bSuccess = false;
	}

	if( !resIsValidScope(pSet->pchScope) ){
		ErrorFilenamef( pSet->pchFilename, "MissionSet scope is illegal: '%s'", pSet->pchScope );
		bSuccess = false;
	}

	if (IsServer()){
		for (i = eaSize(&pSet->eaEntries)-1; i >= 0; --i){
			if (!GET_REF(pSet->eaEntries[i]->hMissionDef)){
				if (REF_STRING_FROM_HANDLE(pSet->eaEntries[i]->hMissionDef)){
					ErrorFilenamef( pSet->pchFilename, "MissionSet references invalid mission '%s'.", REF_STRING_FROM_HANDLE(pSet->eaEntries[i]->hMissionDef));
					bSuccess = false;
				} else {
					ErrorFilenamef( pSet->pchFilename, "MissionSet has an empty entry." );
					bSuccess = false;
				}
			}
		}
	}

	estrDestroy(&estrBuffer);
	return bSuccess;
}


#include "AutoGen/missionset_common_h_ast.c"
