
#include "DoorTransitionCommon.h"
#include "error.h"
#include "file.h"
#include "RegionRules.h"
#include "ResourceManager.h"

#include "AutoGen/DoorTransitionCommon_h_ast.h"
#include "AutoGen/DoorTransitionCommon_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


#define DOOR_TRANS_EXT "trans"
#define DOOR_TRANS_BASE_DIR "defs/Transitions"

DictionaryHandle g_hDoorTransitionDict = NULL;


static void DoorTransitionSequenceDef_Validate(DoorTransitionSequenceDef* pDef)
{
	int i;

	if (!resIsValidName(pDef->pchName))
	{
		ErrorFilenamef(pDef->pchFileName, "Door Transition Sequence '%s' has an invalid name", 
			pDef->pchName);
	}

	if (!resIsValidScope(pDef->pchScope)) 
	{
		ErrorFilenamef(pDef->pchFileName, "Door Transition Sequence '%s' has an invalid scope '%s'.", 
			pDef->pchName, pDef->pchScope);
	}

	for (i = eaSize(&pDef->eaSequences)-1; i >= 0; i--)
	{
		DoorTransitionSequence* pTransSequence = pDef->eaSequences[i];

		if (IS_HANDLE_ACTIVE(pTransSequence->hAllegiance) && !GET_REF(pTransSequence->hAllegiance))
		{
			ErrorFilenamef(pDef->pchFileName, "Door Transition Sequence '%s' has an invalid allegiance '%s'.", 
				pDef->pchName, REF_STRING_FROM_HANDLE(pTransSequence->hAllegiance));
		}
		if (IS_HANDLE_ACTIVE(pTransSequence->hCutscene) && !GET_REF(pTransSequence->hCutscene))
		{
			ErrorFilenamef(pDef->pchFileName, "Door Transition Sequence '%s' has an invalid cutscene '%s'.", 
				pDef->pchName, REF_STRING_FROM_HANDLE(pTransSequence->hCutscene));
		}
		if (pTransSequence->pAnimation && IS_HANDLE_ACTIVE(pTransSequence->pAnimation->hAnimList) && !GET_REF(pTransSequence->pAnimation->hAnimList))
		{
			ErrorFilenamef(pDef->pchFileName, "Door Transition Sequence '%s' has an invalid anim list '%s'.", 
				pDef->pchName, REF_STRING_FROM_HANDLE(pTransSequence->pAnimation->hAnimList));
		}
		if (pTransSequence->pchMovie && pTransSequence->pchMovie[0] &&
			((pTransSequence->pAnimation && IS_HANDLE_ACTIVE(pTransSequence->pAnimation->hAnimList)) ||
			 IS_HANDLE_ACTIVE(pTransSequence->hCutscene)))
		{
			ErrorFilenamef(pDef->pchFileName, "Door Transition Sequence '%s' has an animation or cutscene and a movie specified.", pDef->pchName);
		}
	}
}

static int DoorTransitionSequenceDef_ValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, DoorTransitionSequenceDef *pDef, U32 userID)
{
	switch(eType)
	{
		xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
			resFixPooledFilename(&pDef->pchFileName, DOOR_TRANS_BASE_DIR, pDef->pchScope, pDef->pchName, DOOR_TRANS_EXT);
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES:
			DoorTransitionSequenceDef_Validate(pDef);
			return VALIDATE_HANDLED;
	}		
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterDoorTransitionSequenceDefDict(void)
{
	// Set up reference dictionaries
	g_hDoorTransitionDict = RefSystem_RegisterSelfDefiningDictionary("DoorTransitionSequenceDef",false, parse_DoorTransitionSequenceDef, true, true, NULL);

	resDictManageValidation(g_hDoorTransitionDict, DoorTransitionSequenceDef_ValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hDoorTransitionDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hDoorTransitionDict, ".Name", ".Scope", NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hDoorTransitionDict, 8, false, resClientRequestSendReferentCommand);
	}
}

AUTO_STARTUP(DoorTransitionSequence) ASTRT_DEPS(AnimLists, Cutscenes, Allegiance);
void LoadDoorTransitionSequenceDefs(void)
{
	if (IsGameServerBasedType()) {
		resLoadResourcesFromDisk(g_hDoorTransitionDict,
								 DOOR_TRANS_BASE_DIR,
								 ".trans",
								 NULL,
								 RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
}

DoorTransitionSequenceDef* DoorTransitionSequence_DefFromName(const char* pchName)
{
	if (pchName)
		return RefSystem_ReferentFromString(g_hDoorTransitionDict, pchName);
	return NULL;
}