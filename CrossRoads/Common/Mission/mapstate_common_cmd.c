#include "Entity.h"
#include "Expression.h"
#include "mapstate_common.h"
#include "WorldVariable.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


//-----------------------------------------------------------------------
//   Debug Commands
//-----------------------------------------------------------------------

// A debug command to print the names and values of all existing map values (but not player map values)
AUTO_COMMAND;
void MapValuePrintAll(Entity *pEnt)
{
	MapState *pState = mapState_FromEnt(pEnt);
	if (pState && pState->pMapValues) {
		int i,n=eaSize(&pState->pMapValues->eaValues);	

		printf("Map values:\n");
		for(i=0; i<n; i++) {
			if (MultiValIsNumber(&pState->pMapValues->eaValues[i]->mvValue)) {
				printf("   %s %"FORM_LL"d\n", pState->pMapValues->eaValues[i]->pcName, MultiValGetInt(&pState->pMapValues->eaValues[i]->mvValue, NULL));
			} else if(MultiValIsString(&pState->pMapValues->eaValues[i]->mvValue)) {
				printf("   %s %s\n", pState->pMapValues->eaValues[i]->pcName, MultiValGetString(&pState->pMapValues->eaValues[i]->mvValue, NULL));
			} else {
				printf("   %s UNKNOWN TYPE\n", pState->pMapValues->eaValues[i]->pcName);
			}
		}
		printf("\n");
	}
}


// A debug command to print the names and values of all player map values for the current player
AUTO_COMMAND;
void PlayerMapValuePrintAll(Entity *pClientEntity)
{
	if (pClientEntity) {
		ContainerID iContID = pClientEntity->myContainerID;
		PlayerMapValues *pPlayerMapValues = mapState_FindPlayerValues(entGetPartitionIdx(pClientEntity), iContID);

		if (pPlayerMapValues) {
			int i, n = eaSize(&pPlayerMapValues->eaValues);

			printf("Player map values:\n");

			for(i=0; i<n; i++) {
				if(MultiValIsNumber(&pPlayerMapValues->eaValues[i]->mvValue)) {
					printf("   %s %"FORM_LL"d\n", pPlayerMapValues->eaValues[i]->pcName, MultiValGetInt(&pPlayerMapValues->eaValues[i]->mvValue, NULL));
				} else if(MultiValIsString(&pPlayerMapValues->eaValues[i]->mvValue)) {
					printf("   %s %s\n", pPlayerMapValues->eaValues[i]->pcName, MultiValGetString(&pPlayerMapValues->eaValues[i]->mvValue, NULL));
				} else {
					printf("   %s UNKNOWN TYPE\n", pPlayerMapValues->eaValues[i]->pcName);
				}
			}
			printf("\n");	
		}
	}
}

