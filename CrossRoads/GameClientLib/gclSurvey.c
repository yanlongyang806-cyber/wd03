/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "earray.h"
#include "expression.h"

#include "UIGen.h"

#include "survey.h"

typedef U32 ContainerID;
typedef U32 EntityRef;
#include "autogen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSendCommandWithVars");
void ui_GenExprSendCommandWithVars(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCommand)
{
	if(pGen && pchCommand)
	{
		IndexedPairs pairs = {0};
		int i = eaSize(&pGen->eaVars);
		IndexedPair *p = malloc(sizeof(IndexedPair)*i);

		eaSetCapacity(&pairs.ppPairs, i);
		for(i=i-1; i>=0; i--)
		{
			int j = eaPush(&pairs.ppPairs, p+i);
			pairs.ppPairs[j]->pchKey = (char *)pGen->eaVars[i]->pchName;
			pairs.ppPairs[j]->pchValue = (char *)pGen->eaVars[i]->pchString;
		}

		ServerCmd_survey_Log(&pairs);

		eaDestroy(&pairs.ppPairs);
		free(p);
	}
}

#include "survey_h_ast.c"

/* End of File */
