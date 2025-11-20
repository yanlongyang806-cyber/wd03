#include "metatask.h"
#include "stringcache.h"
#include "earray.h"

#include "utils.h"
#include "Error.h"
#include "file.h"
#include "stashtable.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

const char *g_lastTaskName;

typedef struct
{
	const char *pOtherTaskName;
	bool bHeComesBefore;
	bool bIRequireHim;
	bool bDisabled; //this dependency is currently disabled, presumably due to a CANCELLEDBY
} MetaTaskDependency;

typedef struct
{
	MetaTaskCB *pCB;
	const char *pCBName;
} MetaTaskCBInfo;

typedef struct MetaTaskSingleTask
{
	const char *pTaskName;
	MetaTaskCBInfo **ppCBInfos;
	bool bOKIfMultiplyDefined;
	
	bool bStartsOutOn;

	MetaTaskDependency **ppDependencies;
	struct MetaTaskSingleTask **ppChildren;

	int iGraphDepth;
	int iGraphID;
	bool bHasAtLeastOneParent;

	bool bShouldRunThisTime;

	enumMetaTaskBehavior eBehavior;
	bool bHasRunAtLeastOnce;

	const char **ppCancelledBys;
} MetaTaskSingleTask;

typedef struct
{
	const char *pMetaTaskName;
	MetaTaskSingleTask **ppTasks;
	bool bIsRegistered;
	bool bAlreadyDidGraphAndOrdering;
	StashTable taskTable;
} MetaTask;

MetaTask **sppMetaTasks = NULL;


void MetaTask_DisableAllDependenciesOn(const char *pMetaTask, const char *pTaskNothingShouldDependOn);


MetaTask *AddOrGetMetaTask(const char *pMetaTaskName, bool bRegister)
{
	int i;
	MetaTask *pMetaTask;

	for (i=0; i < eaSize(&sppMetaTasks); i++)
	{
		if (stricmp(sppMetaTasks[i]->pMetaTaskName, pMetaTaskName) == 0)
		{
			if (bRegister)
			{
				assertmsgf(sppMetaTasks[i]->bIsRegistered == false, "metatask %s registered twice", pMetaTaskName);
				sppMetaTasks[i]->bIsRegistered = true;
			}

			return sppMetaTasks[i];
		}
	}

	pMetaTask = (MetaTask*)calloc(sizeof(MetaTask), 1);
	pMetaTask->pMetaTaskName = allocAddString(pMetaTaskName);
	pMetaTask->bIsRegistered = bRegister;
	pMetaTask->ppTasks = NULL;

	eaPush(&sppMetaTasks, pMetaTask);

	pMetaTask->taskTable = stashTableCreateWithStringKeys(16, StashDefault);

	return pMetaTask;
}

MetaTaskSingleTask *MetaTask_FindSingleTask(MetaTask *pMetaTask, const char *pTaskName)
{
	MetaTaskSingleTask *pTask = NULL;

	if (!pMetaTask)
	{
		return NULL;
	}

	stashFindPointer(pMetaTask->taskTable, pTaskName, &pTask);
	
	return pTask;

}




void CheckForUnregisteredMetaTasks(void)
{
	int i;

	for (i=0; i < eaSize(&sppMetaTasks); i++)
	{
		if (!sppMetaTasks[i]->bIsRegistered)
		{
			assertmsgf(0, "MetaTask \"%s\" was never registered... may be a typo", sppMetaTasks[i]->pMetaTaskName);
		}
	}
}


void MetaTask_Register(const char *pMetaTaskName)
{
	AddOrGetMetaTask(pMetaTaskName, true);
}



void MetaTask_AddUniqueDependenciesToTask(MetaTaskSingleTask *pTask, char *pDependencyString)
{
	int i;

	if (pDependencyString)
	{
		char *pToken;
		//have to get a garbage token first to make sure strTokWithSpacesAndPunctuation is reset, in case
		//multiple static constant dependency strings are identical
		pToken = strTokWithSpacesAndPunctuation(NULL, NULL);
		pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");
		while (pToken)
		{
			MetaTaskDependency *pDependency;

			if (!(strcmp(pToken, "BEFORE") == 0 || strcmp(pToken, "AFTER") == 0 || strcmp(pToken, "CANCELLEDBY") == 0))
			{
				assertmsgf(0, "Unrecognized token %s in metatask dependency string", pToken);
			}

			if (strcmp(pToken, "CANCELLEDBY") == 0)
			{
				pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");
				assertmsg(pToken && pToken[0] != ',', "Didn't find metatask dependency string task name");

				for (i=0; i < eaSize(&pTask->ppCancelledBys); i++)
				{
					if (stricmp(pTask->ppCancelledBys[i], pToken) == 0)
					{
						break;
					}
				}

				if (i == eaSize(&pTask->ppCancelledBys))
				{
					eaPush(&pTask->ppCancelledBys, allocAddString(pToken));
				}

				pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");

			}
			else
			{

				pDependency = (MetaTaskDependency*)calloc(sizeof(MetaTaskDependency), 1);
				
				pDependency->bHeComesBefore = (strcmp(pToken, "AFTER") == 0);

				pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");

				assertmsg(pToken && pToken[0] != ',', "Didn't find metatask dependency string task name");

				pDependency->pOtherTaskName = allocAddString(pToken);
				pDependency->bIRequireHim = true;

				pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");

				if (pToken && strcmp(pToken, "IFTHERE") == 0)
				{
					pDependency->bIRequireHim = false;
					pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");
				}


				//check for uniqueness
				for (i=0; i < eaSize(&pTask->ppDependencies); i++)
				{
					if (stricmp(pDependency->pOtherTaskName, pTask->ppDependencies[i]->pOtherTaskName) == 0)
					{
						assertmsgf(pDependency->bHeComesBefore == pTask->ppDependencies[i]->bHeComesBefore
							&& pDependency->bIRequireHim == pTask->ppDependencies[i]->bIRequireHim,
							"Two nonmatching dependencies from %s on %s... not allowed",
							pTask->pTaskName, pDependency->pOtherTaskName);
						break;
					}
				}

				//if it already exists, delete it, otherwise add it
				if (i < eaSize(&pTask->ppDependencies))
				{
					free(pDependency);
				}
				else
				{
					eaPush(&pTask->ppDependencies, pDependency);
				}
			}


			assertmsgf(!pToken || pToken[0] == ',', "expected comma in metatask dependency string %s", pDependencyString);
		
			if (pToken)
			{
				pToken = strTokWithSpacesAndPunctuation(pDependencyString, ",");
			}
		}
	}
}

void MetaTask_AddTask(const char *pMetaTaskName, const char *pTaskName, MetaTaskCB *pCB, const char *pCBName, bool bOKIfAddedMultiply,
	bool bStartsOutOn, char *pDependencyString, enumMetaTaskBehavior eBehavior)
{
	MetaTask *pMetaTask = AddOrGetMetaTask(pMetaTaskName, false);
	MetaTaskSingleTask *pTask;
	MetaTaskCBInfo *pCBInfo;
	char *pToken = NULL;

	pMetaTask->bAlreadyDidGraphAndOrdering = false;

	pTask = MetaTask_FindSingleTask(pMetaTask, pTaskName);

	if (pTask)
	{
		if (bOKIfAddedMultiply && pTask->bOKIfMultiplyDefined)
		{
			assertmsgf(pTask->eBehavior == eBehavior, "Two tasks with same name %s but different behaviors",
				pTaskName);

			pTask->bStartsOutOn |= bStartsOutOn;

			MetaTask_AddUniqueDependenciesToTask(pTask, pDependencyString);

			pCBInfo = malloc(sizeof(MetaTaskCBInfo));
			pCBInfo->pCB = pCB;
			pCBInfo->pCBName = pCBName;

			eaPush(&pTask->ppCBInfos, pCBInfo);


			return;
		}
		else
		{
			assertmsgf(0, "Found duplicate task %s in metatask %s\n", pTaskName, pMetaTaskName);
		}	
	}

	pTask = (MetaTaskSingleTask*)calloc(sizeof(MetaTaskSingleTask), 1);

	pTask->pTaskName = allocAddString(pTaskName);
	pTask->bStartsOutOn = bStartsOutOn;
	pTask->bOKIfMultiplyDefined = bOKIfAddedMultiply;

	pCBInfo = malloc(sizeof(MetaTaskCBInfo));
	pCBInfo->pCB = pCB;
	pCBInfo->pCBName = pCBName;

	eaPush(&pTask->ppCBInfos, pCBInfo);

	pTask->ppDependencies = NULL;
	pTask->eBehavior = eBehavior;


	MetaTask_AddUniqueDependenciesToTask(pTask, pDependencyString);

	

	eaPush(&pMetaTask->ppTasks, pTask);
	stashAddPointer(pMetaTask->taskTable, pTaskName, pTask, false);
}



void MetaTask_AddChild(MetaTaskSingleTask *pParent, MetaTaskSingleTask *pChild)
{
	int i;

	for (i=0; i < eaSize(&pParent->ppChildren); i++)
	{
		if (pParent->ppChildren[i] == pChild)
		{
			return;
		}
	}

	eaPush(&pParent->ppChildren, pChild);

	pChild->bHasAtLeastOneParent = true;
}

void MetaTask_RecursivelyCalcGraphDepth(MetaTask *pMetaTask, MetaTaskSingleTask *pTask, int iCurDepth)
{
	int i;

	if (iCurDepth > eaSize(&pMetaTask->ppTasks))
	{
		assertmsgf(0, "Circular dependency found in metatask \"%s\", task \"%s\"", pMetaTask->pMetaTaskName, pTask->pTaskName);
	}

	if (pTask->iGraphDepth >= iCurDepth)
	{
		return;
	}

	pTask->iGraphDepth = iCurDepth;

	for (i=0; i < eaSize(&pTask->ppChildren); i++)
	{
		MetaTask_RecursivelyCalcGraphDepth(pMetaTask, pTask->ppChildren[i], iCurDepth + 1);
	}
}


int MetaTask_Comparator(const MetaTaskSingleTask **a, const MetaTaskSingleTask **b)
{
	if ((*a)->iGraphDepth > (*b)->iGraphDepth)
	{
		return 1;
	}

	if ((*a)->iGraphDepth < (*b)->iGraphDepth)
	{
		return -1;
	}

	return 0;
}


void MetaTask_CalculateGraphAndOrdering(MetaTask *pMetaTask)
{
	int i, j;
	int iNumNodes = eaSize(&pMetaTask->ppTasks);
	bool bFoundARootNode = false;

	if (pMetaTask->bAlreadyDidGraphAndOrdering)
	{
		return;
	}

	pMetaTask->bAlreadyDidGraphAndOrdering = true;

	for (i=0; i < iNumNodes; i++)
	{
		pMetaTask->ppTasks[i]->iGraphDepth = -1;
		pMetaTask->ppTasks[i]->iGraphID = 0;

	}

	for (i=0; i < iNumNodes; i++)
	{
		MetaTaskSingleTask *pTask = pMetaTask->ppTasks[i];
		int iNumDependencies = eaSize(&pTask->ppDependencies);
		for (j=0; j < iNumDependencies; j++)
		{
			MetaTaskSingleTask *pOtherTask = MetaTask_FindSingleTask(pMetaTask, pTask->ppDependencies[j]->pOtherTaskName);
			assertmsgf(pOtherTask, "Unrecognized task \"%s\" in dependencies for task \"%s\" in metatask \"%s\"",
				pTask->ppDependencies[j]->pOtherTaskName, pTask->pTaskName, pMetaTask->pMetaTaskName);

			if (pTask->ppDependencies[j]->bHeComesBefore)
			{
				MetaTask_AddChild(pOtherTask, pTask);
			}
			else
			{
				MetaTask_AddChild(pTask, pOtherTask);
			}
		}
	}

	for (i=0; i < iNumNodes; i++)
	{
		MetaTaskSingleTask *pTask = pMetaTask->ppTasks[i];

		if (!pTask->bHasAtLeastOneParent)
		{
			MetaTask_RecursivelyCalcGraphDepth(pMetaTask, pTask, 0);
			bFoundARootNode = true;		
		}
	}

	if (!bFoundARootNode)
	{
		assertmsgf(0, "Found to parent nodes for metatask \"%s\"... dependencies must be circular", pMetaTask->pMetaTaskName);
	}

	eaQSort(pMetaTask->ppTasks, MetaTask_Comparator);

}

void MetaTask_RecurseSetShouldRun(MetaTask *pMetaTask, MetaTaskSingleTask *pTask)
{
	int i;

	if (pTask->bShouldRunThisTime)
	{
		return;
	}

	pTask->bShouldRunThisTime = true;

	for (i=0; i < eaSize(&pTask->ppDependencies); i++)
	{
		if (!pTask->ppDependencies[i]->bDisabled)
		{
			if (pTask->ppDependencies[i]->bIRequireHim)
			{
				MetaTask_RecurseSetShouldRun(pMetaTask, MetaTask_FindSingleTask(pMetaTask, pTask->ppDependencies[i]->pOtherTaskName));
			}
		}
	}
}

typedef void MetaTask_DoThingsInOrderCB(MetaTaskSingleTask *pTask, void *pUserData);

void MetaTask_Execute(MetaTaskSingleTask *pTask, void *pUserData)
{
	if (pTask->eBehavior == METATASK_BEHAVIOR_ONLY_ONCE && pTask->bHasRunAtLeastOnce)
	{

	}
	else
	{
		int i;
		pTask->bHasRunAtLeastOnce = true;			

		for (i=0; i < eaSize(&pTask->ppCBInfos); i++)
		{
			if (pTask->ppCBInfos[i]->pCB)
			{
				if (errorGetVerboseLevel())
					loadstart_printf("MetaTask: %s...", pTask->ppCBInfos[i]->pCBName);
				pTask->ppCBInfos[i]->pCB();
				g_lastTaskName = pTask->pTaskName;
				if (errorGetVerboseLevel())
					loadend_printf("done.");
			}
		}
	}
}

void MetaTask_DoThingsInOrder(const char *pMetaTaskName, MetaTask_DoThingsInOrderCB *pCB, void *pUserData)
{
	static bool sbFirst = true;
	MetaTask *pMetaTask = AddOrGetMetaTask(pMetaTaskName, false);
	int i,j;

	if (sbFirst)
	{
		sbFirst = false;
		CheckForUnregisteredMetaTasks();
	}

	if (eaSize(&pMetaTask->ppTasks) == 0)
	{
		return;
	}

	MetaTask_CalculateGraphAndOrdering(pMetaTask);

	//reset everything to off
	for (i=0; i < eaSize(&pMetaTask->ppTasks); i++)
	{
		MetaTaskSingleTask *pTask = pMetaTask->ppTasks[i];
	
		pTask->bShouldRunThisTime = false;

		for (j=0;j < eaSize(&pTask->ppDependencies); j++)
		{
			pTask->ppDependencies[j]->bDisabled = false;
		}
	}

	//do all cancelledbys
	for (i=0; i < eaSize(&pMetaTask->ppTasks); i++)
	{
		MetaTaskSingleTask *pTask = pMetaTask->ppTasks[i];
	
		for (j=0; j < eaSize(&pTask->ppCancelledBys); j++)
		{
			MetaTaskSingleTask *pOtherTask = MetaTask_FindSingleTask(pMetaTask, pTask->ppCancelledBys[j]);
			assertmsgf(pOtherTask, "Unknown/misspelled task %s", pTask->ppCancelledBys[j]);
			if (pOtherTask->bStartsOutOn)
			{
				assertmsgf(!pTask->bStartsOutOn, "One starts-out-on task (%s) wants to be cancelled by another (%s)",
					pTask->pTaskName, pOtherTask->pTaskName);

				MetaTask_DisableAllDependenciesOn(pMetaTask->pMetaTaskName, pTask->pTaskName);
				break;
			}
		}
	}

	for (i=0; i < eaSize(&pMetaTask->ppTasks); i++)
	{
		MetaTaskSingleTask *pTask = pMetaTask->ppTasks[i];

		if (pTask->bStartsOutOn)
		{
			MetaTask_RecurseSetShouldRun(pMetaTask, pTask);
		}
	}


	for (i=0; i < eaSize(&pMetaTask->ppTasks); i++)
	{
		if (pMetaTask->ppTasks[i]->bShouldRunThisTime)
		{
			pCB(pMetaTask->ppTasks[i], pUserData);
		}
	}
}

void MetaTask_DoMetaTask(const char *pMetaTaskName)
{
	MetaTask_DoThingsInOrder(pMetaTaskName, MetaTask_Execute, NULL);
	g_lastTaskName = NULL;
}



void MetaTask_AddDependencies(const char *pMetaTaskName, const char *pTaskName, char *pNewDependencies)
{
	MetaTask *pTask = AddOrGetMetaTask(pMetaTaskName, false);
	MetaTaskSingleTask *pSingleTask = MetaTask_FindSingleTask(pTask, pTaskName);

	if (!pSingleTask)
	{
		Errorf("Couldn't find task %s", pTaskName);
		return;
	}

	MetaTask_AddUniqueDependenciesToTask(pSingleTask, pNewDependencies);
	pTask->bAlreadyDidGraphAndOrdering = false;
}

void MetaTask_DestroyDependency(MetaTaskDependency *pDependency)
{
	free(pDependency);
}

void MetaTask_RemoveDependencies(const char *pMetaTaskName, const char *pTaskName, const char *pTaskNameItShouldNotDependOn)
{
	MetaTask *pTask = AddOrGetMetaTask(pMetaTaskName, false);
	MetaTaskSingleTask *pSingleTask = MetaTask_FindSingleTask(pTask, pTaskName);
	int i;

	if (!pSingleTask)
	{
		Errorf("Couldn't find task %s", pTaskName);
		return;
	}

	for (i=eaSize(&pSingleTask->ppDependencies) - 1; i >= 0; i--)
	{
		if (stricmp(pSingleTask->ppDependencies[i]->pOtherTaskName, pTaskNameItShouldNotDependOn) == 0)
		{
			MetaTask_DestroyDependency(pSingleTask->ppDependencies[i]);
			eaRemoveFast(&pSingleTask->ppDependencies, i);

		}
	}

	pTask->bAlreadyDidGraphAndOrdering = false;
}


//removes all dependences of all tasks on a single task
void MetaTask_RemoveAllDependenciesOn(const char *pMetaTask, const char *pTaskNothingShouldDependOn)
{
	MetaTask *pTask = AddOrGetMetaTask(pMetaTask, false);
	int i;

	for (i=0; i < eaSize(&pTask->ppTasks); i++)
	{
		MetaTask_RemoveDependencies(pMetaTask, pTask->ppTasks[i]->pTaskName, pTaskNothingShouldDependOn);
	}
}

//disable is different than remove in that it just turns them off and doesn't remove them
void MetaTask_DisableDependencies(const char *pMetaTaskName, const char *pTaskName, const char *pTaskNameItShouldNotDependOn)
{
	MetaTask *pTask = AddOrGetMetaTask(pMetaTaskName, false);
	MetaTaskSingleTask *pSingleTask = MetaTask_FindSingleTask(pTask, pTaskName);
	int i;

	if (!pSingleTask)
	{
		Errorf("Couldn't find task %s", pTaskName);
		return;
	}

	for (i=eaSize(&pSingleTask->ppDependencies) - 1; i >= 0; i--)
	{
		if (stricmp(pSingleTask->ppDependencies[i]->pOtherTaskName, pTaskNameItShouldNotDependOn) == 0)
		{
			pSingleTask->ppDependencies[i]->bDisabled = true;			
		}
	}

	pTask->bAlreadyDidGraphAndOrdering = false;
}


//Disables all dependences of all tasks on a single task
void MetaTask_DisableAllDependenciesOn(const char *pMetaTask, const char *pTaskNothingShouldDependOn)
{
	MetaTask *pTask = AddOrGetMetaTask(pMetaTask, false);
	int i;

	for (i=0; i < eaSize(&pTask->ppTasks); i++)
	{
		MetaTask_DisableDependencies(pMetaTask, pTask->ppTasks[i]->pTaskName, pTaskNothingShouldDependOn);
	}
}



//Sets whether a task starts out on
void MetaTask_SetTaskStartsOn(const char *pMetaTaskName, const char *pTaskName, bool bStartsOutOn)
{
	MetaTask *pTask = AddOrGetMetaTask(pMetaTaskName, false);
	MetaTaskSingleTask *pSingleTask = MetaTask_FindSingleTask(pTask, pTaskName);

	if (!pSingleTask)
	{
		Errorf("Couldn't find task %s", pTaskName);
		return;
	}

	pSingleTask->bStartsOutOn = bStartsOutOn;
}

void MetaTask_WriteOutPrototype(MetaTaskSingleTask *pTask, FILE *pOutFile)
{
	if (pTask->eBehavior == METATASK_BEHAVIOR_ONLY_ONCE && pTask->bHasRunAtLeastOnce)
	{

	}
	else
	{
		int i, iTask = 0;

		for (i=0; i < eaSize(&pTask->ppCBInfos); i++)
		{
			if (pTask->ppCBInfos[i]->pCB)
			{
				fprintf(pOutFile, "void %s(void);\n", pTask->ppCBInfos[i]->pCBName);
			}
		}
		fprintf(pOutFile, "	Task name: %s", pTask->pTaskName);
		if (eaSize(&pTask->ppChildren) > 0)
		fprintf(pOutFile, "\n		Required by tasks: ");
		for (i=0; i < eaSize(&pTask->ppChildren); i++)
		{
			if (pTask->ppChildren[i]->bShouldRunThisTime)
			{
				if (iTask++ != 0)
					fprintf(pOutFile, ", ");
				fprintf(pOutFile, "%s", pTask->ppChildren[i]->pTaskName);
			}
		}

		fprintf(pOutFile, "\n\n");
	}
}

void MetaTask_WriteOutFunctionCall(MetaTaskSingleTask *pTask, FILE *pOutFile)
{
	if (pTask->eBehavior == METATASK_BEHAVIOR_ONLY_ONCE && pTask->bHasRunAtLeastOnce)
	{

	}
	else
	{
		int i;
		for (i=0; i < eaSize(&pTask->ppCBInfos); i++)
		{
			if (pTask->ppCBInfos[i]->pCB)
			{
				fprintf(pOutFile, "\t%s();\n", pTask->ppCBInfos[i]->pCBName);
			}
		}
	}
}

void MetaTask_WriteOutFunctionCalls(const char *pMetaTaskName, char *pOutFileName)
{
	int i;
	MetaTask* pMeta = AddOrGetMetaTask(pMetaTaskName, false);

	FILE *pOutFile = fopen(pOutFileName, "wt");

	for (i=0; i < eaSize(&pMeta->ppTasks); i++)
	{
		MetaTaskSingleTask *pTask = pMeta->ppTasks[i];

		if (pTask->bStartsOutOn)
		{
			MetaTask_RecurseSetShouldRun(pMeta, pTask);
		}
	}

	if (pOutFile)
	{
		fprintf(pOutFile, "//This file is auto-generated as part of the %s task... these are all the functions that task\n//will call, in order\n\n", pMetaTaskName);

		MetaTask_DoThingsInOrder(pMetaTaskName, MetaTask_WriteOutPrototype, pOutFile);

		fprintf(pOutFile, "void Do_%s_AutoGen(void)\n{\n", pMetaTaskName);

		MetaTask_DoThingsInOrder(pMetaTaskName, MetaTask_WriteOutFunctionCall, pOutFile);

		fprintf(pOutFile, "}\n\n");

		fclose(pOutFile);
	}
}

//writes a vizgraph file
void MetaTask_WriteOutGraphFile(const char *pMetaTaskName, char *pOutFileName)
{
	MetaTask *pMetaTask = AddOrGetMetaTask(pMetaTaskName, false);
	MetaTaskSingleTask *pSingleTask;
	FILE *pOutFile = fopen(pOutFileName, "wt");
	int i, j;

	if (pOutFile)
	{
		fprintf(pOutFile, "DiGraph G\n{\n");
		
		for (i=0; i < eaSize(&pMetaTask->ppTasks); i++)
		{
			pSingleTask = pMetaTask->ppTasks[i];

			if (pSingleTask->bStartsOutOn)
			{
				fprintf(pOutFile, "\t%s -> %s;\n", pSingleTask->pTaskName, pMetaTaskName);
			}

			for (j=0; j < eaSize(&pSingleTask->ppDependencies); j++)
			{
				if (pSingleTask->ppDependencies[j]->bHeComesBefore && pSingleTask->ppDependencies[j]->bIRequireHim)
				{
					fprintf(pOutFile, "\t%s -> %s;\n", pSingleTask->ppDependencies[j]->pOtherTaskName, pSingleTask->pTaskName);
				}
			}
		}

		fprintf(pOutFile, "}\n");
		fclose(pOutFile);
	}
}


	





/*
bool bDoGraphics = false;
bool bDoCollision = false;
bool bDoPhysics = false;
bool bDoTextures = false;
bool bDoSpecHighLights = false;
bool bDoModels = false;
bool bDoSkeletons = true;
bool bDoFoo = true;
		
AUTO_RUN;
int metaTaskTest(void)
{
	MetaTask_AddTask("init", "graphics", NULL, &bDoGraphics, NULL, METATASK_BEHAVIOR_ONLY_ONCE);
	MetaTask_AddTask("init", "collision", NULL, &bDoCollision, "AFTER physics", METATASK_BEHAVIOR_ONLY_ONCE);
	MetaTask_AddTask("init", "physics", NULL, &bDoPhysics, "AFTER graphics", METATASK_BEHAVIOR_ONLY_ONCE);

	MetaTask_Register("init");

	MetaTask_AddTask("init", "textures", NULL, &bDoTextures, "AFTER graphics", METATASK_BEHAVIOR_ONLY_ONCE);
	MetaTask_AddTask("init", "spechighlights", NULL, &bDoSpecHighLights, "AFTER textures", METATASK_BEHAVIOR_ONLY_ONCE);

	MetaTask_AddTask("init", "models", NULL, &bDoModels, "BEFORE textures", METATASK_BEHAVIOR_ONLY_ONCE);
	MetaTask_AddTask("init", "skeletons", NULL, &bDoSkeletons, "BEFORE models", METATASK_BEHAVIOR_ONLY_ONCE);



	MetaTask_DoMetaTask("init");


	return 0;
}*/
	









