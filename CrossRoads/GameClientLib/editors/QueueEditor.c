//
// QueueEditor.c
//

#ifndef NO_EDITORS

#include "ActivityCommon.h"
#include "entCritter.h"
#include "EString.h"
#include "gameeditorshared.h"
#include "mission_common.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ResourceSearch.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "StringCache.h"
#include "PvPGameCommon.h"
#include "CharacterClass.h"

//#include "WorldGrid.h"
//#include "../StaticWorld/WorldGridPrivate.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/queue_common_h_ast.h"
#include "AutoGen/queue_common_structs_h_ast.h"
#include "AutoGen/CharacterClass_h_ast.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define QE_GROUP_MAIN				"Main"
#define QE_GROUP_REQUIRES			"Requirements"
#define QE_SUBGROUP_LEVELBAND_VAR	"LevelBandWorldVar"
#define QE_SUBGROUP_LEVELBAND		"LevelBand"
#define QE_SUBGROUP_GROUPDEF		"Group"
#define QE_SUBGROUP_WORLDVAR		"WorldVariable"
#define QE_SUBGROUP_QUEUEVAR		"QueueVariableData"
#define QE_SUBGROUP_GAMERULES		"GameRules"
#define QE_SUBGROUP_TRACKEDEVENT	"TrackedEvent"
#define QE_SUBGROUP_CUSTOMMAPDATA	"CustomMapData"
#define QE_SUBGROUP_REWARDS			"QueueRewards"

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *qeWindow = NULL;
static int qeGroupId = 0;
static int qeLevelBandID = 0;
static int qeWorldVarId = 0;
static int qeQueueVarId = 0;
static int s_RewardTableID = 0;
static int s_TrackedEventID = 0;
static int s_CustomMapDataID = 0;

//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------


static int qe_validateCallback(METable *pTable, QueueDef *pQueueDef, void *pUserData)
{

	return queue_Validate(pQueueDef);
}

static void *qe_createQueueLevelBandEditorData(METable *pTable, QueueDef *pQueueDef, QueueLevelBandEditorData *pDataToClone, QueueLevelBandEditorData *pBeforeData, QueueLevelBandEditorData *pAfterData)
{
	QueueLevelBandEditorData *pNewData = NULL;

	// Allocate the object
	if (pDataToClone) {
		pNewData = (QueueLevelBandEditorData*)StructClone(parse_QueueLevelBandEditorData, pDataToClone);
	} else {
		pNewData = (QueueLevelBandEditorData*)StructCreate(parse_QueueLevelBandEditorData);
	}

	assertmsg(pNewData, "Failed to create queue editor data");

	return pNewData;
}

static void qe_UsePrivateGroupCB(METable *pTable, QueueDef *pQueueDef, QueueGroupDef *pData, void *pUserData, bool bInitNotify)
{
	if(pData->bUseGroupPrivateSettings)
	{
		METableSetSubFieldNotApplicable(pTable, pQueueDef, qeGroupId, pData, "Private Min Group Size", 0);
		METableSetSubFieldNotApplicable(pTable, pQueueDef, qeGroupId, pData, "Private Min Overtime Size", 0);
	}
	else
	{
		METableSetSubFieldNotApplicable(pTable, pQueueDef, qeGroupId, pData, "Private Min Group Size", 1);
		METableSetSubFieldNotApplicable(pTable, pQueueDef, qeGroupId, pData, "Private Min Overtime Size", 1);
	}
}

static void qe_changeQueueLevelBand(METable *pTable, QueueDef *pQueueDef, QueueLevelBandEditorData *pData, void *pUserData, bool bInitNotify)
{
	int i;

	if ( pData->pWorldVar )
	{
		switch ( pData->pWorldVar->eType )
		{
			case WVAR_NONE:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 1);
				break;
			}
			case WVAR_INT:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 0);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 1);
				break;
			}
			case WVAR_FLOAT:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 0);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 1);
				break;
			}
			case WVAR_STRING:
			case WVAR_LOCATION_STRING:
			case WVAR_ANIMATION:
			case WVAR_ITEM_DEF:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 0);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 1);
				break;
			}
			case WVAR_MESSAGE:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 0);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 1);
				break;
			}
			case WVAR_CRITTER_DEF:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 0);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 1);
				break;
			}
			case WVAR_CRITTER_GROUP:
			{
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Int", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Float", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "String", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "Message", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterDef", 1);
				METableSetSubFieldNotApplicable(pTable, pQueueDef, qeLevelBandID, pData, "CritterGroup", 0);
				break;
			}
		}
	}

	for( i = 0; i < eaSize(&pQueueDef->eaEditorData); i++ )
	{
		QueueLevelBandEditorData* pCurrData = pQueueDef->eaEditorData[i];
		
		if ( pCurrData->iLevelBandIndex == pData->iLevelBandIndex && pCurrData != pData )
		{
			//Copy all existing information
			StructCopyAll( parse_QueueLevelBand, pData->pLevelBand, pCurrData->pLevelBand );
		}
	}
}

static	qeGameRuleGroup **s_eaGameRuleGroups;

static char *s_BadString = {""};

char *GetGameRuleGroupName(PVPGameType eType)
{
	if(eType <0 || eType >= kPVPGameType_Maximum)
	{
		return s_BadString;
	}

	while(eType >= eaSize(&s_eaGameRuleGroups))
	{
		qeGameRuleGroup *pRule = StructCreate(parse_qeGameRuleGroup);
		eaPush(&s_eaGameRuleGroups, pRule);
	}

	if(!s_eaGameRuleGroups[eType]->groupName)
	{
		estrPrintf(&s_eaGameRuleGroups[eType]->groupName, "%dGameRule", eType);
	}

	return s_eaGameRuleGroups[eType]->groupName;
}

static void qe_UsePrivateCB(METable *pTable, QueueDef *pQueueDef, void *pUserData, bool bInitNotify)
{
	if(pQueueDef->Limitations.bUsePrivateSettings)
	{
		METableSetFieldNotApplicable(pTable, pQueueDef, "Private MaxWaitTime", 0);
		METableSetFieldNotApplicable(pTable, pQueueDef, "Private MinMembersAllGroups", 0);
	}
	else
	{
		METableSetFieldNotApplicable(pTable, pQueueDef, "Private MaxWaitTime", 1);
		METableSetFieldNotApplicable(pTable, pQueueDef, "Private MinMembersAllGroups", 1);
	}
}

static void qe_changeGameRuleCB(METable *pTable, QueueDef *pQueueDef, void *pUserData, bool bInitNotify)
{
	bool bChanged = false;
	static PVPGameType lastType = kPVPGameType_None;

	if(pQueueDef->MapRules.QGameRules.publicRules.eGameType != lastType || bInitNotify)
	{
		bChanged = true;
		lastType = pQueueDef->MapRules.QGameRules.publicRules.eGameType;
		METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_Domination), true);
		METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_Deathmatch), true);
		METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_LastManStanding), true);
		METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), true);

		switch(pQueueDef->MapRules.QGameRules.publicRules.eGameType)
		{
			case kPVPGameType_CaptureTheFlag:
			{
				METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), false);
				break;
			}
			case kPVPGameType_Deathmatch:
			{
				METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_Deathmatch), false);
				break;
			}
			case kPVPGameType_Domination:
			{
				METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_Domination), false);
				break;
			}
			case kPVPGameType_LastManStanding:
			{
				METableSetGroupNotApplicable(pTable, pQueueDef, GetGameRuleGroupName(kPVPGameType_LastManStanding), false);
				break;
			}
		}
	}

	if(bChanged && !bInitNotify)
	{
		// If we changed the data, we have to regenerate the row
		METableRegenerateRow(pTable, pQueueDef);
	}

}

static void *qe_createQueueGroupDef(METable *pTable, QueueDef *pQueueDef, QueueGroupDef *pGroupToClone, QueueGroupDef *pBeforeGroup, QueueGroupDef *pAfterGroup)
{
	QueueGroupDef *pNewGroup;
	
	// Allocate the object
	if (pGroupToClone) {
		pNewGroup = (QueueGroupDef*)StructClone(parse_QueueGroupDef, pGroupToClone);
	} else {
		pNewGroup = (QueueGroupDef*)StructCreate(parse_QueueGroupDef);
	}
	
	assertmsg(pNewGroup, "Failed to create queue item");
	
	return pNewGroup;
}

static void *qe_createQueueWorldVar(METable *pTable, WorldVariable *pWorldVar, WorldVariable *pVarToClone, WorldVariable *pBeforeVar, WorldVariable *pAfterVar)
{
	WorldVariable *pNewVar;
	
	// Allocate the object
	if (pVarToClone) {
		pNewVar = (WorldVariable*)StructClone(parse_WorldVariable, pVarToClone);
	} else {
		pNewVar = (WorldVariable*)StructCreate(parse_WorldVariable);
	}
	
	assertmsg(pNewVar, "Failed to create world var");
	
	return pNewVar;
}

static void *qe_createQueueVarData(METable *pTable, QueueVariableData *pQueueData, QueueVariableData *pDataToClone, QueueVariableData *pBeforeData, QueueVariableData *pAfterData)
{
	QueueVariableData *pNewData;
	
	// Allocate the object
	if (pDataToClone) {
		pNewData = (QueueVariableData*)StructClone(parse_QueueVariableData, pDataToClone);
	} else {
		pNewData = (QueueVariableData*)StructCreate(parse_QueueVariableData);
	}
	
	assertmsg(pNewData, "Failed to create queue var data");
	
	return pNewData;
}

static void QEFixMessage(Message *pmsg, const char *pchQueueName, const char *pchKey, const char *pchDesc, const char *pchScope)
{
	char buf[1024];

	sprintf(buf, "QueueDef.%s.%s", pchKey, pchQueueName);
	pmsg->pcMessageKey = allocAddString(buf);

	StructFreeString(pmsg->pcDescription);
	pmsg->pcDescription = StructAllocString(pchDesc);

	pmsg->pcScope = allocAddString(pchScope);

	// Leave pcFilename alone
}

static void qe_fixMessages(QueueDef *pQueueDef)
{
	int i;
	char *pchScope = NULL;
	char pchBuffer[512];

	estrStackCreate(&pchScope);

	estrPrintf(&pchScope,"QueueDef");
	if(pQueueDef->pchScope)
	{
		char *p = NULL;
		estrConcatf(&pchScope,"/%s",pQueueDef->pchScope);
		while((p = strchr(pchScope,'.')) != NULL)
		{
			*p = '/';
		}
	}

	QEFixMessage(pQueueDef->displayNameMesg.pEditorCopy,pQueueDef->pchName,"Name","Queue name",pchScope);
	QEFixMessage(pQueueDef->descriptionMesg.pEditorCopy,pQueueDef->pchName,"Desc","Description of the Queue",pchScope);

	for (i = eaSize(&pQueueDef->eaGroupDefs)-1; i >= 0; i--)
	{
		sprintf(pchBuffer, "GroupName%d", i);
		QEFixMessage(pQueueDef->eaGroupDefs[i]->DisplayName.pEditorCopy,pQueueDef->pchName,pchBuffer,"Queue group name",pchScope);
	}

	for (i = eaSize(&pQueueDef->MapSettings.VarData.eaQueueData)-1; i >= 0; i--)
	{
		QueueVariableData* pVarData = pQueueDef->MapSettings.VarData.eaQueueData[i];
		if (pVarData->pSettingData)
		{
			if (!pVarData->pSettingData->msgDisplayName.pEditorCopy)
			{
				pVarData->pSettingData->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
			}
			sprintf(pchBuffer, "QueueVarDataName%d", i);
			QEFixMessage(pVarData->pSettingData->msgDisplayName.pEditorCopy,pQueueDef->pchName,pchBuffer,"Queue Var Data name",pchScope);
		}
	}

	for(i= 0; i < eaSize(&pQueueDef->QueueMaps.eaCustomMapTypes); ++i)
	{
		if (!pQueueDef->QueueMaps.eaCustomMapTypes[i]->msgDisplayName.pEditorCopy)
		{
			pQueueDef->QueueMaps.eaCustomMapTypes[i]->msgDisplayName.pEditorCopy = StructCreate(parse_Message);
		}
		sprintf(pchBuffer, "QeCustomName%d", i);
		QEFixMessage(pQueueDef->QueueMaps.eaCustomMapTypes[i]->msgDisplayName.pEditorCopy,pQueueDef->pchName,pchBuffer,"Queue Custom Name",pchScope);
	}

	estrDestroy(&pchScope);
}

static void qe_postOpenCallback(METable *pTable, QueueDef *pQueueDef, QueueDef *pOrigQueueDef)
{
	int i, j;

	qe_fixMessages(pQueueDef);
	
	if (pOrigQueueDef) 
	{
		qe_fixMessages(pOrigQueueDef);

		if (pOrigQueueDef->eaEditorData)
			return;

		for (i = 0; i < eaSize(&pOrigQueueDef->eaLevelBands); i++ )
		{
			QueueLevelBand* pLevelBand = pOrigQueueDef->eaLevelBands[i];

			if ( eaSize(&pLevelBand->VarData.eaWorldVars) )
			{
				for (j = 0; j < eaSize(&pLevelBand->VarData.eaWorldVars); j++ )
				{
					WorldVariable* pWorldVar = pLevelBand->VarData.eaWorldVars[j];

					QueueLevelBandEditorData* pOrigEditorData = StructCreate(parse_QueueLevelBandEditorData);
					QueueLevelBandEditorData* pNewEditorData = NULL;

					pOrigEditorData->pWorldVar = StructClone(parse_WorldVariable, pWorldVar);
					pOrigEditorData->pLevelBand = StructClone(parse_QueueLevelBand, pLevelBand);
					pOrigEditorData->iLevelBandIndex = i;

					eaDestroyStruct(&pOrigEditorData->pLevelBand->VarData.eaWorldVars,parse_WorldVariable);

					eaPush(&pOrigQueueDef->eaEditorData,pOrigEditorData);

					pNewEditorData = StructClone(parse_QueueLevelBandEditorData, pOrigEditorData);

					eaPush(&pQueueDef->eaEditorData,pNewEditorData);
				}
			}
			else
			{
				QueueLevelBandEditorData* pOrigEditorData = StructCreate(parse_QueueLevelBandEditorData);
				QueueLevelBandEditorData* pNewEditorData = NULL;
				pOrigEditorData->pLevelBand = StructClone(parse_QueueLevelBand, pLevelBand);
				pOrigEditorData->iLevelBandIndex = i;
				eaDestroyStruct(&pOrigEditorData->pLevelBand->VarData.eaWorldVars,parse_WorldVariable);
				eaPush(&pOrigQueueDef->eaEditorData,pOrigEditorData);
				pNewEditorData = StructClone(parse_QueueLevelBandEditorData, pOrigEditorData);
				eaPush(&pQueueDef->eaEditorData,pNewEditorData);
			}
		}

		if (pOrigQueueDef->eaEditorData)
		{
			METableRegenerateRow(pTable,pQueueDef);
		}
	}

	METableRefreshRow(pTable,pQueueDef);
}

static int SortQueueLevelBandEditorData(const QueueLevelBandEditorData **ppA, const QueueLevelBandEditorData **ppB)
{
	const QueueLevelBandEditorData* pA = (*ppA);
	const QueueLevelBandEditorData* pB = (*ppB);
	return pA->iLevelBandIndex - pB->iLevelBandIndex;
}

static void qe_preSaveCallback(METable *pTable, QueueDef *pQueueDef)
{
	int i, j;

	eaQSort(pQueueDef->eaEditorData, SortQueueLevelBandEditorData);

	for (i = 0; i < eaSize(&pQueueDef->eaEditorData); i++)
	{
		int iCurrIndex = pQueueDef->eaEditorData[i]->iLevelBandIndex;
		int iPrevIndex = i>0 ? pQueueDef->eaEditorData[i-1]->iLevelBandIndex : -1;
		if ( iCurrIndex != iPrevIndex && iCurrIndex != iPrevIndex+1 )
		{
			Errorf("Missing level band index (%d), could not save.", iPrevIndex+1);
			return;
		}
	}

	eaDestroyStruct(&pQueueDef->eaLevelBands,parse_QueueLevelBand);

	for (i = 0; i < eaSize(&pQueueDef->eaEditorData); i++)
	{
		QueueLevelBandEditorData *pData = pQueueDef->eaEditorData[i];
		QueueLevelBand *pLevelBand = NULL;
		WorldVariable *pWorldVar = StructClone(parse_WorldVariable, pData->pWorldVar);

		for (j = 0; j < eaSize(&pQueueDef->eaLevelBands); j++)
		{
			if( pData->iLevelBandIndex == j )
			{
				pLevelBand = pQueueDef->eaLevelBands[j];
				break;
			}
		}

		if (!pLevelBand)
		{
			pLevelBand = StructClone(parse_QueueLevelBand, pData->pLevelBand);

			eaPush(&pQueueDef->eaLevelBands, pLevelBand);
		}

		assertmsg(pLevelBand, "Failed to create level band during save.");

		if ( pWorldVar )
		{
			eaPush(&pLevelBand->VarData.eaWorldVars, pWorldVar);
		}
	}

	qe_fixMessages(pQueueDef);
}

static void *qe_createObject(METable *pTable, QueueDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	QueueDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_QueueDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_QueueDef);

		pcBaseName = "_New_Queue";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create queue");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,"defs/queues/%s.queue",pNewDef->pchName);
	pNewDef->pchFilename = (char*)allocAddString(buf);

	return pNewDef;
}


static void *qe_tableCreateCallback(METable *pTable, QueueDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return qe_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *qe_windowCreateCallback(MEWindow *pWindow, QueueDef *pObjectToClone)
{
	return qe_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void qe_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void qe_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

			METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}


static void qe_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, qe_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, qe_validateCallback, pTable);
	METableSetCreateCallback(pTable, qe_tableCreateCallback);
	METableSetPostOpenCallback(pTable, qe_postOpenCallback);
	METableSetPreSaveCallback(pTable, qe_preSaveCallback);

	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Level Band Index", qe_changeQueueLevelBand, NULL); 
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Minimum Level", qe_changeQueueLevelBand, NULL); 
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Maximum Level", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Map Level", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Bolster Type", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Bolster Level", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Var Name", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Var Type", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Int", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Float", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "String", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "Message", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "CritterDef", qe_changeQueueLevelBand, NULL);
	METableSetSubColumnChangeCallback(pTable, qeLevelBandID, "CritterGroup", qe_changeQueueLevelBand, NULL);

	METableSetColumnChangeCallback(pTable, "Game Type", qe_changeGameRuleCB, NULL);

	METableSetColumnChangeCallback(pTable, "Use private settings", qe_UsePrivateCB, NULL);
	METableSetSubColumnChangeCallback(pTable, qeGroupId, "Private UseGroupSettings", qe_UsePrivateGroupCB, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hQueueDefDict, qe_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, qe_messageDictChangeCallback, pTable);
}

static char** qe_GetMaps(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	S32 iIdx, iSize = eaSize(&g_GEMapDispNames);
	for(iIdx = 0; iIdx < iSize; iIdx++)
	{
		eaPush(&eaResult, strdup(g_GEMapDispNames[iIdx]));
	}
	return eaResult;
}

static char** qe_GetQueueCooldowns(METable *pTable, void *pUnused)
{
	char **eaResult = NULL;
	S32 iIdx, iSize;

	queue_GetCooldownNames(&eaResult);

	iSize = eaSize(&eaResult);
	for(iIdx = 0; iIdx < iSize; iIdx++)
	{
		eaResult[iIdx] = strdup(eaResult[iIdx]);
	}
	return eaResult;
}

static char** qe_GetCustomMapTypes(METable *pTable, QueueDef* pDef)
{
	char **eaResult = NULL;
	S32 iTypeIdx, iSize = eaSize(&pDef->QueueMaps.eaCustomMapTypes);
	for (iTypeIdx = 0; iTypeIdx < iSize; iTypeIdx++)
	{
		QueueCustomMapData* pMapType = pDef->QueueMaps.eaCustomMapTypes[iTypeIdx];
		eaPush(&eaResult, strdup(pMapType->pchName));
	}
	return(eaResult);
}

static char** qe_GetActivities(METable *pTable, void *pUnused)
{
	char** eaResult = NULL;
	int i, iSize = eaSize(&g_ActivityDefs.ppDefs);

	eaSetSize(&eaResult, iSize);

	for(i = 0; i < iSize; i++)
	{
		eaResult[i] = strdup(g_ActivityDefs.ppDefs[i]->pchActivityName);
	}
	return eaResult;
}

//---------------------------------------------------------------------------------------------------
// UI Init
//---------------------------------------------------------------------------------------------------

static void qe_initQueueColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "Name", ME_STATE_NOT_PARENTABLE);
	
	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);
	
	METableAddScopeColumn(pTable,	"Scope",		"Scope",          160, QE_GROUP_MAIN, kMEFieldType_TextEntry); // Not validated on purpose
	METableAddFileNameColumn(pTable,"File Name",	"filename",       210, QE_GROUP_MAIN, NULL, "defs/queues", "defs/queues", ".queue", UIBrowseNewOrExisting);

	METableAddColumn(pTable, "Icon", "Icon", 180, QE_GROUP_MAIN, kMEFieldType_Texture, NULL, NULL, NULL, NULL, "texture_library/ui/Icons", NULL, NULL);

	METableAddSimpleColumn(pTable,	"Display Name", ".displayNameMesg.EditorCopy", 160, QE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,	"Description",	".descriptionMesg.EditorCopy", 160, QE_GROUP_MAIN, kMEFieldType_Message);

	METableAddEnumColumn(pTable, "Category", "Category", 120, QE_GROUP_MAIN, kMEFieldType_Combo, QueueCategoryEnum);

	METableAddSimpleColumn(pTable, "Map Level",		"OverrideMapLevel",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Map Difficulty", "MapDifficulty",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddEnumColumn(pTable, "Bolster Type",	"BolsterType",  150, QE_GROUP_MAIN, kMEFieldType_Combo, BolsterTypeEnum);

	METableAddSimpleColumn(pTable, "Min Members", "MinMembersAllGroups",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Max Local Teams Per Group", "MaxLocalTeamsPerGroup", 100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Max Wait Time",	"MaxTimeToWait",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Join Time Limit","JoinTimeLimit",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Mission Return Logout Time","MissionReturnLogoutTime", 100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Leaver Penalty Duration","PenaltyDuration", 100, QE_GROUP_MAIN, kMEFieldType_TextEntry);
	
	METableAddSimpleColumn(pTable, "Leaver Penalty Min Group Member Count","LeaverPenaltyMinGroupMemberCount", 100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable, "Player Disconnect Timeout","PlayerLimboTimeout", 100, QE_GROUP_MAIN, kMEFieldType_TextEntry);
	
	METableAddColumn(pTable,	   "Maps",		"MapName",	100, QE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, qe_GetMaps);

	METableAddEnumColumn(pTable,   "Map Type",	"MapType",	150, QE_GROUP_MAIN, kMEFieldType_Combo, ZoneMapTypeEnum);

	METableAddDictColumn(pTable,   "Neutral Faction", "NeutralFaction", 120, QE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, "CritterFaction", parse_CritterFaction, "Name");

	METableAddColumn(pTable,	   "Cooldown", "CooldownDef",	100, QE_GROUP_MAIN, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, qe_GetQueueCooldowns);

	METableAddExprColumn(pTable,   "Req. Expression",	"Requires",		160, QE_GROUP_REQUIRES, queue_GetContext(NULL));
	METableAddDictColumn(pTable,   "Req. Mission",	"RequiredMission", 120, QE_GROUP_REQUIRES, kMEFieldType_ValidatedTextEntry, "Mission", parse_MissionDef, "name");

	METableAddEnumColumn(pTable,   "Req. Mission State", "MissionReqFlags", 120, QE_GROUP_REQUIRES, kMEFieldType_FlagCombo, QueueMissionReqEnum);
	METableAddSimpleColumn(pTable, "Req. Mission No Access",	"MissionReqNoAccess", 120, QE_GROUP_REQUIRES, kMEFieldType_BooleanCombo);
	
	METableAddSimpleColumn(pTable, "Require Same Guild", "RequireSameGuild", 120, QE_GROUP_REQUIRES, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Require Any Guild",	 "RequireAnyGuild", 120, QE_GROUP_REQUIRES, kMEFieldType_BooleanCombo);

	METableAddColumn(pTable,	   "Required Activity", "RequiredActivity", 180, QE_GROUP_REQUIRES, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, qe_GetActivities);
	
	METableAddGlobalDictColumn(pTable,   "Required Event",	"RequiredEvent", 120, QE_GROUP_REQUIRES, kMEFieldType_ValidatedTextEntry, "Event", "resourceName");
	
	METableAddGlobalDictColumn(pTable, "Classes Required", "ClassRequired", 140, QE_GROUP_REQUIRES, kMEFieldType_ValidatedTextEntry, "CharacterClass", "resourceName");
	METableAddEnumColumn(pTable, "Class Categories Required", "ClassCategoryRequired", 140, QE_GROUP_REQUIRES, kMEFieldType_FlagCombo, CharClassCategoryEnum);

	METableAddSimpleColumn(pTable, "Gear Rating", "RequiredGearRating", 0, QE_GROUP_MAIN, kMEFieldType_TextEntry);
	
	METableAddSimpleColumn(pTable, "Public",	"Public",		120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "AlwaysCreate",	"AlwaysCreate",	120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Debug",		"Debug",		120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "SplitTeams","SplitTeams",	120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Random Map","RandomMap",	120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Validate Offers","CheckOffersBeforeMapLaunch", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Disable Auto-Balance","DisableAutoBalance",	120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Challenge Match", "ChallengeMatch", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Enable Auto-Team","EnableAutoTeam", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Enable Team-Up","EnableTeamUp", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Bolster To Map Level","BolsterToMapLevel", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Ignore LevelBands For Teams","IgnoreLevelBandsForTeams", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Enable Leaver Penalty","EnableLeaverPenalty", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Destroy Empty Maps","DestroyEmptyMaps", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Allow Concede","AllowConcede", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Allow Vote Kick","AllowVoteKick", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);

	METableAddEnumColumn(pTable, 		"Game Type",		"GameType",				120, QE_SUBGROUP_GAMERULES, kMEFieldType_Combo, PVPGameTypeEnum);
	METableAddSimpleColumn(pTable, 		"Scoreboard",		"Scoreboard",			150, QE_SUBGROUP_GAMERULES, kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Point Max",		"PointMax",				150, QE_SUBGROUP_GAMERULES, kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Disable Respawn",	"DisableNaturalRespawn",150, QE_SUBGROUP_GAMERULES, kMEFieldType_BooleanCombo );

	METableAddEnumColumn(pTable, "UI Reward", "Reward", 120, QE_GROUP_MAIN, kMEFieldType_Combo, QueueRewardEnum);
	METableAddEnumColumn(pTable, "UI Category", "Category", 120, QE_GROUP_MAIN, kMEFieldType_Combo, QueueCategoryEnum);
	METableAddEnumColumn(pTable, "UI Difficulty", "Difficulty", 120, QE_GROUP_MAIN, kMEFieldType_Combo, QueueDifficultyEnum);
	METableAddSimpleColumn(pTable, "Expected Game Time","ExpectedGameTime", 150, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	// private limits data
	METableAddSimpleColumn(pTable, "Use private settings","UsePrivateSettings", 120, QE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Private MaxWaitTime", "PrivateMaxWaitTime",  120, QE_GROUP_MAIN, kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, "Private MinMembersAllGroups", "PrivateMinMembersAllGroups",  150, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	// death match
	METableAddSimpleColumn(pTable, 		"Suicide Penalty",	"SuicidePenality",		150, GetGameRuleGroupName(kPVPGameType_Deathmatch), kMEFieldType_BooleanCombo);

	// last man standing
	METableAddSimpleColumn(pTable, 		"Round Time",		"RoundTime",			150, GetGameRuleGroupName(kPVPGameType_LastManStanding), kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable, 		"Interval Time",	"IntervalTime",			150, GetGameRuleGroupName(kPVPGameType_LastManStanding), kMEFieldType_TextEntry);

	// capture the flag
	METableAddSimpleColumn(pTable, 		"Capture time",		"CaptureTime",			150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Recycle time",		"RecycleTime",			150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Friendly bonus",	"FriendlyBonus",		150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Point time",		"PointTime",			150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Capture distance",	"CaptureDistance",		150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Capture point name",	"CapturePointName",	150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Flag Power",		"FlagPower",			150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Recharge Power Category",	"RechargePowerCategories",150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_TextEntry );
	METableAddDictColumn(pTable,   		"Flag Carrier Class","FlagCarrierClass",	150, GetGameRuleGroupName(kPVPGameType_CaptureTheFlag), kMEFieldType_ValidatedTextEntry, "CharacterClass", parse_CharacterClass, "name");

	// domination
	METableAddSimpleColumn(pTable, 		"Max game time",	"MaxGameTime",			150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Max over time",	"MaxOvertime",			150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Max drop time",	"MaxDropTime",			150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Flag name",		"FlagName",				150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Flag critter",		"FlagCritter",			150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"Require own flag",	"RequireOwnFlagToScore",150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"PointStatusFX",	"PointStatusFX",		150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"CapturePointFX",	"CapturePointFX",		150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );
	METableAddSimpleColumn(pTable, 		"ContestedPointFX",	"ContestedPointFX",		150, GetGameRuleGroupName(kPVPGameType_Domination), kMEFieldType_TextEntry );

	METableAddSimpleColumn(pTable, "Enable Strict Team Rules","EnableStrictTeamRules",120,QE_GROUP_MAIN,kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable, "Only Unteamed Can Join","UnteamedQueuingOnly",120,QE_GROUP_MAIN,kMEFieldType_BooleanCombo);
}

static void qe_initLevelBandColumns(METable *pTable)
{
	int id;
	// ---- Level Gating ----
	qeLevelBandID = id = METableCreateSubTable(pTable, "LevelBand", "EditorData", parse_QueueLevelBandEditorData, NULL, NULL, NULL, qe_createQueueLevelBandEditorData);
	METableAddSimpleSubColumn(pTable, id, "LevelBand", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "LevelBand", ME_STATE_LABEL);
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSimpleSubColumn(pTable, id,	"Level Band Index", "LevelBandIndex", 100, QE_SUBGROUP_LEVELBAND, kMEFieldType_TextEntry);

	METableAddSimpleSubColumn(pTable, id,	"Minimum Level", ".LevelBand.MinLevel", 100, QE_SUBGROUP_LEVELBAND, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Maximum Level", ".LevelBand.MaxLevel", 100, QE_SUBGROUP_LEVELBAND, kMEFieldType_TextEntry);

	// ---- Map Level ----
	METableAddSimpleSubColumn(pTable, id, "Map Level",		".LevelBand.MapLevel",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	// ---- Bolstering ----
	METableAddEnumSubColumn(pTable, id,   "Bolster Type",	".LevelBand.BolsterType",    150, QE_GROUP_MAIN, kMEFieldType_Combo, BolsterTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Bolster Level",	".LevelBand.BolsterLevel",  100, QE_GROUP_MAIN, kMEFieldType_TextEntry);

	// ---- Level Band World Vars ----
	METableAddSimpleSubColumn(pTable, id,	"Var Name",		".WorldVar.Name",		100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable,	id,		"Var Type",		".WorldVar.Type",		100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_Combo, WorldVariableTypeEnum);
	METableAddSimpleSubColumn(pTable, id,	"Int",			".WorldVar.IntVal",		100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Float",		".WorldVar.FloatVal",	100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"String",		".WorldVar.StringVal",	100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Message",		".WorldVar.MessageVal",	100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_Message);
	METableAddDictSubColumn(pTable, id,		"CritterDef",	".WorldVar.CritterDef",	100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_ValidatedTextEntry, "CritterDef", parse_CritterDef, "Name");
	METableAddDictSubColumn(pTable, id,		"CritterGroup",	".WorldVar.CritterGroup",100, QE_SUBGROUP_LEVELBAND_VAR, kMEFieldType_ValidatedTextEntry, "CritterGroup", parse_CritterGroup, "Name");
}

static void qe_initGroupColumns(METable *pTable)
{
	int id;
	qeGroupId = id = METableCreateSubTable(pTable, "Group", "GroupDefs", parse_QueueGroupDef, NULL, NULL, NULL, qe_createQueueGroupDef);
	METableAddSimpleSubColumn(pTable, id, "Group", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "Group", ME_STATE_LABEL);
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);
	METableAddSimpleSubColumn(pTable, id,	"Display Name",			".DisplayName.EditorCopy",	100, QE_SUBGROUP_GROUPDEF, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id,	"Min Group Size",		"Min",						100, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Min Overtime Size",	"MinTimed",					100, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Min Auto-Balance Size","AutoBalanceMin",			100, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Maximum Group Size",	"Max",						100, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Maximum Overtime Size","MaxTimed",					100, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddDictSubColumn(pTable, id,		"Required Allegiance",	"Allegiance",				100, QE_SUBGROUP_GROUPDEF, kMEFieldType_ValidatedTextEntry, "Allegiance", parse_AllegianceDef, "Name");
	METableAddSimpleSubColumn(pTable, id,	"Group's Spawn",		"SpawnTargetName",			160, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddExprSubColumn(pTable, id,		"Requires Expression",	"Requires",					160, QE_SUBGROUP_GROUPDEF, queue_GetContext(NULL));
	METableAddDictSubColumn(pTable, id,		"Group Faction",		"Faction",					100, QE_SUBGROUP_GROUPDEF, kMEFieldType_ValidatedTextEntry, "CritterFaction", parse_CritterFaction, "Name");

	// private settings
	METableAddSimpleSubColumn(pTable, id,	"Private UseGroupSettings",		"UseGroupPrivateSettings",				170, QE_SUBGROUP_GROUPDEF, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id,	"Private Min Group Size",		"PrivateMinGroupSize",					170, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Private Min Overtime Size",	"PrivateOverTimeSize",					170, QE_SUBGROUP_GROUPDEF, kMEFieldType_TextEntry);
}

static void qe_initWorldVarColumns(METable *pTable)
{
	int id;
	qeWorldVarId = id = METableCreateSubTable(pTable, "WorldVars", "WorldVars", parse_WorldVariable, NULL, NULL, NULL, qe_createQueueWorldVar);
	METableAddSimpleSubColumn(pTable, id, "WorldVar", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "WorldVar", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSimpleSubColumn(pTable, id,	"Var Name",		"Name",			100, QE_SUBGROUP_WORLDVAR, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable,	id,		"Var Type",		"Type",			100, QE_SUBGROUP_WORLDVAR, kMEFieldType_Combo, WorldVariableTypeEnum);
	METableAddSimpleSubColumn(pTable, id,	"Int",			"IntVal",		100, QE_SUBGROUP_WORLDVAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Float",		"FloatVal",		100, QE_SUBGROUP_WORLDVAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"String",		"StringVal",	100, QE_SUBGROUP_WORLDVAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id,	"Message",		"MessageVal",	100, QE_SUBGROUP_WORLDVAR, kMEFieldType_Message);
	METableAddDictSubColumn(pTable, id,		"CritterDef",	"CritterDef",	100, QE_SUBGROUP_WORLDVAR, kMEFieldType_ValidatedTextEntry, "CritterDef", parse_CritterDef, "Name");
	METableAddDictSubColumn(pTable, id,		"CritterGroup",	"CritterGroup",	100, QE_SUBGROUP_WORLDVAR, kMEFieldType_ValidatedTextEntry, "CritterGroup", parse_CritterGroup, "Name");
}

static void qe_initQueueVarColumns(METable *pTable)
{
	int id;
	qeQueueVarId = id = METableCreateSubTable(pTable, "QueueVarData", "QueueVarData", parse_QueueVariableData, NULL, NULL, NULL, qe_createQueueVarData);
	METableAddSimpleSubColumn(pTable, id, "QueueVarData", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, id, "QueueVarData", ME_STATE_LABEL);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSimpleSubColumn(pTable, id, "Name", "Name", 100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_TextEntry);
	METableAddSubColumn(pTable, id,	"WorldVariable", "WorldVariableName", NULL, 100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	METableAddSubColumn(pTable, id,	"Copy To WorldVariable", "CopyVariable", NULL, 100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	METableAddSubColumn(pTable, id,	"Apply To Custom Map Type","ApplyToCustomMapType", NULL, 100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, qe_GetCustomMapTypes);
	METableAddSubColumn(pTable, id,	"StringValue", "StringValue", NULL, 100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	METableAddSimpleSubColumn(pTable, id, "Display Name",".ChallengeData.DisplayMessage.EditorCopy", 160, QE_SUBGROUP_QUEUEVAR, kMEFieldType_Message);
	METableAddSimpleSubColumn(pTable, id, "Min Value",	 ".ChallengeData.MinValue",  100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Value",	 ".ChallengeData.MaxValue",  100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Reward Min Value",	 ".ChallengeData.RewardMinValue",  100, QE_SUBGROUP_QUEUEVAR, kMEFieldType_TextEntry);
}

static void *qe_createCustomMapData(METable *pTable, QueueDef *pQueueDef, QueueCustomMapData *pDataToClone, QueueCustomMapData *pBeforeData, QueueCustomMapData *pAfterData)
{
	QueueCustomMapData *pNewData;

	// Allocate the object
	if (pDataToClone)
	{
		pNewData = (QueueCustomMapData*)StructClone(parse_QueueCustomMapData, pDataToClone);
	}
	else
	{
		pNewData = (QueueCustomMapData*)StructCreate(parse_QueueCustomMapData);
	}

	assertmsg(pNewData, "Failed to create edtior QueueCustomMapData data");

	return pNewData;
}

static void qe_InitQueueCustomMapData(METable *pTable)
{
	// Create the subtable and get the ID
	s_CustomMapDataID = METableCreateSubTable(pTable, "Custom Map Data", "QueueCustomMapData", parse_QueueCustomMapData, NULL,
		NULL, NULL, qe_createCustomMapData);

	METableAddSimpleSubColumn(pTable, s_CustomMapDataID, "QueueCustomMapData", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, s_CustomMapDataID, "QueueCustomMapData", ME_STATE_LABEL);
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, s_CustomMapDataID, 2);

	METableAddSimpleSubColumn(pTable, s_CustomMapDataID,		"Map Type Name",	"Name",				150, QE_SUBGROUP_CUSTOMMAPDATA, kMEFieldType_TextEntry );
	METableAddSimpleSubColumn(pTable, s_CustomMapDataID,		"Map Name",				"MapName",			150, QE_SUBGROUP_TRACKEDEVENT, kMEFieldType_TextEntry );
	METableAddSimpleSubColumn(pTable, s_CustomMapDataID,		"Icon",				"Icon",				150, QE_SUBGROUP_TRACKEDEVENT, kMEFieldType_TextEntry );
	METableAddEnumSubColumn(pTable, s_CustomMapDataID,			"Game Mode",		"GameMode",			120, QE_SUBGROUP_TRACKEDEVENT, kMEFieldType_ValidatedTextEntry, PVPGameTypeEnum);
	METableAddSimpleSubColumn(pTable, s_CustomMapDataID,		"Display Name",		".DisplayName.EditorCopy",		150, QE_SUBGROUP_TRACKEDEVENT, kMEFieldType_Message );
}

static void *qe_createTrackedEvent(METable *pTable, QueueDef *pQueueDef, QueueTrackedEvent *pDataToClone, QueueTrackedEvent *pBeforeData, QueueTrackedEvent *pAfterData)
{
	QueueTrackedEvent *pNewData;

	// Allocate the object
	if (pDataToClone)
	{
		pNewData = (QueueTrackedEvent*)StructClone(parse_QueueTrackedEvent, pDataToClone);
	}
	else
	{
		pNewData = (QueueTrackedEvent*)StructCreate(parse_QueueTrackedEvent);
	}

	assertmsg(pNewData, "Failed to create edtior QueueTrackedEvent data");

	return pNewData;
}

static void qe_InitTrackedEvents(METable *pTable)
{
	// Create the subtable and get the ID
	s_TrackedEventID = METableCreateSubTable(pTable, "Tracked Events", "TrackedEvent", parse_QueueTrackedEvent, NULL,
		NULL, NULL, qe_createTrackedEvent);

	METableAddSimpleSubColumn(pTable, s_TrackedEventID, "TrackedEvent", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, s_TrackedEventID, "TrackedEvent", ME_STATE_LABEL);
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, s_TrackedEventID, 2);

	METableAddSimpleSubColumn(pTable, s_TrackedEventID,		"Map Value",		"MapValue",			150, QE_SUBGROUP_TRACKEDEVENT, kMEFieldType_TextEntry );
	METableAddSimpleSubColumn(pTable, s_TrackedEventID,		"Event String",		"EventString",		150, QE_SUBGROUP_TRACKEDEVENT, kMEFieldType_TextEntry );
}

static void *qe_createQueueRewardTable(METable *pTable, QueueDef *pQueueDef, QueueRewardTable *pDataToClone, QueueRewardTable *pBeforeData, QueueRewardTable *pAfterData)
{
	QueueRewardTable *pNewData;

	// Allocate the object
	if (pDataToClone)
	{
		pNewData = (QueueRewardTable*)StructClone(parse_QueueRewardTable, pDataToClone);
	}
	else
	{
		pNewData = (QueueRewardTable*)StructCreate(parse_QueueRewardTable);
	}

	assertmsg(pNewData, "Failed to create edtior QueueRewardTable data");

	return pNewData;
}

static void qe_InitQueueRewardTable(METable *pTable)
{
	// Create the subtable and get the ID
	s_RewardTableID = METableCreateSubTable(pTable, "Reward Tables", "QueueRewardTables", parse_QueueRewardTable, NULL,
		NULL, NULL, qe_createQueueRewardTable);

	METableAddSimpleSubColumn(pTable, s_RewardTableID, "QueueRewardTables", NULL, 80, NULL, 0);
	METableSetSubColumnState(pTable, s_RewardTableID, "QueueRewardTables", ME_STATE_LABEL);
	// Lock in label column
	METableSetNumLockedSubColumns(pTable, s_RewardTableID, 2);

	METableAddEnumSubColumn(pTable, s_RewardTableID, 		"Game Type",		"Type",				120, QE_SUBGROUP_GAMERULES, kMEFieldType_Combo, PVPGameTypeEnum);
	METableAddEnumSubColumn(pTable, s_RewardTableID, 		"Reward Condition",	"RewardCondition",	120, QE_SUBGROUP_GAMERULES, kMEFieldType_Combo, EQueueRewardTableConditionEnum);
	METableAddExprSubColumn(pTable, s_RewardTableID,		"Condition Expression",	"ExprRewardConditionBlock",	160, QE_SUBGROUP_GAMERULES, queue_GetContext(NULL));
	METableAddGlobalDictSubColumn(pTable, s_RewardTableID,	"Reward Table",		"RewardTable",		180, QE_SUBGROUP_GAMERULES, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");

	METableAddSimpleSubColumn(pTable, s_RewardTableID,		"Event",			"Event",			150, QE_SUBGROUP_REWARDS, kMEFieldType_TextEntry );
	METableAddSimpleSubColumn(pTable, s_RewardTableID,		"Scale Reward",		"ScaleReward",		128, QE_SUBGROUP_REWARDS, kMEFieldType_BooleanCombo);
}

static void qe_init(MultiEditEMDoc *pEditorDoc)
{
	if (!qeWindow) {
		// Create the editor window
		qeWindow = MEWindowCreate("Queue Editor", "Queue", "Queues", SEARCH_TYPE_QUEUE, g_hQueueDefDict, parse_QueueDef, "name", "filename", "scope", pEditorDoc);

		// Add queue-specific columns
		qe_initQueueColumns(qeWindow->pTable);
		qe_initLevelBandColumns(qeWindow->pTable);
		qe_initGroupColumns(qeWindow->pTable);
		qe_initWorldVarColumns(qeWindow->pTable);
		qe_initQueueVarColumns(qeWindow->pTable);

		qe_InitTrackedEvents(qeWindow->pTable);
		qe_InitQueueCustomMapData(qeWindow->pTable);
		qe_InitQueueRewardTable(qeWindow->pTable);

		METableFinishColumns(qeWindow->pTable);

		// Init the menus after adding all the columns
		MEWindowInitTableMenus(qeWindow);

		//Get missions for the editor
		resRequestAllResourcesInDictionary("MissionDef");

		// Set the callbacks
		qe_initCallbacks(qeWindow, qeWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(qeWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *queueEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	qe_init(pEditorDoc);

	return qeWindow;
}


void queueEditor_createQueue(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = qe_createObject(qeWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(qeWindow->pTable, pObject, 1, 1);
}

#endif
