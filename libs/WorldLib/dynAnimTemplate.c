#include "dynAnimTemplate.h"

#include "error.h"
#include "fileutil.h"
#include "foldercache.h"
#include "StringCache.h"
#include "NameList.h"
#include "ResourceManager.h"
#include "dynSeqData.h"
#include "dynSkeleton.h"
#include "dynAnimGraph.h"


#include "dynAnimTemplate_h_ast.h"

static int bLoadedOnce = false;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Animation););

DictionaryHandle hAnimTemplateDict;

const char* pcStart;
const char* pcEnd;

static void dynAnimTemplateCreateKeywordList(DynAnimTemplate* pTemplate);
static void dynAnimTemplateFixPointers(DynAnimTemplate* pTemplate);
static DynAnimTemplateNode* dynAnimTemplateNodeFromGraphNode(DynAnimTemplate* pTemplate, DynAnimGraphNode* pNode);
static void dynAnimTemplateBuildDirectionalData(DynAnimTemplate* pTemplate);

AUTO_RUN;
void dynAnimTemplate_InitStrings(void)
{
	pcStart = allocAddStaticString("Start");
	pcEnd = allocAddStaticString("End");
}


void dynAnimTemplateInit(DynAnimTemplate* pTemplate)
{
	DynAnimGraph* pDefaultsGraph;

	DynAnimTemplateNode* pStart;
	DynAnimTemplateNode* pEnd;

	DynAnimGraphNode* pStartGraph;
	DynAnimGraphNode* pEndGraph;

	assert(eaSize(&pTemplate->eaNodes) == 0);

	pDefaultsGraph = StructCreate(parse_DynAnimGraph);
	pDefaultsGraph->bPartialGraph = true;
	pTemplate->pDefaultsGraph = pDefaultsGraph;

	pStart = StructCreate(parse_DynAnimTemplateNode);
	pStart->eType = eAnimTemplateNodeType_Start;
	pStart->pcName = pcStart;
	pStart->fX = 50.0f;
	pStart->fY = DEFAULT_ANIM_TEMPLATE_NODE_Y;
	eaPush(&pTemplate->eaNodes, pStart);

	pStartGraph = StructCreate(parse_DynAnimGraphNode);
	pStartGraph->pcName = pStart->pcName;
	pStartGraph->fX = pStart->fX;
	pStartGraph->fY = pStart->fY;
	pStartGraph->pTemplateNode = pStart;
	eaPush(&pDefaultsGraph->eaNodes, pStartGraph);

	pEnd = StructCreate(parse_DynAnimTemplateNode);
	pEnd->eType = eAnimTemplateNodeType_End;
	pEnd->pcName = pcEnd;
	pEnd->fX = 1050.0f;
	pEnd->fY = DEFAULT_ANIM_TEMPLATE_NODE_Y;
	eaPush(&pTemplate->eaNodes, pEnd);

	pEndGraph = StructCreate(parse_DynAnimGraphNode);
	pEndGraph->pcName = pEnd->pcName;
	pEndGraph->fX = pEnd->fX;
	pEndGraph->fY = pEnd->fY;
	pEndGraph->pTemplateNode = pEnd;
	eaPush(&pDefaultsGraph->eaNodes, pEndGraph);

	pTemplate->bPointersFixed = true;
	dynAnimTemplateFixIndices(pTemplate);
}

static void dynAnimTemplateRefDictCallback(enumResourceEventType eType, const char *pDictName, const char *pRefData, DynAnimTemplate* pTemplate, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED || eType == RESEVENT_RESOURCE_MODIFIED)
	{
		if (pTemplate && !pTemplate->bPointersFixed)
		{
			dynAnimTemplateFixPointers(pTemplate);
		}
	}
	if (bLoadedOnce)
		danimForceDataReload();
}


AUTO_FIXUPFUNC;
TextParserResult fixupDynAnimTemplate(DynAnimTemplate* pTemplate, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_PRE_STRUCTCOPY:
		{
			dynAnimTemplateFixIndices(pTemplate);
		}

		xcase FIXUPTYPE_POST_STRUCTCOPY:
		{
			dynAnimTemplateFixPointers(pTemplate);
		}

		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			dynAnimTemplateBuildDirectionalData(pTemplate);
			dynAnimTemplateFixPointers(pTemplate);
			if (!dynAnimTemplateVerify(pTemplate))
				return PARSERESULT_ERROR; // remove this from the costume list
		}
		xcase FIXUPTYPE_POST_BIN_READ:
		{
			dynAnimTemplateFixPointers(pTemplate);
		}
		xcase FIXUPTYPE_PRE_TEXT_WRITE:
		case FIXUPTYPE_PRE_BIN_WRITE:
		{
			dynAnimTemplateFixIndices(pTemplate);
			dynAnimTemplateFixDefaultsGraph(pTemplate);
			if (!dynAnimTemplateVerify(pTemplate))
				return PARSERESULT_ERROR; // remove this from the costume list
		}
	}
	return PARSERESULT_SUCCESS;
}

static bool dynAnimTemplateNodeConnectedHelper2(DynAnimTemplate *pTemplate, DynAnimTemplateNode *pWalk, DynAnimTemplateNode ***peaConnected)
{
	if (!pWalk)
		return false;
	if (eaFind(peaConnected, pWalk) >= 0)
		return false;
	if (pWalk->eType == eAnimTemplateNodeType_End)
		return true;
	eaPush(peaConnected, pWalk);

	switch (pWalk->eType)
	{
		xcase eAnimTemplateNodeType_Start  :
		acase eAnimTemplateNodeType_Normal :
		{
			if (dynAnimTemplateNodeConnectedHelper2(pTemplate, pWalk->defaultNext.p, peaConnected))
				return true;
			FOR_EACH_IN_EARRAY(pWalk->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (dynAnimTemplateNodeConnectedHelper2(pTemplate, pSwitch->next.p, peaConnected))
					return true;
			}
			FOR_EACH_END;
		}
		xcase eAnimTemplateNodeType_Randomizer :
		{
			FOR_EACH_IN_EARRAY(pWalk->eaPath, DynAnimTemplatePath, pPath)
			{
				if (dynAnimTemplateNodeConnectedHelper2(pTemplate,pPath->next.p, peaConnected))
					return true;
			}
			FOR_EACH_END;
		}
	}

	return false;
}

static bool dynAnimTemplateNodeConnectedHelper(DynAnimTemplate* pTemplate, DynAnimTemplateNode* pNode, DynAnimTemplateNode* pWalk, DynAnimTemplateNode*** peaConnected)
{
	if (!pWalk)
		return false;
	if (eaFind(peaConnected, pWalk) >= 0)
		return false;
	if (pWalk == pNode)
		return true;
	eaPush(peaConnected, pWalk);

	switch (pWalk->eType)
	{
		xcase eAnimTemplateNodeType_Start :
		acase eAnimTemplateNodeType_Normal:
		{
			if (dynAnimTemplateNodeConnectedHelper(pTemplate, pNode, pWalk->defaultNext.p, peaConnected))
				return true;
			FOR_EACH_IN_EARRAY(pWalk->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (dynAnimTemplateNodeConnectedHelper(pTemplate, pNode, pSwitch->next.p, peaConnected))
					return true;
			}
			FOR_EACH_END;
		}
		xcase eAnimTemplateNodeType_Randomizer :
		{
			FOR_EACH_IN_EARRAY(pWalk->eaPath, DynAnimTemplatePath, pPath)
			{
				if (dynAnimTemplateNodeConnectedHelper(pTemplate, pNode, pPath->next.p, peaConnected))
					return true;
			}
			FOR_EACH_END;
		}
		xcase eAnimTemplateNodeType_End :
		{
			if (dynAnimTemplateNodeConnectedHelper(pTemplate, pNode, pWalk->defaultNext.p, peaConnected))
				return true;
		}
	}

	return false;
}

bool dynAnimTemplateNodeConnected(DynAnimTemplate* pTemplate, DynAnimTemplateNode* pNode)
{
	DynAnimTemplateNode** eaConnected;
	DynAnimTemplateNode* pWalk;
	bool bStartResult, bEndResult;

	//check for link from start
	eaConnected = NULL;
	pWalk = pTemplate->eaNodes[0];
	assert(pWalk->eType == eAnimTemplateNodeType_Start);
	bStartResult = dynAnimTemplateNodeConnectedHelper(pTemplate, pNode, pWalk, &eaConnected);
	eaDestroy(&eaConnected);

	//check for link to end
	eaConnected = NULL;
	pWalk = pNode;
	bEndResult = dynAnimTemplateNodeConnectedHelper2(pTemplate, pWalk, &eaConnected);
	eaDestroy(&eaConnected);

	return (bStartResult && bEndResult);
}

static bool dynAnimTemplateGraphVerify(DynAnimTemplate *pTemplate)
{
	bool bRet = true;
	DynAnimGraph *pDefaultsGraph = pTemplate->pDefaultsGraph;
	if (pDefaultsGraph)
	{
		if (pDefaultsGraph->pcName != pTemplate->pcName)
		{
			AnimFileError(pTemplate->pcFilename, "Template Defaults Graph %s does not have same name as Template %s", pDefaultsGraph->pcName, pTemplate->pcName);
			bRet = false;
		}

		if (pDefaultsGraph->pcFilename != pTemplate->pcFilename)
		{
			AnimFileError(pTemplate->pcFilename, "Template Defaults Graph %s does not have same filename as Template %s", pDefaultsGraph->pcFilename, pTemplate->pcFilename);
			bRet = false;
		}

		if (pDefaultsGraph->pcScope != pTemplate->pcScope)
		{
			AnimFileError(pTemplate->pcFilename, "Template Defaults Graph %s does not have same scope as Template %s", pDefaultsGraph->pcScope, pTemplate->pcScope);
			bRet = false;
		}

		if (!pDefaultsGraph->bPartialGraph)
		{
			AnimFileError(pTemplate->pcFilename, "Template Defaults Graph %s is not marked as a partial graph", pDefaultsGraph->pcName);
			bRet = false;
		}

		if (eaSize(&pTemplate->eaNodes) != eaSize(&pDefaultsGraph->eaNodes))
		{
			AnimFileError(pTemplate->pcFilename, "Template Defaults Graph %s has %d nodes, while its Template %s has %d nodes!", pDefaultsGraph->pcName, eaSize(&pDefaultsGraph->eaNodes), pTemplate->pcName, eaSize(&pTemplate->eaNodes));
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pDefaultsGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			DynAnimTemplateNode* pTemplateNode = dynAnimTemplateNodeFromGraphNode(pTemplate, pNode);
			if (!pTemplateNode || (pTemplateNode && pTemplateNode->pcName != pNode->pcName))
			{
				AnimFileError(pTemplate->pcFilename, "Graph %s, Node %s pTemplate pointer is not correct!", pDefaultsGraph->pcName, pNode->pcName);
				bRet = false;
			}
		}
		FOR_EACH_END;

		if (!dynAnimGraphVerify(pDefaultsGraph, true))
		{
			bRet = false;
		}

		if (pTemplate->eType != eAnimTemplateType_Idle && pDefaultsGraph->fTimeout > 0)
		{
			AnimFileError(pTemplate->pcFilename, "Graph timeouts only allowed when the template type is set to idle");
			bRet = false;
		}
	}
	return bRet;
}

bool dynAnimTemplateVerify(DynAnimTemplate* pTemplate)
{
	bool bRet = true;
	bool bFoundStart = false;
	bool bFoundEnd = false;
	bool bFoundNormal = false;
	
	if(!resIsValidName(pTemplate->pcName))
	{
		AnimFileError(pTemplate->pcFilename, "Template name \"%s\" is illegal.", pTemplate->pcName);
		bRet = false;
	}
	if(!resIsValidScope(pTemplate->pcScope))
	{
		AnimFileError(pTemplate->pcFilename, "Template scope \"%s\" is illegal.", pTemplate->pcScope);
		bRet = false;
	}
	{
		const char* pcTempFileName = pTemplate->pcFilename;
		if (resFixPooledFilename(&pcTempFileName, "dyn/animtemplate", pTemplate->pcScope, pTemplate->pcName, "atemp"))
		{
			if (IsServer())
			{
				AnimFileError(pTemplate->pcFilename, "Template filename does not match name '%s' scope '%s'", pTemplate->pcName, pTemplate->pcScope);
				bRet = false;
			}
		}
	}

	if (pTemplate->eType == eAnimTemplateType_NotSet)
	{
		AnimFileError(pTemplate->pcFilename, "Must set an anim template type!");
		bRet = false;
	}
	if (!dynAnimTemplateGraphVerify(pTemplate))
	{
		bRet = false;
	}
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pNode)
	{
		if (pNode->eType == eAnimTemplateNodeType_Start)
		{
			if (bFoundStart)
			{
				AnimFileError(pTemplate->pcFilename, "Found two Start nodes!");
				bRet = false;
			}
			bFoundStart = true;
		}
		else if (pNode->eType == eAnimTemplateNodeType_End)
		{
			bFoundEnd = true;
		}
		else if (pNode->eType == eAnimTemplateNodeType_Normal)
		{
			bFoundNormal = true;
		}

		if (pTemplate->eType == eAnimTemplateType_Movement)
		{
			if (pNode->bInterruptible) {
				AnimFileError(pTemplate->pcFilename, "Movement Template Specific: Node %s is set as interruptible by movement", pNode->pcName);
				bRet = false;
			}
		}

		if (!dynAnimTemplateNodeConnected(pTemplate, pNode))
		{
			AnimFileError(pTemplate->pcFilename, "Node %s is not connected!", pNode->pcName);
			bRet = false;
		}

		if (!pNode->defaultNext.p)
		{
			if (pNode->eType != eAnimTemplateNodeType_End &&
				pNode->eType != eAnimTemplateNodeType_Randomizer)
			{
				AnimFileError(pTemplate->pcFilename, "Node %s has no default next animation!", pNode->pcName);
				bRet = false;
			}
		}
		else //pNode->defaultNext.p
		{
			if (pNode->eType == eAnimTemplateNodeType_End)
			{
				AnimFileError(pTemplate->pcFilename, "End Node %s has a default next animation!", pNode->pcName);
				bRet = false;
			}
			else if (pNode->eType == eAnimTemplateNodeType_Randomizer)
			{
				AnimFileError(pTemplate->pcFilename, "Randomizer Node %s has a default next animation!", pNode->pcName);
				bRet = false;
			}
		}

		if (eaSize(&pNode->eaSwitch) > 0 &&
				(pNode->eType == eAnimTemplateNodeType_Randomizer ||
				 pNode->eType == eAnimTemplateNodeType_End))
		{
			AnimFileError(pTemplate->pcFilename, "Node %s can not have switches due to its type", pNode->pcName);
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			if (pSwitch->eDirection < 0 ||
				pTemplate->eType != eAnimTemplateType_Movement && pSwitch->eDirection != 0 ||
				pTemplate->eType == eAnimTemplateType_Movement && DYNMOVEMENT_NUMDIRECTIONS < pSwitch->eDirection)
			{ 
				AnimFileError(pTemplate->pcFilename, "Node %s Switch %s has invalid movement direction!\n", pNode->pcName, pSwitch->pcFlag);
				bRet = false;
			}

			if (!pSwitch->next.p)
			{
				AnimFileError(pTemplate->pcFilename, "Node %s: Switch %s is unconnected!", pNode->pcName, pSwitch->pcFlag);
				bRet = false;
			}
			if (stricmp(pSwitch->pcFlag, "Default")==0)
			{
				AnimFileError(pTemplate->pcFilename, "Node %s: Duplicate Switch %s", pNode->pcName, pSwitch->pcFlag);
				bRet = false;
			}
			if (strchr(pSwitch->pcFlag, ' '))
			{
				AnimFileError(pTemplate->pcFilename, "Node %s: Switch %s has a space in the flag.", pNode->pcName, pSwitch->pcFlag);
				bRet = false;
			}
			FOR_EACH_IN_EARRAY(pNode->eaSwitch, DynAnimTemplateSwitch, pOtherSwitch)
			{
				if (pSwitch != pOtherSwitch &&
					pSwitch->pcFlag == pOtherSwitch->pcFlag &&
					pSwitch->eDirection == pOtherSwitch->eDirection)
				{
					AnimFileError(pTemplate->pcFilename, "Node %s: Duplicate Switch %s", pNode->pcName, pSwitch->pcFlag);
					bRet = false;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;

		if (eaSize(&pNode->eaPath) > 0 && pNode->eType != eAnimTemplateNodeType_Randomizer)
		{
			AnimFileError(pTemplate->pcFilename, "Node %s: is of non-randomizer type and has Paths", pNode->pcName);
			bRet = false;
		}

		FOR_EACH_IN_EARRAY(pNode->eaPath, DynAnimTemplatePath, pPath)
		{
			if (!pPath->next.p)
			{
				AnimFileError(pTemplate->pcFilename, "Node %s: RandPath %d is unconnected!", pNode->pcName, ipPathIndex);
				bRet = false;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	if (!bFoundStart)
	{
		AnimFileError(pTemplate->pcFilename, "Can't find Start Node");
		bRet = false;
	}
	if (!bFoundEnd)
	{
		AnimFileError(pTemplate->pcFilename, "Can't find End Node");
		bRet = false;
	}
	if (!bFoundNormal)
	{
		AnimFileError(pTemplate->pcFilename, "Requires at least one Normal Node");
		bRet = false;
	}

	dynAnimTemplateCreateKeywordList(pTemplate);

	return bRet;
}

static int dynAnimTemplateResValidateCB(enumResourceValidateType eType, const char* pDictName, const char* pResourceName, DynAnimTemplate* pTemplate, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_FIX_FILENAME:
		{
			resFixPooledFilename((char**)&pTemplate->pcFilename, "dyn/animtemplate", pTemplate->pcScope, pTemplate->pcName, "atemp");
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_TEXT_READING:
		{
			dynAnimTemplateFixDefaultsGraph(pTemplate);
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_POST_BINNING:
		{
			dynAnimTemplateFixDefaultsGraph(pTemplate);
		}
		return VALIDATE_HANDLED;

		xcase RESVALIDATE_FINAL_LOCATION:
		{
			dynAnimTemplateFixPointers(pTemplate);
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void registerAnimTemplateDictionaries(void)
{
	hAnimTemplateDict = RefSystem_RegisterSelfDefiningDictionary(ANIM_TEMPLATE_EDITED_DICTIONARY, false, parse_DynAnimTemplate, true, true, NULL);

	resDictManageValidation(hAnimTemplateDict, dynAnimTemplateResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(hAnimTemplateDict);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(hAnimTemplateDict, ".name", ".scope", NULL, NULL, NULL);
		}
	}
	else if (IsClient())
	{
		resDictRequestMissingResources(hAnimTemplateDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
	//resDictProvideMissingRequiresEditMode(hAnimTemplateDict);
	resDictRegisterEventCallback(hAnimTemplateDict, dynAnimTemplateRefDictCallback, NULL);
}

void dynAnimTemplateLoadAll(void)
{
	if (!bLoadedOnce)
	{
		dynAnimKeywordList = CreateNameList_Bucket();
		dynAnimFlagList = CreateNameList_Bucket();

		if (IsServer())
		{
			resLoadResourcesFromDisk(hAnimTemplateDict, "dyn/animtemplate", ".atemp", NULL, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
		}
		else if (IsClient())
		{
			loadstart_printf("Loading DynAnimTemplates...");
			ParserLoadFilesToDictionary("dyn/animtemplate", ".atemp", "DynAnimTemplate.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, hAnimTemplateDict);
			loadend_printf(" done (%d DynAnimTemplates)", RefSystem_GetDictionaryNumberOfReferents(hAnimTemplateDict));
		}
		bLoadedOnce = true;
	}
}

static DynAnimTemplateNode* dynAnimTemplateNodeFromGraphNode(DynAnimTemplate* pTemplate, DynAnimGraphNode* pNode)
{
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pTemplateNode)
	{
		if (pTemplateNode->pcName == pNode->pcName)
			return pTemplateNode;
	}
	FOR_EACH_END;
	Errorf("Could not find node %s in template %s", pNode->pcName, pTemplate->pcName);
	return NULL;
}

void dynAnimTemplateFixGraphNode(DynAnimTemplate *pTemplate, DynAnimTemplateNode *pTemplateNodeOld, DynAnimTemplateNode *pTemplateNodeNew)
{
	DynAnimGraph *pGraph = pTemplate->pDefaultsGraph;
	if (pGraph && pTemplateNodeOld)
	{
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			if (pNode->pcName == pTemplateNodeOld->pcName ||
				pNode->pTemplateNode == pTemplateNodeOld
				)
			{
				pNode->pcName = pTemplateNodeNew->pcName;
				pNode->pTemplateNode = pTemplateNodeNew;
				break;
			}
		}
		FOR_EACH_END;
	}
}

void dynAnimTemplateFixDefaultsGraph(DynAnimTemplate *pTemplate)
{
	DynAnimGraph *pGraph = pTemplate->pDefaultsGraph;
	if (pGraph)
	{
		pGraph->pcName     = pTemplate->pcName;
		pGraph->pcFilename = pTemplate->pcFilename;
		pGraph->pcScope    = pTemplate->pcScope;
		FOR_EACH_IN_EARRAY(pGraph->eaNodes, DynAnimGraphNode, pNode)
		{
			DynAnimTemplateNode* pTemplateNode;

			if (pNode->pTemplateNode)
			{
				pTemplateNode = pNode->pTemplateNode;
				pNode->pcName = pTemplateNode->pcName;
			}
			else
			{
				pNode->pTemplateNode = pTemplateNode = dynAnimTemplateNodeFromGraphNode(pTemplate, pNode);
			}

			//make sure that we've got switch data for older graphs that pre-date it
			if (!eaSize(&pNode->eaSwitch))
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pTemplateSwitch)
				{
					DynAnimGraphSwitch* pGraphSwitch = StructCreate(parse_DynAnimGraphSwitch);
					pGraphSwitch->fRequiredPlaytime = 0;
					pGraphSwitch->bInterrupt = pTemplateSwitch->bInterrupt_Depreciated;
					eaPush(&pNode->eaSwitch, pGraphSwitch);
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;
	}
	else
	{
		pGraph = StructCreate(parse_DynAnimGraph);
		pGraph->pcName     = pTemplate->pcName;
		pGraph->pcFilename = pTemplate->pcFilename;
		pGraph->pcScope    = pTemplate->pcScope;
		pGraph->bPartialGraph = true;
		FOR_EACH_IN_EARRAY_FORWARDS(pTemplate->eaNodes, DynAnimTemplateNode, pTemplateNode)
		{
			DynAnimGraphNode *pGraphNode;
			pGraphNode = StructCreate(parse_DynAnimGraphNode);
			pGraphNode->pcName = pTemplateNode->pcName;
			pGraphNode->fX = pTemplateNode->fX;
			pGraphNode->fY = pTemplateNode->fY;
			pGraphNode->pTemplateNode = pTemplateNode;
			FOR_EACH_IN_EARRAY_FORWARDS(pTemplateNode->eaSwitch, DynAnimTemplateSwitch, pTemplateSwitch)
			{
				DynAnimGraphSwitch* pGraphSwitch = StructCreate(parse_DynAnimGraphSwitch);
				pGraphSwitch->fRequiredPlaytime = 0;
				pGraphSwitch->bInterrupt = pTemplateSwitch->bInterrupt_Depreciated;
				eaPush(&pGraphNode->eaSwitch, pGraphSwitch);
			}
			FOR_EACH_END;
			eaPush(&pGraph->eaNodes, pGraphNode);
		}
		FOR_EACH_END;
		pTemplate->pDefaultsGraph = pGraph;
	}
}

static void dynAnimTemplateNodeRefFixPointer(DynAnimTemplate* pTemplate, DynAnimTemplateNodeRef* pRef)
{
	if (pRef->index >= 0)
		pRef->p = pTemplate->eaNodes[pRef->index];
	else
		pRef->p = NULL;
}


static void dynAnimTemplateNodeRefFixIndex(DynAnimTemplate* pTemplate, DynAnimTemplateNodeRef* pRef)
{
	pRef->index = -1;
	if (pRef->p)
	{
		FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pWalk)
		{
			if (pWalk == pRef->p)
			{
				pRef->index = ipWalkIndex;
				break;
			}
		}
		FOR_EACH_END;
	}
}

static void dynAnimTemplateCreateKeywordList(DynAnimTemplate* pTemplate)
{
	eaDestroy(&pTemplate->eaFlags);
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pNode)
	{
		FOR_EACH_IN_EARRAY(pNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			eaPushUnique(&pTemplate->eaFlags, pSwitch->pcFlag);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

static void dynAnimTemplateFixPointers(DynAnimTemplate* pTemplate)
{
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pWalk)
	{
		dynAnimTemplateNodeRefFixPointer(pTemplate, &pWalk->defaultNext);
		FOR_EACH_IN_EARRAY(pWalk->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			NameList_Bucket_AddName(dynAnimFlagList, pSwitch->pcFlag);
			dynAnimTemplateNodeRefFixPointer(pTemplate, &pSwitch->next);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pWalk->eaDirectionalData, DynAnimTemplateDirectionalData, pDirectionalData)
		{
			FOR_EACH_IN_EARRAY(pDirectionalData->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				// don't duplicate this, already added from non-directional version - NameList_Bucket_AddName(dynAnimFlagList, pSwitch->pcFlag);
				dynAnimTemplateNodeRefFixPointer(pTemplate, &pSwitch->next);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pWalk->eaPath, DynAnimTemplatePath, pPath)
		{
			dynAnimTemplateNodeRefFixPointer(pTemplate, &pPath->next);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	pTemplate->bPointersFixed = true;
}

void dynAnimTemplateFixIndices(DynAnimTemplate* pTemplate)
{
	assert(pTemplate->bPointersFixed);
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pWalk)
	{
		dynAnimTemplateNodeRefFixIndex(pTemplate, &pWalk->defaultNext);
		FOR_EACH_IN_EARRAY(pWalk->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			dynAnimTemplateNodeRefFixIndex(pTemplate, &pSwitch->next);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pWalk->eaDirectionalData, DynAnimTemplateDirectionalData, pDirectionalData)
		{
			FOR_EACH_IN_EARRAY(pDirectionalData->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				dynAnimTemplateNodeRefFixIndex(pTemplate, &pSwitch->next);
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pWalk->eaPath, DynAnimTemplatePath, pPath)
		{
			dynAnimTemplateNodeRefFixIndex(pTemplate, &pPath->next);
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
}

void dynAnimTemplateFreeNode(DynAnimTemplate* pTemplate, DynAnimTemplateNode* pNode)
{
	//delete the graph node version 1st (contains extra data)
	if (pTemplate->pDefaultsGraph)
	{
		DynAnimGraph* pDefaultsGraph = pTemplate->pDefaultsGraph;

		FOR_EACH_IN_EARRAY(pDefaultsGraph->eaNodes, DynAnimGraphNode, pGraphNode)
		{
			if (pGraphNode->pTemplateNode->pcName == pNode->pcName)
			{
				eaRemove(&pDefaultsGraph->eaNodes, ipGraphNodeIndex);
				pGraphNode->pTemplateNode = NULL;
				StructDestroy(parse_DynAnimGraphNode, pGraphNode);
				break;
			}
		}
		FOR_EACH_END;
	}

	//now delete the template version (contains flow info)

	if (pNode->defaultNext.p == pNode)
		pNode->defaultNext.p = NULL;

	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pWalk)
	{
		if (pWalk->defaultNext.p == pNode)
			pWalk->defaultNext.p = NULL;
		FOR_EACH_IN_EARRAY(pWalk->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			if (pSwitch->next.p == pNode)
				pSwitch->next.p = NULL;
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pWalk->eaDirectionalData, DynAnimTemplateDirectionalData, pDirectionalData)
		{
			FOR_EACH_IN_EARRAY(pDirectionalData->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				if (pSwitch->next.p == pNode)
					pSwitch->next.p = NULL;
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pWalk->eaPath, DynAnimTemplatePath, pPath)
		{
			if (pPath->next.p == pNode)
				pPath->next.p = NULL;
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	StructDestroy(parse_DynAnimTemplateNode, pNode);
}

static bool dynAnimTemplateNodesAttachedHelper(	DynAnimTemplate *pTemplate,
												DynAnimTemplateNode *pTemplateNode1,
												DynAnimTemplateNode *pTemplateNode2,
												DynAnimTemplateNode ***peaVisitedNodes,
												bool bAllowNormalNodesInbetween)
{
	DynAnimTemplateNode *pDefaultNext;
	bool bRet = false;
	assert(pTemplateNode1->defaultNext.index < eaSize(&pTemplate->eaNodes));
	pDefaultNext = pTemplate->eaNodes[pTemplateNode1->defaultNext.index];

	FOR_EACH_IN_EARRAY(*peaVisitedNodes, DynAnimTemplateNode, pCheckNode)
	{
		if (pTemplateNode1 == pCheckNode)
			return false;
	}
	FOR_EACH_END;
	eaPush(peaVisitedNodes, pTemplateNode1);

	if (pDefaultNext == pTemplateNode2)
	{
		return true;
	}
	else if(pDefaultNext &&
			pDefaultNext->eType != eAnimTemplateNodeType_End &&
			(	bAllowNormalNodesInbetween ||
				pDefaultNext->eType != eAnimTemplateNodeType_Normal))
	{
		bRet |= dynAnimTemplateNodesAttachedHelper(pTemplate, pDefaultNext, pTemplateNode2, peaVisitedNodes, bAllowNormalNodesInbetween);
	}

	if (!bRet)
	{
		FOR_EACH_IN_EARRAY(pTemplateNode1->eaSwitch, DynAnimTemplateSwitch, pSwitch)
		{
			DynAnimTemplateNode *pSwitchedNode;
			assert(pSwitch->next.index < eaSize(&pTemplate->eaNodes));
			pSwitchedNode = pTemplate->eaNodes[pSwitch->next.index];

			if (pSwitchedNode == pTemplateNode2)
			{
				return true;
			}
			else if (	pSwitchedNode &&
						pSwitchedNode->eType != eAnimTemplateNodeType_End &&
						(	bAllowNormalNodesInbetween ||
							pSwitchedNode->eType != eAnimTemplateNodeType_Normal))
			{
				bRet |= dynAnimTemplateNodesAttachedHelper(pTemplate, pSwitchedNode, pTemplateNode2, peaVisitedNodes, bAllowNormalNodesInbetween);
			}
		}
		FOR_EACH_END;

		if (!bRet)
		{
			FOR_EACH_IN_EARRAY(pTemplateNode1->eaPath, DynAnimTemplatePath, pPath)
			{
				DynAnimTemplateNode *pPathedNode;
				assert(pPath->next.index < eaSize(&pTemplate->eaNodes));
				pPathedNode = pTemplate->eaNodes[pPath->next.index];

				if (pPathedNode == pTemplateNode2)
				{
					return true;
				}
				else if (	pPathedNode &&
							pPathedNode->eType != eAnimTemplateNodeType_End &&
							(	bAllowNormalNodesInbetween ||
								pPathedNode->eType != eAnimTemplateNodeType_Normal))
				{
					bRet |= dynAnimTemplateNodesAttachedHelper(pTemplate, pPathedNode, pTemplateNode2, peaVisitedNodes, bAllowNormalNodesInbetween);
				}
			}
			FOR_EACH_END;
		}
	}

	return bRet;
}

bool dynAnimTemplateNodesAttached(	DynAnimTemplate *pTemplate,
									DynAnimTemplateNode *pTemplateNode1,
									DynAnimTemplateNode* pTemplateNode2,
									bool bAllowNormalNodesInbetween)
{
	DynAnimTemplateNode** eaVisitedNodes = NULL;
	bool bRet;
	eaCreate(&eaVisitedNodes);
	bRet = dynAnimTemplateNodesAttachedHelper(pTemplate, pTemplateNode1, pTemplateNode2, &eaVisitedNodes, bAllowNormalNodesInbetween);
	eaDestroy(&eaVisitedNodes);
	return bRet;
}

bool dynAnimTemplateHasMultiFlagStartNode(DynAnimTemplate* pTemplate)
{
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pNode)
	{
		if (pNode->eType == eAnimTemplateNodeType_Start &&
			eaSize(&pNode->eaSwitch) > 0)
		{
			return true;
		}
	}
	FOR_EACH_END;
	return false;
}

static void dynAnimTemplateBuildDirectionalData(DynAnimTemplate* pTemplate)
{
	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pNode)
	{
		if (pNode->eaDirectionalData) {
			FOR_EACH_IN_EARRAY(pNode->eaDirectionalData, DynAnimTemplateDirectionalData, pDirectionalData) {
				StructDestroy(parse_DynAnimTemplateDirectionalData, pDirectionalData);
			} FOR_EACH_END;
			eaDestroy(&pNode->eaDirectionalData);
		}

		if (pTemplate->eType == eAnimTemplateType_Movement)
		{
			U32 i;
			for (i = 0; i < DYNMOVEMENT_NUMDIRECTIONS; i++) {
				eaPush(&pNode->eaDirectionalData, StructCreate(parse_DynAnimTemplateDirectionalData));
			}
		
			FOR_EACH_IN_EARRAY(pNode->eaSwitch, DynAnimTemplateSwitch, pSwitch)
			{
				assert(pSwitch->eDirection < eaSize(&pNode->eaDirectionalData));
				eaPush(&pNode->eaDirectionalData[pSwitch->eDirection]->eaSwitch, StructClone(parse_DynAnimTemplateSwitch,pSwitch));
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
}

int dynAnimTemplateCompareSwitchDisplayOrder(const void** pa, const void** pb)
{
	const DynAnimTemplateSwitch *a = *(const DynAnimTemplateSwitch **)pa;
	const DynAnimTemplateSwitch *b = *(const DynAnimTemplateSwitch **)pb;

	S32 iOrderA = dynMovementDirectionDisplayOrder(a->eDirection);
	S32 iOrderB = dynMovementDirectionDisplayOrder(b->eDirection);

	const char *pcA = NULL_TO_EMPTY(a->pcFlag);
	const char *pcB = NULL_TO_EMPTY(b->pcFlag);

	if (iOrderA < iOrderB)
		return 1;
	if (iOrderB < iOrderA)
		return -1;
	return stricmp(pcA, pcB);
}

static int dynAnimTemplateNodeGetSearchStringCount(const DynAnimTemplateNode *pNode, const char *pcSearchText)
{
	int count = 0;

	if (pNode->pcName	&& strstri(pNode->pcName,	pcSearchText))	count++;

	//other fields ignored

	return count;
}

int dynAnimTemplateGetSearchStringCount(const DynAnimTemplate* pTemplate, const char* pcSearchText)
{
	int count = 0;
	
	if (pTemplate->pcName		&& strstri(pTemplate->pcName,		pcSearchText))	count++;
	if (pTemplate->pcFilename	&& strstri(pTemplate->pcFilename,	pcSearchText))	count++;
	if (pTemplate->pcComments	&& strstri(pTemplate->pcComments,	pcSearchText))	count++;
	if (pTemplate->pcScope		&& strstri(pTemplate->pcScope,		pcSearchText))	count++;

	if      (pTemplate->eType == eAnimTemplateType_NotSet		&& strstri("NotSet",	pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_Idle			&& strstri("Idle",		pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_Emote		&& strstri("Emote",		pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_Power		&& strstri("Power",		pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_HitReact		&& strstri("HitReact",	pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_Death		&& strstri("Death",		pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_NearDeath	&& strstri("NearDeath",	pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_Block		&& strstri("Block",		pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_TPose		&& strstri("Death",		pcSearchText))	count++;
	else if (pTemplate->eType == eAnimTemplateType_PowerInterruptingHitReact && strstri("PowerInterruptingHitReact", pcSearchText)) count++;
	else if (pTemplate->eType == eAnimTemplateType_Movement		&& strstri("Movement",	pcSearchText))	count++;

	//defaults graph ignored

	FOR_EACH_IN_EARRAY(pTemplate->eaNodes, DynAnimTemplateNode, pNode) {
		count += dynAnimTemplateNodeGetSearchStringCount(pNode, pcSearchText);
	} FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pTemplate->eaFlags, const char, pcFlag) {
		if (pcFlag && strstri(pcFlag, pcSearchText)) count++;
	} FOR_EACH_END;

	//pointers fixed ignored

	return count;
}

#include "dynAnimTemplate_h_ast.c"