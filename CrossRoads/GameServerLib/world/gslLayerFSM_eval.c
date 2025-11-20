/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "../aiMessages.h"
#include "earray.h"
#include "Expression.h"
#include "gslLayerFSM.h"
#include "StateMachine.h"


// ----------------------------------------------------------------------------------
// Expression functions
// ----------------------------------------------------------------------------------

// Send a message to a disembodied layer FSM
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessageToLayerFSM);
void exprFuncSendMessageToLayerFSM(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcFSMName, const char *pcMsgTag)
{
	static GameLayerFSM **eaLayerFSMs = NULL;

	WorldScope *pScope = exprContextGetScope(pContext);
	int i;

	eaClear(&eaLayerFSMs);
	layerfsm_GetByFSMName(pcFSMName, pScope, &eaLayerFSMs);

	for(i = eaSize(&eaLayerFSMs)-1; i >= 0; i--)
	{
		FSMContext *pFSMContext = layerfsm_GetFSMContext(eaLayerFSMs[i], iPartitionIdx);
		aiMessageSendAbstract(iPartitionIdx, pFSMContext, pcMsgTag, NULL, 1, NULL);
	}
}


// Send a message to a disembodied layer FSM by logical name
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendMessageToLayerFSMByLogicalName);
void exprFuncSendMessageToLayerFSMByLogicalName(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcLogicalName, const char *pcMsgTag)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameLayerFSM *pLayerFSM;
	FSMContext *pFSMContext;

	pLayerFSM = layerfsm_GetByName(pcLogicalName, pScope);
	if (!pLayerFSM) {
		return;
	}

	pFSMContext = layerfsm_GetFSMContext(pLayerFSM, iPartitionIdx);
	aiMessageSendAbstract(iPartitionIdx, pFSMContext, pcMsgTag, NULL, 1, NULL);
}


// Send a message to a layer FSM with ent array data
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendEntArrayMessageToLayerFSM);
void exprFuncSendEntArrayMessageToLayerFSM(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN eaEntsIn, const char *pcFSMName, const char *pcMsgTag)
{
	static GameLayerFSM **eaLayerFSMs = NULL;

	WorldScope *pScope = exprContextGetScope(pContext);
	int i;

	eaClear(&eaLayerFSMs);
	layerfsm_GetByFSMName(pcFSMName, pScope, &eaLayerFSMs);

	for(i = eaSize(&eaLayerFSMs)-1; i >= 0; i--)
	{
		FSMContext *pFSMContext = layerfsm_GetFSMContext(eaLayerFSMs[i], iPartitionIdx);
		aiMessageSendAbstract(iPartitionIdx, pFSMContext, pcMsgTag, NULL, 1, eaEntsIn);
	}
}


// Send a message to a layer FSM with ent array data by logical Name
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(SendEntArrayMessageToLayerFSMByLogicalName);
void exprFuncSendEntArrayMessageToLayerFSMByLogicalName(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN eaEntsIn, const char *pcLogicalName, const char *pcMsgTag)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameLayerFSM *pLayerFSM;
	FSMContext *pFSMContext;

	pLayerFSM = layerfsm_GetByName(pcLogicalName, pScope);
	if (!pLayerFSM) {
		return;
	}

	pFSMContext = layerfsm_GetFSMContext(pLayerFSM, iPartitionIdx);
	aiMessageSendAbstract(iPartitionIdx, pFSMContext, pcMsgTag, NULL, 1, eaEntsIn);
}


// Gets the state of a layerFSM
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(LayerFSMGetState);
const char* exprFuncLayerFSMGetState(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcFSMName)
{
	static GameLayerFSM **eaLayerFSMs = NULL;

	WorldScope *pScope = exprContextGetScope(pContext);
	FSMContext *pFSMContext;

	eaClear(&eaLayerFSMs);
	layerfsm_GetByFSMName(pcFSMName, pScope, &eaLayerFSMs);
	if(eaSize(&eaLayerFSMs)==0) {
		return NULL;
	}

	pFSMContext = layerfsm_GetFSMContext(eaLayerFSMs[0], iPartitionIdx);
	return fsmGetState(pFSMContext);
}


// Gets the state of a layerFSM by logical name
AUTO_EXPR_FUNC(gameutil) ACMD_NAME(LayerFSMGetStateByLogicalName);
const char* exprFuncLayerFSMGetStateByLogicalName(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcLogicalName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameLayerFSM *pLayerFSM = NULL;
	FSMContext *pFSMContext;

	pLayerFSM = layerfsm_GetByName(pcLogicalName, pScope);
	if (!pLayerFSM) {
		return "DoesNotExist";
	}

	pFSMContext = layerfsm_GetFSMContext(pLayerFSM, iPartitionIdx);
	return fsmGetState(pFSMContext);
}


// Returns the tick rate of layerFSMs in seconds
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(LayerFSMGetTickRate);
F32 exprFuncLayerFSMGetTickRate(ExprContext *pContext)
{
	return LAYERFSM_UPDATE_TICK/30.0f;
}
