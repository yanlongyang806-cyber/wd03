//
// MicroTransEditor.c
//

#ifndef NO_EDITORS

#include "CSVExport.h"
#include "CSVExport_h_ast.h"
#include "EString.h"
#include "GamePermissionsCommon.h"
#include "MicroTransactions.h"
#include "MicroTransactionEditor.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "ObjPath.h"
#include "rewardCommon.h"
#include "StringCache.h"
#include "sysutil.h"
#include "ResourceSearch.h"

#include "AutoGen/MicroTransactions_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#define MTE_GROUP_MAIN				"Main"

#define MTE_SUBGROUP_PART			"Part"
#define MTE_SUBGROUP_PART_ITEM		"PartItem"
#define MTE_SUBGROUP_PART_COSTUME	"PartCostume"
#define MTE_SUBGROUP_PART_POWER		"PartPower"
#define MTE_SUBGROUP_PART_ATTRIB	"PartAttrib"
#define MTE_SUBGROUP_PART_SPECIES	"PartSpecies"
#define MTE_SUBGROUP_PART_PERMISSION	"PartPermission"
#define MTE_SUBGROUP_PART_REWARD	"PartReward"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------

static MEWindow *mteWindow = NULL;
static int mteSubgroupPartId = 0;

static void MTEFixMessage(Message *pmsg, const char *pchMicroTransactionName, const char *pchKey, const char *pchDesc, const char *pchScope)
{
	char buf[1024];

	sprintf(buf, "MicroTransactionDef.%s.%s", pchKey, pchMicroTransactionName);
	pmsg->pcMessageKey = allocAddString(buf);

	StructFreeString(pmsg->pcDescription);
	pmsg->pcDescription = StructAllocString(pchDesc);

	pmsg->pcScope = allocAddString(pchScope);

	// Leave pcFilename alone
}

static void mte_fixMessages(MicroTransactionDef *pMicroTransactionDef)
{
	char *pchScope = NULL;

	estrStackCreate(&pchScope);

	estrPrintf(&pchScope,"MicroTransactionDef");
	if(pMicroTransactionDef->pchScope)
	{
		char *p = NULL;
		estrConcatf(&pchScope,"/%s",pMicroTransactionDef->pchScope);
		while((p = strchr(pchScope,'.')) != NULL)
		{
			*p = '/';
		}
	}

	MTEFixMessage(pMicroTransactionDef->displayNameMesg.pEditorCopy,pMicroTransactionDef->pchName,"Name","Microtransaction name",pchScope);
	MTEFixMessage(pMicroTransactionDef->descriptionShortMesg.pEditorCopy,pMicroTransactionDef->pchName,"ShortDesc","Description of the MicroTransaction (Used in tooltips)",pchScope);
	MTEFixMessage(pMicroTransactionDef->descriptionLongMesg.pEditorCopy,pMicroTransactionDef->pchName,"LongDesc","Long Description of the MicroTransaction (Shown in the full details)",pchScope);

	estrDestroy(&pchScope);
}

static S32 mte_ResolvePath(ParseTable *pParseTable, char *pcField, void *pData, ParseTable **ppResultParseTable, int *piResultCol, void **ppResultData)
{
	if (pcField[0] == '.') {
		int index;
		if (!objPathResolveField(pcField,pParseTable,pData,ppResultParseTable,piResultCol,ppResultData,&index, 0)) {
			char buf[1024];
			sprintf(buf, "Field is missing and could not be created: %s", pcField);
			assertmsg(0, buf);
			return 0;
		}
		return 1;
	} else if (pcField[0] == '@') {
		// This is used for polymorphic fields, expecting format "@base.path@inside.poly.path"
		char buf[260];
		char *pos;
		int index;

		strcpy(buf, pcField);
		buf[0] = '.';
		pos = strchr(buf,'@');
		if (pos) {
			*pos = '\0';
		}
		if (!objPathResolveField(buf,pParseTable,pData,ppResultParseTable,piResultCol,ppResultData,&index,0)) {
			char buf2[1024];
			sprintf(buf2, "Field is missing and could not be created: %s", pcField);
			assertmsg(0, buf2);
			return 0;
		}
		if (*ppResultData) {
			// Get here if poly field exists and is non-null, now test if whole field is valid
			if (pos) {
				*pos = '.';
			}
			if (objPathResolveField(buf,pParseTable,pData,ppResultParseTable,piResultCol,ppResultData,&index,0)) {
				// Subfield exists, so we have valid data
				return 1;
			}
			// Subfield does not exist in this poly form so fall through to return failure
		}
		*ppResultParseTable = NULL;
		*piResultCol = -1;
		*ppResultData = NULL;
		return 0;
	} else {
		if (!ParserFindColumn(pParseTable, pcField, piResultCol)) {
			char buf[1024];
			sprintf(buf, "Field is missing: %s", pcField);
			assertmsg(0, buf);
			return 0;
		}
		*ppResultParseTable = pParseTable;
		*ppResultData = pData;
		return 1;
	}
}

void mte_clearNotApplicableColumns(METable *pTable, MicroTransactionDef *pMicroTransactionDef)
{
	int i, j;
	for (i = eaSize(&pTable->eaRows)-1; i >= 0; i--)
	{
		const char *pcObjectName = met_getObjectName(pTable, i);
		if(stricmp(pcObjectName, pMicroTransactionDef->pchName)==0)
		{
			METableRow *pRow = pTable->eaRows[i];
			METableSubTableData *pSubTableData = pRow->eaSubData[mteSubgroupPartId];
			MESubTable *pSubTable = pTable->eaSubTables[mteSubgroupPartId];

			for (j = eaSize(&pRow->eaFields)-1; j >= 0; j--)
			{
				MEField *pField = pRow->eaFields[j];
				MEColData *pColData = pTable->eaCols[j];
				if (SAFE_MEMBER(pField, bNotApplicable))
				{
					void *pStruct = NULL;
					ParseTable *pTableOut = NULL;
					int iColOut;

					if(mte_ResolvePath(parse_MicroTransactionDef, pColData->pcPTName, pMicroTransactionDef,
										&pTableOut, &iColOut, &pStruct))
					{
						if(pStruct && pTableOut && iColOut >= 0)
							FieldClear(pTableOut, iColOut, pStruct, 0);
					}
				}
			}

			for(j = eaSize(&pSubTableData->eaRows)-1; j >= 0; j--)
			{
				int k;
				METableSubRow *pSubRow = pSubTableData->eaRows[j];
				for(k = eaSize(&pSubRow->eaFields)-1; k>= 0; k--)
				{
					MEField *pField = pSubRow->eaFields[k];
					MESubColData *pSubColData = pSubTable->eaCols[k];
				
					
					if(SAFE_MEMBER(pField, bNotApplicable))
					{
						void *pStruct = NULL;
						ParseTable *pTableOut = NULL;
						int iColOut;

						if(mte_ResolvePath(parse_MicroTransactionPart, pSubColData->pcPTName, pMicroTransactionDef->eaParts[j],
											&pTableOut, &iColOut, &pStruct))
						{
							if(pStruct && pTableOut && iColOut >= 0)
								FieldClear(pTableOut, iColOut, pStruct, 0);
						}
					}
				}
			}
		}
	}
}


static void mte_postOpenCallback(METable *pTable, MicroTransactionDef *pMicroTransactionDef, MicroTransactionDef *pOrigMicroTransactionDef)
{
	mte_fixMessages(pMicroTransactionDef);

	if (pOrigMicroTransactionDef) 
		mte_fixMessages(pOrigMicroTransactionDef);
}

static void mte_ASNRegenCallback(METable *pTable, MicroTransactionDef *pDef, void *pUserData, bool bInitNotify)
{
	if(bInitNotify)
		return;

	if(!pDef || pDef->bOldProductName)
		return;

	if(pDef->pchProductIdentifier)
	{
		char *estrBuffer = NULL;
		estrBuffer = estrStackCreateFromStr(pDef->pchProductIdentifier);
		estrMakeAllAlphaNumAndUnderscores(&estrBuffer);
		StructFreeStringSafe(&pDef->pchProductIdentifier);
		pDef->pchProductIdentifier = StructAllocString(estrBuffer);
		estrDestroy(&estrBuffer);
	}
	else
	{
		pDef->pchProductIdentifier = StructAllocString("");
	}

	microtrans_GenerateProductConfigs(pDef);
}

static void mte_preSaveCallback(METable *pTable,  MicroTransactionDef *pMicroTransactionDef)
{
	mte_ASNRegenCallback(pTable, pMicroTransactionDef, NULL, false);
	mte_clearNotApplicableColumns(pTable, pMicroTransactionDef);
	mte_fixMessages(pMicroTransactionDef);
}

static void *mte_createMicroTransactionPart(METable *pTable, MicroTransactionDef *pMicroTransactionDef, MicroTransactionPart *pPartToClone, MicroTransactionPart *pBeforePart, MicroTransactionPart *pAfterPart)
{
	MicroTransactionPart *pNewPart;

	// Allocate the object
	if (pPartToClone) {
		pNewPart = (MicroTransactionPart*)StructClone(parse_MicroTransactionPart, pPartToClone);
	} else {
		pNewPart = (MicroTransactionPart*)StructCreate(parse_MicroTransactionPart);
	}

	assertmsg(pNewPart, "Failed to create micro transaction part");

	return pNewPart;
}

static void *mte_createObject(METable *pTable, MicroTransactionDef *pObjectToClone, char *pcNewName, bool bCloneKeepsKeys)
{
	MicroTransactionDef *pNewDef = NULL;
	char buf[128];
	const char *pcBaseName;

	// Create the object
	if (pObjectToClone) {
		pNewDef = StructClone(parse_MicroTransactionDef, pObjectToClone);
		pcBaseName = pObjectToClone->pchName;
	} else {
		pNewDef = StructCreate(parse_MicroTransactionDef);

		pcBaseName = "_New_MicroTransaction";
	}
	// Use provided name if available
	if (pcNewName) {
		pcBaseName = pcNewName;
	}

	assertmsg(pNewDef, "Failed to create MicroTransaction");

	// Assign a new name
	pNewDef->pchName = (char*)METableMakeNewNameShared(pTable, pcBaseName, true);

	// Assign a file
	sprintf(buf,"%s/%s.%s",
		MICROTRANSACTIONS_BASE_DIR,
		pNewDef->pchName,
		MICROTRANSACTIONS_EXTENSION);

	pNewDef->pchFile = (char*)allocAddString(buf);

	return pNewDef;
}


static void *mte_tableCreateCallback(METable *pTable, MicroTransactionDef *pObjectToClone, bool bCloneKeepsKeys)
{
	return mte_createObject(pTable, pObjectToClone, NULL, bCloneKeepsKeys);
}


static void *mte_windowCreateCallback(MEWindow *pWindow, MicroTransactionDef *pObjectToClone)
{
	return mte_createObject(pWindow->pTable, pObjectToClone, NULL, false);
}


static void mte_dictChangeCallback(enumResourceEventType eType, const char *pDictName, ConstReferenceData pRefData, Referent pReferent, METable *pTable)
{
	microtrans_BuyContextSetup(0, NULL, NULL, false);
	METableDictChanged(pTable, eType, pReferent, pRefData);
}

static void mte_messageDictChangeCallback(enumResourceEventType eType, const char *pDictName, const char *pcMessageKey, Referent pReferent, METable *pTable)
{
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {

			METableMessageChangedRefresh(pTable, pcMessageKey);
	}
}

static int mte_validateCallback(METable *pTable, MicroTransactionDef *pMicroTransactionDef, void *pUserData)
{
	return microtransdef_Validate(pMicroTransactionDef);
}

static void mte_typeChangeCallback(METable *pTable, MicroTransactionDef *pMicroTransactionDef, MicroTransactionPart *pPart, void *pUserData, bool bInitNotify)
{
	//Set everything to not applicable
	MESubTable *pSubTable = eaGet(&pTable->eaSubTables,mteSubgroupPartId);
	if(pSubTable)
	{
		int i;
		for(i=eaSize(&pSubTable->eaCols)-1;i>=0;i--)
		{
			bool bHide = true;
			MESubColData *pCol = eaGet(&pSubTable->eaCols,i);
			const char *pchTitle = NULL;

			if(pCol->flags & ME_STATE_HIDDEN)
				continue;

			if(!pCol->pListColumn)
				continue;

			pchTitle = ui_ListColumnGetTitle(pCol->pListColumn);

			if(!stricmp(pCol->pcGroup, MTE_SUBGROUP_PART))
			{
				if(stricmp(pchTitle, "Special Type")==0)
					bHide = (pPart->ePartType != kMicroPart_Special);
				else
					bHide = false;
			}
			else
			{
				switch(pPart->ePartType)
				{
				case kMicroPart_Attrib:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_ATTRIB));
						break;
					}
				case kMicroPart_Costume:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_ITEM));
						break;
					}
				case kMicroPart_CostumeRef:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_COSTUME));
						break;
					}
				case kMicroPart_Item:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_ITEM));
						break;
					}
				case kMicroPart_RewardTable:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_REWARD));
						break;
					}
				case kMicroPart_Special:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART));
						break;
					}
				case kMicroPart_VanityPet:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_POWER));
						break;
					}
				case kMicroPart_Species:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_SPECIES));
						break;
					}
				case kMicroPart_Permission:
					{
						bHide = (stricmp(pCol->pcGroup, MTE_SUBGROUP_PART_PERMISSION));
						break;
					}
				}
			}

			METableSetSubFieldNotApplicable(pTable, pMicroTransactionDef, mteSubgroupPartId, pPart, pchTitle, bHide);
		}	
	}
}

static char** mte_getMTCategoryNames(METable *pTable, void *pUnused)
{
	char **eaCategoryNames = NULL;

	microtranscategory_FillNameEArray(&eaCategoryNames);

	return eaCategoryNames;
}

static char **mte_getPermissions(METable *pTable, void *pUnused)
{
	char **eaPermissions = NULL;

	gamePermissions_FillNameEArray(&eaPermissions);

	return eaPermissions;
}

void mte_ExportImportOkayClicked(CSVConfig *pCSVConfig)
{
	S32 i, j;

	pCSVConfig->pchDictionary = StructAllocString("MicroTransactionDef");
	pCSVConfig->pchScopeColumnName = StructAllocString("Scope");
	pCSVConfig->pchStructName = StructAllocString("MicroTransactionDef");

	for (i = eaSize(&pCSVConfig->eaColumns) - 1; i >= 0; --i)
	{
		if (!stricmp(pCSVConfig->eaColumns[i]->pchTitle, "Categories"))
		{
			//Fix the title
			StructFreeStringSafe(&pCSVConfig->eaColumns[i]->pchTitle);
			pCSVConfig->eaColumns[i]->pchTitle = StructAllocString("categories");

			//add the fixup settings for this column to add "FC." or "ST." to the start of the categories
			pCSVConfig->eaColumns[i]->bRemoveWhitespace = true;

			for (j = 0; j < eaSize(&g_MicroTransConfig.ppShardConfigs); ++j)
			{
				char buffer[64];
				sprintf(buffer, "%s.",g_MicroTransConfig.ppShardConfigs[j]->pchCategoryPrefix);
				if (eaFindString(&pCSVConfig->eaColumns[i]->ppchFixes, buffer) == -1)
					eaPush(&pCSVConfig->eaColumns[i]->ppchFixes, StructAllocString(buffer));
			}

			pCSVConfig->eaColumns[i]->bPrefix = true;
			pCSVConfig->eaColumns[i]->bTokens = true;
		}
		else if (!stricmp(pCSVConfig->eaColumns[i]->pchTitle, "Price"))
		{
			StructFreeStringSafe(&pCSVConfig->eaColumns[i]->pchTitle);
			pCSVConfig->eaColumns[i]->pchTitle = StructAllocString("prices");

			for (j = 0; j < eaSize(&g_MicroTransConfig.ppShardConfigs); ++j)
			{
				char buffer[64];
				sprintf(buffer, " _%s", g_MicroTransConfig.ppShardConfigs[j]->pchCurrency);
				if (eaFindString(&pCSVConfig->eaColumns[i]->ppchFixes, buffer) == -1)
					eaPush(&pCSVConfig->eaColumns[i]->ppchFixes, StructAllocString(buffer));
			}

			pCSVConfig->eaColumns[i]->bTokens = true;
		}
		else if (!stricmp(pCSVConfig->eaColumns[i]->pchTitle, "Short Description"))
		{
			StructFreeStringSafe(&pCSVConfig->eaColumns[i]->pchTitle);
			pCSVConfig->eaColumns[i]->pchTitle = StructAllocString("description");
		}
		//Else it's not supported for this export
		else
		{
			StructDestroy(parse_CSVColumn, eaRemove(&pCSVConfig->eaColumns, i));
		}
	}

	//Add the dont associate column
	{
		CSVColumn *pDontAssocCol = StructCreate(parse_CSVColumn);
		pDontAssocCol->eType = kCSVColumn_StaticText;
		pDontAssocCol->pchTitle = StructAllocString("dontAssociate");
		pDontAssocCol->pchObjPath = StructAllocString("1");
		eaPush(&pCSVConfig->eaColumns, pDontAssocCol);
	}

	//Add the internal name column
	{
		CSVColumn *pInternalNameCol = StructCreate(parse_CSVColumn);
		pInternalNameCol->eType = kCSVColumn_StaticText;
		pInternalNameCol->pchTitle = StructAllocString("internal");
		pInternalNameCol->pchObjPath = StructAllocString(GetProductName());
		eaPush(&pCSVConfig->eaColumns, pInternalNameCol);
	}

	pCSVConfig->bMicrotransactionExport = true;
}

//Export the import file for the account server
AUTO_COMMAND ACMD_NAME("mte_ExportImport");
void mte_ExportImport()
{
	static CSVConfigWindow *pConfigWindow = NULL;

	if(pConfigWindow == NULL)
	{
		pConfigWindow = (CSVConfigWindow*)malloc(sizeof(CSVConfigWindow));
		pConfigWindow->pWindow = ui_WindowCreate("CSV Export Config", 150, 200,450,300);
		pConfigWindow->eDefaultExportColumns = kColumns_All;
		pConfigWindow->eDefaultExportType = kCSVExport_Group;
		pConfigWindow->pchBaseFilename = StructAllocString("MicroTransactions");
		initCSVConfigWindow(pConfigWindow);
	}

	//Populate the datas
	setupCSVConfigWindow(pConfigWindow, mteWindow, mte_ExportImportOkayClicked);

	ui_WindowSetModal(pConfigWindow->pWindow, true);
	ui_WindowShow(pConfigWindow->pWindow);
}

static void mte_initCallbacks(MEWindow *pWindow, METable *pTable)
{
	// General Window callbacks
	MEWindowSetCreateCallback(pWindow, mte_windowCreateCallback);

	// General table callbacks
	METableSetValidateCallback(pTable, mte_validateCallback, pTable);
	METableSetCreateCallback(pTable, mte_tableCreateCallback);
	METableSetPostOpenCallback(pTable, mte_postOpenCallback);
	METableSetPreSaveCallback(pTable, mte_preSaveCallback);

	//Setup column callbacks
	METableSetColumnChangeCallback(pTable, "AS Prod. Category", mte_ASNRegenCallback, NULL);
	METableSetColumnChangeCallback(pTable, "AS Prod. Suffix", mte_ASNRegenCallback, NULL);
	METableSetColumnChangeCallback(pTable, "AS Generate Reclaim Product", mte_ASNRegenCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Promo Product", mte_ASNRegenCallback, NULL);
	METableSetColumnChangeCallback(pTable, "Once per Account", mte_ASNRegenCallback, NULL);
	METableSetSubColumnChangeCallback(pTable, mteSubgroupPartId, "Type", mte_typeChangeCallback, NULL);

	// We need this registered here instead of by METable because the dictionary will 
	// only allow each callback function to be registered once and there may be multiple
	// METable instances.  Our local callback just passes through to the METable.
	resDictRegisterEventCallback(g_hMicroTransDefDict, mte_dictChangeCallback, pTable);
	resDictRegisterEventCallback(gMessageDict, mte_messageDictChangeCallback, pTable);
}

static void mte_initColumns(METable *pTable)
{
	METableAddSimpleColumn(pTable, "Name", "name", 150, NULL, kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "Name", ME_STATE_NOT_PARENTABLE);

	// Lock in name column
	METableSetNumLockedColumns(pTable, 2);

	METableAddScopeColumn(pTable,	"Scope",				"Scope",			160,	MTE_GROUP_MAIN,	kMEFieldType_TextEntry);
	METableAddFileNameColumn(pTable,"File Name",			"file",				210,	MTE_GROUP_MAIN,	NULL, MICROTRANSACTIONS_BASE_DIR, MICROTRANSACTIONS_BASE_DIR, ".microtrans", UIBrowseNewOrExisting);
	METableAddDictColumn(pTable,	"Required Purchase",	"RequiredPurchase", 140,	MTE_GROUP_MAIN,	kMEFieldType_ValidatedTextEntry, "MicroTransactionDef", parse_MicroTransactionDef, "Name");

	METableAddSimpleColumn(pTable,	"Account Server Name",	"ProductName",		140,	MTE_GROUP_MAIN,	kMEFieldType_TextEntry);
	METableSetColumnState(pTable, "Account Server Name", ME_STATE_NOT_EDITABLE);

	METableAddEnumColumn(pTable,	"AS Prod. Category",	"Category",			120,	MTE_GROUP_MAIN, kMEFieldType_Combo, ProductCategoryEnum);
	METableAddSimpleColumn(pTable,	"AS Prod. Suffix",		"ProductIdentifier",140,	MTE_GROUP_MAIN,	kMEFieldType_TextEntry);
	METableAddSimpleColumn(pTable,	"AS Generate Reclaim Product",	"GenerateReclaimProduct",	100,	MTE_GROUP_MAIN, kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,	"AS Prod. Buy Flag",	"BuyProduct",		100,	MTE_GROUP_MAIN,	kMEFieldType_BooleanCombo);
	METableSetColumnState(pTable, "AS Prod. Buy Flag", ME_STATE_NOT_EDITABLE);
	METableAddSimpleColumn(pTable,	"Promo Product",		"PromoProduct",		100,	MTE_GROUP_MAIN,	kMEFieldType_BooleanCombo);

	METableAddDictColumn(pTable,	"Categories",			"Categories",		175,	MTE_GROUP_MAIN,	kMEFieldType_ValidatedTextEntry, "MicroTransactionCategory", parse_MicroTransactionCategory, "Name");
	METableAddEnumColumn(pTable,	"Shards",				"Shards",			175,	MTE_GROUP_MAIN, kMEFieldType_FlagCombo, MicroTrans_ShardCategoryEnum);
	METableAddSimpleColumn(pTable,	"Price",				"uiPrice",			50,		MTE_GROUP_MAIN,	kMEFieldType_TextEntry);

	METableAddSimpleColumn(pTable,	"Display Name",			".displayNameMesg.EditorCopy",		160, MTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,	"Short Description",	".descriptionShortMesg.EditorCopy",	160, MTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,	"Long Description",		".descriptionLongMesg.EditorCopy",	160, MTE_GROUP_MAIN, kMEFieldType_Message);
	METableAddSimpleColumn(pTable,	"Small Icon",			"SmallIcon",		180,	MTE_GROUP_MAIN,	kMEFieldType_Texture);
	METableAddSimpleColumn(pTable,	"Large Icon 1",			"LargeIcon",		180,	MTE_GROUP_MAIN,	kMEFieldType_Texture);
	METableAddSimpleColumn(pTable,	"Large Icon 2",			"LargeIconSecond",	180,	MTE_GROUP_MAIN,	kMEFieldType_Texture);
	METableAddSimpleColumn(pTable,	"Large Icon 3",			"LargeIconThird",	180,	MTE_GROUP_MAIN,	kMEFieldType_Texture);
	METableAddSimpleColumn(pTable,	"Once per Account",		"OnePerAccount",	100,	MTE_GROUP_MAIN,	kMEFieldType_BooleanCombo);
	METableAddSimpleColumn(pTable,	"Once per Character",	"OnePerCharacter",	100,	MTE_GROUP_MAIN,	kMEFieldType_BooleanCombo);
	METableAddExprColumn(pTable,	"Can Buy",				"ExprCanBuyBlock",  180,	MTE_GROUP_MAIN, microtrans_GetBuyContext(false));
	METableAddSimpleColumn(pTable,	"Deprecated",			"Deprecated",		100,	MTE_GROUP_MAIN,	kMEFieldType_BooleanCombo);
	//TODO(BH): An Earray of icons?  METableAddSimpleColumn(pTable, "Detail Icons", "DetailIcons", 180, MTE_GROUP_MAIN, kMEFieldType_TextEntry);
}

static void mte_initPartColumns(METable *pTable)
{
	int id;
	// ---- Level Gating ----
	mteSubgroupPartId = id = METableCreateSubTable(pTable, "Part", "Parts", parse_MicroTransactionPart, NULL, NULL, NULL, mte_createMicroTransactionPart);
	
	
	METableAddEnumSubColumn(pTable, id,   "Type",	"PartType",    150, MTE_SUBGROUP_PART, kMEFieldType_Combo, MicroPartTypeEnum);
	METableAddEnumSubColumn(pTable, id,   "Special Type",	"SpecialPartType",    150, MTE_SUBGROUP_PART, kMEFieldType_Combo, SpecialPartTypeEnum);

	// Lock in label column
	METableSetNumLockedSubColumns(pTable, id, 2);

	METableAddSimpleSubColumn(pTable, id, "Count", "Count", 80, MTE_SUBGROUP_PART, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Optional Icon", "Icon",180, MTE_SUBGROUP_PART, kMEFieldType_Texture);

	//Add the dictionary references
	METableAddGlobalDictSubColumn(pTable, id, "Item", "ItemDef", 160, MTE_SUBGROUP_PART_ITEM, kMEFieldType_ValidatedTextEntry, "ItemDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Reward Table", "RewardTable", 160, MTE_SUBGROUP_PART_REWARD, kMEFieldType_ValidatedTextEntry, "RewardTable", "resourceName");
	METableAddSimpleSubColumn(pTable, id, "Add to Best Bag", "AddToBestBag",100, MTE_SUBGROUP_PART_ITEM, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Allow Overflow Bag", "AllowOverflowBag",100, MTE_SUBGROUP_PART_ITEM, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Ignore Usage Restrictions", "IgnoreUsageRestrictions",100, MTE_SUBGROUP_PART_ITEM, kMEFieldType_BooleanCombo);
	METableAddGlobalDictSubColumn(pTable, id, "Costume", "Costume", 160, MTE_SUBGROUP_PART_COSTUME, kMEFieldType_ValidatedTextEntry, "PlayerCostume", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Power", "PowerDef", 160, MTE_SUBGROUP_PART_POWER, kMEFieldType_ValidatedTextEntry, "PowerDef", "resourceName");
	METableAddGlobalDictSubColumn(pTable, id, "Species", "Species", 160, MTE_SUBGROUP_PART_SPECIES, kMEFieldType_ValidatedTextEntry, "SpeciesDef", "resourceName");
	METableAddSubColumn(pTable, id, "Permission", "Permission", NULL, 160, MTE_SUBGROUP_PART_PERMISSION, kMEFieldType_ValidatedTextEntry, NULL, NULL, NULL, NULL, NULL, NULL, mte_getPermissions);

	METableAddSimpleSubColumn(pTable, id, "Attribute Key", ".Attrib.Attribute", 150, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_TextEntry);
	METableAddEnumSubColumn(pTable, id,   "Change Type",	".Attrib.Type",    100, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_Combo, AVChangeTypeEnum);
	METableAddSimpleSubColumn(pTable, id, "Integer Value", ".Attrib.Val", 100, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Clamp Values", ".Attrib.ClampValues", 100, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_BooleanCombo);
	METableAddSimpleSubColumn(pTable, id, "Min Value", ".Attrib.MinVal", 100, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "Max Value", ".Attrib.MaxVal", 100, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_TextEntry);
	METableAddSimpleSubColumn(pTable, id, "String Value", ".Attrib.StringVal", 100, MTE_SUBGROUP_PART_ATTRIB, kMEFieldType_TextEntry);
}

static void mte_init(MultiEditEMDoc *pEditorDoc)
{
	if (!mteWindow) {
		// Create the editor window
		mteWindow = MEWindowCreate("Micro Transactions Editor", "MicroTransaction", "MicroTransactions", SEARCH_TYPE_MICROTRANSACTION, g_hMicroTransDefDict, parse_MicroTransactionDef, "name", "file", "scope", pEditorDoc);

		emMenuItemCreate(mteWindow->pEditorDoc->emDoc.editor, "mte_ExportImport", "Create Import File", NULL, NULL, "mte_ExportImport");
		emMenuRegister(mteWindow->pEditorDoc->emDoc.editor, emMenuCreate(mteWindow->pEditorDoc->emDoc.editor, "Tools", "mte_ExportImport", NULL));

		// Add micro transaction columns
		mte_initColumns(mteWindow->pTable);

		mte_initPartColumns(mteWindow->pTable);
		METableFinishColumns(mteWindow->pTable);

		MEWindowInitTableMenus(mteWindow);

		//Get the required resources
		resRequestAllResourcesInDictionary("PowerDef");
		resRequestAllResourcesInDictionary("ItemDef");
		resRequestAllResourcesInDictionary("PlayerCostume");
		resRequestAllResourcesInDictionary("SpeciesDef");

		// Set edit mode
		resSetDictionaryEditMode(g_hRewardTableDict, true);

		// Set the callbacks
		mte_initCallbacks(mteWindow, mteWindow->pTable);
	}

	// Show the window
	ui_WindowPresent(mteWindow->pUIWindow);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------

MEWindow *MicroTransEditor_init(MultiEditEMDoc *pEditorDoc) 
{
	mte_init(pEditorDoc);

	return mteWindow;
}

void MicroTransEditor_createMT(char *pcName)
{
	// Create a new object since it is not in the dictionary
	// Add the object as a new object with no old
	void *pObject = mte_createObject(mteWindow->pTable, NULL, pcName, false);
	METableAddRowByObject(mteWindow->pTable, pObject, 1, 1);
}

#endif

