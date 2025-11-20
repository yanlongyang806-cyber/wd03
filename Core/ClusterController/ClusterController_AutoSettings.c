#include "sysutil.h"
#include "file.h"
#include "memorymonitor.h"
#include "foldercache.h"
#include "winutil.h"
#include "gimmeDLLWrapper.h"
#include "serverlib.h"
#include "UtilitiesLib.h"
#include "ResourceInfo.h"
#include "timing.h"

#include "GenericHttpServing.h"
#include "StringCache.h"

#include "controllerpub_h_ast.h"
#include "structNet.h"
#include "ClusterController_c_ast.h"
#include "ResourceInfo.h"
#include "Alerts.h"
#include "TimedCallback.h"
#include "StringUtil.h"
#include "structDefines.h"
#include "cmdparse.h"
#include "logging.h"
#include "ClusterController.h"
#include "ClusterController_h_ast.h"
#include "ClusterController_Commands.h"
#include "..\..\libs\serverlib\pub\shardCluster.h"
#include "SentryServerComm.h"
#include "ClusterController_AutoSettings.h"
#include "StringCache.h"
#include "ClusterController_AutoSettings_c_ast.h"
#include "ResourceInfo.h"
#include "../../libs/serverlib/AutoSettings.h"


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Comment, Errors, LinkToSettings");
typedef struct ClusterControllerAutoSettingCategory
{
	const char *pCategoryName; AST(KEY POOL_STRING)
	char *pComment;
	char *pErrors; AST(ESTRING, FORMATSTRING(HTML_CLASS_IFEXPR = "$ ; divWarning2"))
	char *pLinkToSettings; AST(ESTRING, FORMATSTRING(HTML=1))
} ClusterControllerAutoSettingCategory;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Set, RestoreToDefault, CategoryName, Comment, Errors, CurValue, DefaultValue");
typedef struct ClusterControllerAutoSetting
{
	const char *pCommandName; AST(KEY POOL_STRING)
	const char *pCategoryName; AST(POOL_STRING)
	char *pComment;
	char *pErrors; AST(ESTRING, FORMATSTRING(HTML_CLASS_IFEXPR = "$ ; divWarning2"))
	char *pCurValue; //blank if it varies between shards
	char *pDefaulValue; //blank if it varies between shards

	AST_COMMAND("Set", "SendCommandToShard All SetAutoSetting $FIELD(CommandName) $STRING(New value)")
	AST_COMMAND("RestoreToDefault", "SendCommandToShard All RestoreAutoSetting $FIELD(CommandName)")
	
} ClusterControllerAutoSetting;


StashTable sClusterControllerAutoSettingCategoriesByName = NULL;
StashTable sClusterControllerAutoSettingsByName = NULL;

void CreateLocalSetting(ClusterControllerAutoSettingCategory *pCategory, const char *pCmdName)
{
	ClusterControllerAutoSetting *pSetting;

	char *pExistenceErrors = NULL;

	bool bValuesDontMatch = false;
	bool bDefaultsDontMatch = false;
	bool bCommentsDontMatch = false;

	char *pAllShardsValues = NULL;
	char *pAllShardsDefaults = NULL;

	if (stashFindPointer(sClusterControllerAutoSettingsByName, pCmdName, &pSetting))
	{
		estrPrintf(&pSetting->pErrors, "This command appears on different shards in at least two different categories (%s and %s). This is fairly unrecoverable",
			pCategory->pCategoryName, pSetting->pCategoryName);
		return;
	}

	pSetting = StructCreate(parse_ClusterControllerAutoSetting);
	pSetting->pCommandName = pCmdName;
	pSetting->pCategoryName = pCategory->pCategoryName;

	stashAddPointer(sClusterControllerAutoSettingsByName, pCmdName, pSetting, false);

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		ControllerAutoSetting_Category *pCategoryInShard = eaIndexedGetUsingString(&pShard->ppAutoSettingCategories, pCategory->pCategoryName);
		bool bFound = false;

		if (pCategoryInShard)
		{
			FOR_EACH_IN_EARRAY(pCategoryInShard->ppSettings, AutoSetting_SingleSetting, pSettingInShard)
			{
				if (pSettingInShard->pCmdName == pSetting->pCommandName)
				{
					bFound = true;

					if (pSetting->pCurValue)
					{
						if (stricmp(pSetting->pCurValue, pSettingInShard->pCurValueString) == 0)
						{
							//do nothing
						}
						else
						{
							SAFE_FREE(pSetting->pCurValue);
							bValuesDontMatch = true;
						}
					}
					else if (!bValuesDontMatch)
					{
						pSetting->pCurValue = strdup(pSettingInShard->pCurValueString);
					}

					estrConcatf(&pAllShardsValues, "%s%s:%s", estrLength(&pAllShardsValues) ? ", " : "", pShard->pShardName, pSettingInShard->pCurValueString);


					if (pSetting->pDefaulValue)
					{
						if (stricmp(pSetting->pDefaulValue, pSettingInShard->pDefaultValueString) == 0)
						{
							//do nothing
						}
						else
						{
							SAFE_FREE(pSetting->pDefaulValue);
							bDefaultsDontMatch = true;
						}
					}
					else if (!bDefaultsDontMatch)
					{
						pSetting->pDefaulValue = strdup(pSettingInShard->pDefaultValueString);
					}

					estrConcatf(&pAllShardsDefaults, "%s%s:%s", estrLength(&pAllShardsDefaults) ? ", " : "", pShard->pShardName, pSettingInShard->pDefaultValueString);

					if (pSetting->pComment)
					{
						if (stricmp(pSetting->pComment, pSettingInShard->pComment) == 0)
						{
							//do nothing
						}
						else
						{
							bCommentsDontMatch = true;
						}
					}
				}
			}
			FOR_EACH_END;
		}
		
		if (!bFound)
		{
			if (estrLength(&pExistenceErrors))
			{
				estrConcatf(&pExistenceErrors, ", %s", pShard->pShardName);
			}
			else
			{
				estrPrintf(&pExistenceErrors, "This command is missing from one or more shards: %s", pShard->pShardName);
			}
		}
	}
	FOR_EACH_END;

	if (pExistenceErrors)
	{
		estrPrintf(&pSetting->pErrors, "%s\n", pExistenceErrors);
	}

	if (bValuesDontMatch)
	{
		estrConcatf(&pSetting->pErrors, "Values don't match between shards: %s\n", pAllShardsValues);
	}

	if (bDefaultsDontMatch)
	{
		estrConcatf(&pSetting->pErrors, "Defaults don't match between shards: %s\n", pAllShardsDefaults);
	}

	if (bCommentsDontMatch)
	{
		estrConcatf(&pSetting->pErrors, "Comments don't match between shards... worrisome but non-fatal");
	}


	estrDestroy(&pExistenceErrors);
	estrDestroy(&pAllShardsValues);
	estrDestroy(&pAllShardsDefaults);

}

void CreateLocalCategory(const char *pCategoryName)
{
	ClusterControllerAutoSettingCategory *pCategory;
	bool bCommentErrors = false;
	const char **ppUniqueCommandNames = NULL;
	int i;

	if (!sClusterControllerAutoSettingCategoriesByName)
	{
		sClusterControllerAutoSettingCategoriesByName = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("AutoSettingCategories", RESCATEGORY_SYSTEM, 0, sClusterControllerAutoSettingCategoriesByName, parse_ClusterControllerAutoSettingCategory);
		resDictSetHTMLExtraCommand("AutoSettingCategories", "Reload AUTO_SETTINGs from shards", "RequestAutoSettings");
	}	
	
	if (!sClusterControllerAutoSettingsByName)
	{
		sClusterControllerAutoSettingsByName = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("AutoSettings", RESCATEGORY_SYSTEM, 0, sClusterControllerAutoSettingsByName, parse_ClusterControllerAutoSetting);
		resDictSetHTMLExtraCommand("AutoSettings", "Reload AUTO_SETTINGs from shards", "RequestAutoSettings");
	}

	pCategory = StructCreate(parse_ClusterControllerAutoSettingCategory);

	pCategory->pCategoryName = pCategoryName;
	stashAddPointer(sClusterControllerAutoSettingCategoriesByName, pCategory->pCategoryName, pCategory, true);
	estrPrintf(&pCategory->pLinkToSettings, "<a href=\"%s.globObj.AutoSettings&svrFilter=me.categoryName+%%3D+&quot;%s&quot;\">Settings</a>",
		LinkToThisServer(), pCategoryName);

//&svrFilter=me.categoryname+%3D+"Misc"

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		ControllerAutoSetting_Category *pCategoryInShard = eaIndexedGetUsingString(&pShard->ppAutoSettingCategories, pCategory->pCategoryName);
		if (pCategoryInShard)
		{
			if (pCategory->pComment)
			{
				if (stricmp(pCategory->pComment, pCategoryInShard->pComment) != 0)
				{
					bCommentErrors = true;
				}
			}
			else
			{
				if (pCategoryInShard->pComment)
				{
					pCategory->pComment = strdup(pCategoryInShard->pComment);
				}
				else
				{
					pCategory->pComment = strdup("");
				}
			}

			FOR_EACH_IN_EARRAY(pCategoryInShard->ppSettings, AutoSetting_SingleSetting, pSetting)
			{
				eaPushUnique(&ppUniqueCommandNames, pSetting->pCmdName);
			}
			FOR_EACH_END;		
		}
		else
		{
			if (!estrLength(&pCategory->pErrors))
			{
				estrPrintf(&pCategory->pErrors, "ERROR -- This category does not exist for one or more shards: %s", 
					pShard->pShardName);
			}
			else
			{
				estrConcatf(&pCategory->pErrors, ", %s", pShard->pShardName);
			}


				
		}


	}
	FOR_EACH_END;
	
	for (i = 0; i < eaSize(&ppUniqueCommandNames); i++)
	{
		CreateLocalSetting(pCategory, ppUniqueCommandNames[i]);
	}

	eaDestroy(&ppUniqueCommandNames);

	if (!pCategory->pErrors && bCommentErrors)
	{
		estrCopy2(&pCategory->pErrors, "The comment strings do not agree between all shards. This is weird but non-fatal");
	}
	
}



void ClusterControllerAutoSettings_Update(void)
{
	const char **ppCategoryNames = NULL;
	int i;

	stashTableClearStruct(sClusterControllerAutoSettingCategoriesByName, NULL, parse_ClusterControllerAutoSettingCategory);
	stashTableClearStruct(sClusterControllerAutoSettingsByName, NULL, parse_ClusterControllerAutoSetting);

	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		FOR_EACH_IN_EARRAY(pShard->ppAutoSettingCategories, ControllerAutoSetting_Category, pCategory)
		{
			eaPushUnique(&ppCategoryNames, allocAddString(pCategory->pName));
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;
	
	for (i = 0; i < eaSize(&ppCategoryNames); i++)
	{
		CreateLocalCategory(ppCategoryNames[i]);
	}

	eaDestroy(&ppCategoryNames);
}

void HandleHereAreAutoSettings(Packet *pPak, Shard *pShard)
{
	eaClearStruct(&pShard->ppAutoSettingCategories, parse_ControllerAutoSetting_Category);
	while (pktGetBits(pPak, 1))
	{
		eaPush(&pShard->ppAutoSettingCategories, ParserRecvStructSafe_Create(parse_ControllerAutoSetting_Category, pPak));
	}
	ClusterControllerAutoSettings_Update();

}

AUTO_COMMAND;
void RequestAutoSettings(void)
{
	FOR_EACH_IN_STASHTABLE(gShardsByName, Shard, pShard)
	{
		eaClearStruct(&pShard->ppAutoSettingCategories, parse_ControllerAutoSetting_Category);
		if (pShard->pLink)
		{
			Packet *pPak = pktCreate(pShard->pLink, CLUSTERCONTROLLER_TO_CONTROLLER__REQUEST_AUTO_SETTINGS);
			pktSend(&pPak);
		}
	}
	FOR_EACH_END;
}


#include "ClusterController_AutoSettings_c_ast.c"
