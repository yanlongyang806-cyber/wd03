#include "mechanics_common.h"
#include "StringCache.h"
#include "wlInteraction.h"
#include "WorldGrid.h"

#include "AutoGen/mechanics_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Map Summary
// ----------------------------------------------------------------------------------

// Resets numerical data in the passed MapSummary
void mechanics_ResetCalculatedMapSummaryData( MapSummary* pData )
{
	if ( pData ) {
		pData->iNumInstances = 0;
		pData->iNumNonFullInstances = 0;
		pData->iNumPlayers = 0;
		pData->iNumEnabledOpenInstancing = 0;
	}
}


MapList* mechanics_CreateMapListFromMapSummaryList( MapSummaryList* pSummaryList )
{
	MapList *pList = StructCreate( parse_MapList );
	
	S32 i, iSize = eaSize(&pSummaryList->eaList);
	for ( i = 0; i < iSize; i++ ) {
		const char* pchMapName = allocAddString(pSummaryList->eaList[i]->pchMapName);
		const char* pchMapVars = pSummaryList->eaList[i]->pchMapVars;
		
		if ( pchMapVars && pchMapVars[0] ) {
			pchMapVars = allocAddString(pchMapVars);
		}

		if ( pchMapName && pchMapName[0] ) {
			eaPush(&pList->eaMapNames,(char*)pchMapName);
			eaPush(&pList->eaMapVars,(char*)pchMapVars);
		}
	}

	return pList;
}


MapSummaryList* mechanics_CreateMapSummaryListFromMapList( MapList* pList )
{
	MapSummaryList* pSummaryList = StructCreate( parse_MapSummaryList );
	S32 i, iSize = MIN(eaSize(&pList->eaMapNames),eaSize(&pList->eaMapVars));

	for ( i = 0; i < iSize; i++ ) {
		const char* pchMapName = eaGet(&pList->eaMapNames,i);
		const char* pchMapVars = eaGet(&pList->eaMapVars,i);

		if ( pchMapVars && pchMapVars[0] ) {
			pchMapVars = allocAddString(pchMapVars);
		}

		if ( pchMapName && pchMapName[0] ) {
			MapSummary* pData = StructCreate( parse_MapSummary );
			pData->pchMapName = (char*)allocAddString(pchMapName);
			pData->pchMapVars = (char*)pchMapVars;
			eaPush(&pSummaryList->eaList, pData);
		}
	}

	return pSummaryList;
}


bool mechanics_MapSummaryHasNode( MapSummary* pData, WorldInteractionNode* pNode )
{
	S32 i;
	if ( pNode==NULL )
		return false;

	for ( i = eaSize(&pData->eaNodes)-1; i >= 0; i-- )
	{
		if ( GET_REF(pData->eaNodes[i]->hNode) == pNode )
			return true;
	}

	return false;
}


#include "AutoGen/mechanics_common_h_ast.c"