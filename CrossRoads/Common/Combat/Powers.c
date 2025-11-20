/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Powers.h"

#include "aiStructCommon.h"
#include "BlockEarray.h"
#include "dynFxInfo.h"
#include "entCritter.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "estring.h"
#include "Expression.h"
#include "ExpressionPrivate.h" // So we can walk through generated Expressions
#include "fileutil.h"
#include "foldercache.h"
#include "GameAccountDataCommon.h"
#include "MemoryPool.h"
#include "objPath.h"
#include "Player.h"
#include "qsortG.h"
#include "rand.h"
#include "RegionRules.h"
#include "ResourceManager.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TextParserInheritance.h"
#include "tokenstore.h"
#include "utilitiesLib.h"
#include "CostumeCommonEntity.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"

#include "AutoGen/AttribMod_h_ast.h"
#include "AttribModFragility.h"
#include "Character.h"
#include "CharacterAttribute.h"
#include "CharacterAttribs.h"
#include "AutoGen/CharacterAttribs_h_ast.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "CombatEval.h"
#include "CombatSensitivity.h"
#include "ComboTracker.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowersAutoDesc.h"
#include "PowersAutoDesc_h_ast.h"
#include "PowerModes.h"
#include "PowerVars.h"
#include "PowerTree.h"
#include "PowerHelpers.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/PowerAnimFX_h_ast.h"
#include "AutoGen/PowersEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
	#include "EntityMovementDefault.h"
#endif


#if GAMESERVER
	#include "aiLib.h"
	#include "gslSendToClient.h"
	#include "LoggedTransactions.h"
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
	#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#elif GAMECLIENT
	#include "UIGen.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ParseTable parse_Character[];
#define TYPE_parse_Character Character
extern ParseTable parse_PowerActivation[];
#define TYPE_parse_PowerActivation PowerActivation
extern StaticDefineInt TargetTypeEnum[];

extern AttribType *attrib_Unroll(AttribType attrib);

DictionaryHandle g_hPowerTargetDict;
DictionaryHandle g_hPowerDefDict;
DictionaryHandle g_hPowerEmitDict;

bool g_bPowersDebug = 0;
bool g_bPowersErrors = 0;
bool g_bPowersSelectDebug = 0;
int g_bNewAttributeSystem = 0;

// Global list of names of Powers that are disabled
const char **g_ppchPowersDisabled = NULL;

PowerCategories g_PowerCategories = {0};
PowerConfig gPowerConfig = {0};

PEPowerDefGroup **s_stGroups;

PEPowerDefGroup s_PEPowerDefTopGroup;

// Static variables to support power tags and such
static PowerTagNames s_PowerTagNames;
static PowerAITagNames s_PowerAITagNames;
DefineContext *s_pDefinePowerCategories = NULL;
DefineContext *s_pDefinePowerTags = NULL;
DefineContext *g_pDefinePowerAITags = NULL;

// Static "Forever" expression for matching
static Expression *s_pExprForever = NULL;

// Power Purposes for UI categorization 
DefineContext *g_pPowerPurposes = NULL;
S32 g_iNumOfPurposes = 0;

// ModStackGroup DefineContext for data-defined ModStackGroups
DefineContext *g_pModStackGroups = NULL;

#define POWERS_BASE_DIR "defs/powers"
#define POWERS_EXTENSION "powers"



// Special set of CombatTrackerFlags we want to show in the editor for the 
StaticDefineInt CombatTrackerFlagEditorEnum[] =
{
	DEFINE_INT
	{ "ReactiveDodge", kCombatTrackerFlag_ReactiveDodge},
	{ "ReactiveBlock", kCombatTrackerFlag_ReactiveBlock},
	{ "ShowPowerName", kCombatTrackerFlag_ShowPowerDisplayName},
	DEFINE_END
};


// StaticDefineInts into which all the power tags and such are dynamically loaded
StaticDefineInt PowerCategoriesEnum[] =
{
	DEFINE_INT
	DEFINE_EMBEDDYNAMIC_INT(s_pDefinePowerCategories)
	DEFINE_END
};

StaticDefineInt PowerTagsEnum[] =
{
	DEFINE_INT

	DEFINE_EMBEDDYNAMIC_INT(s_pDefinePowerTags)

	DEFINE_END
};

// Parse table for Expressions, with specific fields exposed (copied from real parse table)
// If you touch, fix InitPowerExpr()
ParseTable parse_PowerDef_ForExpr[] =
{
	{ "PowerDef_ForExpr", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(PowerDef), 0, NULL, 0 },
	{ "{",					TOK_START, 0 },
	{ "Name",				TOK_STRUCTPARAM | TOK_KEY | TOK_STRING(PowerDef, pchName, 0), NULL },
	{ "Tags",				TOK_NO_TEXT_SAVE | TOK_EMBEDDEDSTRUCT(PowerDef, tags, parse_PowerTagsStruct)},
	{ "MaxTargetsHit",		TOK_AUTOINT(PowerDef, iMaxTargetsHit, 0), NULL },
	{ "Range",				TOK_F32(PowerDef, fRange, 0), NULL },
	{ "TimeCharge",			TOK_F32(PowerDef, fTimeCharge, 0), NULL },
	{ "TimeMaintain",		TOK_F32(PowerDef, fTimeMaintain, 0), NULL },
	{ "TotalCastPeriod",	TOK_F32(PowerDef, fTotalCastPeriod, 0), NULL },
	{ "TimeActivate",		TOK_F32(PowerDef, fTimeActivate, 0), NULL },
	{ "TimeActivatePeriod",	TOK_F32(PowerDef, fTimeActivatePeriod, 0), NULL },
	{ "TimeRecharge",		TOK_F32(PowerDef, fTimeRecharge, 0), NULL },
	{ "TimeOverride",		TOK_F32(PowerDef, fTimeOverride, 0), NULL },
	{ "TimeAllowQueue",		TOK_F32(PowerDef, fTimeAllowQueue, 0), NULL },
	{ "ChargeRefillInterval",TOK_F32(PowerDef, fChargeRefillInterval, 0), NULL },
	{ "AIMinRange",			TOK_AUTOINT(PowerDef, iAIMinRange, 0), NULL },
	{ "AIMaxRange",			TOK_AUTOINT(PowerDef, iAIMaxRange, 0), NULL },
	{ "RechargeDisabled",	TOK_BIT, 0, 8, NULL},
	{ "}",					TOK_END, 0 },
	{ "", 0, 0 }
};

void FindAutoStructBitField(char *pStruct, int iAllocedSizeInWords, ParseTable *pTPIColumn);
static void PowerDefDeriveAndSetCritAttribs(PowerDef *pdef, S32 bWarnings);

AUTO_RUN;
void InitPowerExpr(void)
{
	ParserSetTableInfo(parse_PowerDef_ForExpr, sizeof(PowerDef), "PowerDef_ForExpr", NULL, __FILE__, false, true);
	{
		int iSizeInWords = (sizeof(PowerDef) + 7) / 4;
		PowerDef *pTemp = alloca(iSizeInWords * 4);
		memset(pTemp, 0, iSizeInWords * 4);
		pTemp->bRechargeDisabled = ~0;
		FindAutoStructBitField((char*)pTemp, iSizeInWords, &parse_PowerDef_ForExpr[17]);
		pTemp->bRechargeDisabled = 0;
	}
}


AUTO_COMMAND ACMD_NAME("Powers.NewAttributeSystem") ACMD_SERVERONLY ACMD_HIDE;
void PowersNewAttributeSystem(int bOn)
{
	if(g_bNewAttributeSystem == !bOn)
	{
		EntityIterator *piter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE);
		Entity *pent;
		while(pent = EntityIteratorGetNext(piter))
		{
			if(pent->pChar)
			{
				if(bOn)
				{
					TEST_character_CreateAttributeArray(pent->pChar);
				}
				else
				{
					TEST_character_DestroyAttributeArray(pent->pChar);
				}
			}
		}

		g_bNewAttributeSystem = !!bOn;
	}
}



// MemoryPool initialization

MP_DEFINE(PowerRef);
MP_DEFINE(CooldownTimer);

AUTO_RUN;
void InitPowersMemPools(void)
{
	MP_CREATE(PowerRef,10);
#ifdef GAMESERVER
	MP_CREATE(CooldownTimer, 100);
#else
	MP_CREATE(CooldownTimer,10);
#endif
}


// PowerRefs

// Creates and returns a PowerRef for the Power
PowerRef *power_CreateRef(Power *ppow)
{
	PowerRef *ppowref = NULL;
	ppowref = MP_ALLOC(PowerRef);
	powerref_Set(ppowref,ppow);
	return ppowref;
}

// Frees the PowerRef
void powerref_Destroy(PowerRef *ppowref)
{
	if(ppowref)
	{
		// Any special cleanup needs to be included in poweract_Destroy()
		REMOVE_HANDLE(ppowref->hdef);
		MP_FREE(PowerRef,ppowref);
	}
}

// Frees the PowerTracker and sets its pointer to NULL
void powerref_DestroySafe(PowerRef **pppowref)
{
	if(*pppowref)
	{
		powerref_Destroy(*pppowref);
		*pppowref = NULL;
	}
}

// Sets the PowerRef to refer to the Power
void powerref_Set(PowerRef *ppowref, Power *ppow)
{
	power_GetIDAndSubIdx(ppow, &ppowref->uiID, &ppowref->iIdxSub, &ppowref->iLinkedSub);
	REMOVE_HANDLE(ppowref->hdef);
	if(ppow)
	{
		ppowref->uiSrcEquipSlot = ppow->uiSrcEquipSlot;
		COPY_HANDLE(ppowref->hdef,ppow->hDef);
	}
}


// Searches an earray of PowerRefs for the Power, returns the index or -1
int power_FindInRefs(Power *ppow, PowerRef ***pppPowerRefs)
{
	int i = eaSize(pppPowerRefs)-1;
	if(i>=0)
	{
		U32 uiID = 0;
		S32 iIdxSub = -1;
		S16 iLinkedSub = -1;
		PowerDef *pdef = GET_REF(ppow->hDef);

		power_GetIDAndSubIdx(ppow, &uiID, &iIdxSub, &iLinkedSub);
		for(; i>=0; i--)
		{
			PowerRef *ppowref = (*pppPowerRefs)[i];
			if(ppowref->uiID==uiID
				&& ppowref->iIdxSub==iIdxSub
				&& ppowref->iLinkedSub==iLinkedSub
				&& GET_REF(ppowref->hdef)==pdef)
			{
				break;
			}
		}
	}
	return i;
}


// Returns the translated display name of the PowerDef, if it exists, otherwise it returns the internal name
const char *powerdef_GetLocalName(PowerDef *pdef)
{
	Message *pMsg = GET_REF(pdef->msgDisplayName.hMessage);
	const char *pchReturn = TranslateMessagePtrSafe(pMsg,pdef->pchName);
	// This case can't actually happen, but I have to include it because analysis isn't being used in the
	//  message system.
	return pchReturn ? pchReturn : "";
}

// Returns the translated display name of the Power's PowerDef, if it exists, otherwise it returns the internal name
const char *power_GetLocalName(Power *ppow)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef)
	{
		return powerdef_GetLocalName(pdef);
	}
	else
	{
		return NULL;
	}
}

static bool powertarget_CheckOrPairsSameTargetTypes(PowerTarget* pPowerTargetA, PowerTarget* pPowerTargetB)
{
	S32 i, j;
	for (i = eaSize(&pPowerTargetA->ppOrPairs)-1; i >= 0; i--)
	{
		for (j = eaSize(&pPowerTargetB->ppOrPairs)-1; j >= 0; j--)
		{
			if (pPowerTargetA->ppOrPairs[i]->eRequire == pPowerTargetB->ppOrPairs[j]->eRequire && 
				pPowerTargetA->ppOrPairs[i]->eExclude == pPowerTargetB->ppOrPairs[j]->eExclude)
			{
				break;
			}
		}
		if (j < 0)
		{
			return false;
		}
	}
	return true;
}

static bool powertarget_CheckSameTargetTypes(PowerTarget* pPowerTargetA, PowerTarget* pPowerTargetB)
{
	if ((!pPowerTargetA && pPowerTargetB) || (pPowerTargetA && !pPowerTargetB))
		return false;

	if (pPowerTargetA && pPowerTargetB)
	{
		if (pPowerTargetA->eRequire != pPowerTargetB->eRequire)
		{
			return false;
		}
		else if (pPowerTargetA->eExclude != pPowerTargetB->eExclude)
		{
			return false;
		}
		else // Not checking for matching OrPair array size on purpose just in case there are duplicates
		{
			if (!powertarget_CheckOrPairsSameTargetTypes(pPowerTargetA, pPowerTargetB))
				return false;
			if (!powertarget_CheckOrPairsSameTargetTypes(pPowerTargetB, pPowerTargetA))
				return false;			
		}
	}
	return true;
}


/***** START Power Grouping START *****/


static int PESortPowerGroup(const void *a, const void *b)
{
	return stricmp((*(PEPowerDefGroup**)a)->pchName,(*(PEPowerDefGroup**)b)->pchName);
}

static int PESortPower(const void *a, const void *b)
{
	return stricmp((*(PowerDef**)a)->pchName,(*(PowerDef**)b)->pchName);
}
static void PEClearGroup(PEPowerDefGroup *pgroup)
{
	int i;
	eaDestroy(&pgroup->ppPowers);
	for(i=eaSize(&pgroup->ppGroups)-1; i>=0; i--)
	{
		PEClearGroup(pgroup->ppGroups[i]);
	}
}
static void PESortGroup(PEPowerDefGroup *pgroup)
{
	int i;

	// Process subgroups
	for(i=0; i<eaSize(&pgroup->ppGroups); i++)
	{
		PESortGroup(pgroup->ppGroups[i]);
	}

	// Sort sub groups if we've got any
	if (eaSize(&pgroup->ppGroups))
	{
		qsort(pgroup->ppGroups,eaSize(&pgroup->ppGroups),sizeof(PEPowerDefGroup*),PESortPowerGroup);
	}

	// Sort powers
	if(eaSize(&pgroup->ppPowers)>0)
	{
		qsort(pgroup->ppPowers,eaSize(&pgroup->ppPowers),sizeof(PowerDef*),PESortPower);
	}
}
static PEPowerDefGroup* PEGetGroup(SA_PARAM_NN_STR char *pchGroup)
{
	PEPowerDefGroup *pGroup;
	char *pchTerm;
	int i;

	// Look for group
	for(i=eaSize(&s_stGroups)-1; i>=0; --i) {
		if (stricmp(s_stGroups[i]->pchName, pchGroup) == 0) {
			return s_stGroups[i];
		}
	}

	// Make this group and add it to the list
	pGroup = StructCreate(parse_PEPowerDefGroup);
	pGroup->pchName = StructAllocString(pchGroup);
	eaPush(&s_stGroups, pGroup);

	pchTerm = strrchr(pchGroup,'/');
	if(pchTerm)
	{
		// Find the parent group
		PEPowerDefGroup *pParent;
		char achParent[CRYPTIC_MAX_PATH];
		strncpy(achParent,pchGroup,pchTerm-pchGroup);
		pParent = PEGetGroup(achParent);
		eaPush(&pParent->ppGroups,pGroup);
	}
	else
	{
		eaPush(&s_PEPowerDefTopGroup.ppGroups,pGroup);
	}

	return pGroup;
}
void PEBuildGroupedPowerDefs(void)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_hPowerDefDict);
	PowerDef **pPowerDefs = (PowerDef**)pStruct->ppReferents;
	int i;

	if(eaSize(&s_stGroups))
	{
		for(i=eaSize(&s_stGroups)-1; i>=0; --i) {
			eaClear(&s_stGroups[i]->ppGroups);
			eaClear(&s_stGroups[i]->ppPowers);
			StructDestroy(parse_PEPowerDefGroup, s_stGroups[i]);
		}
		eaClear(&s_stGroups);
	}

	// Clear the arrays of any existing groups
	eaClear(&s_PEPowerDefTopGroup.ppGroups);

	// Group up all powers
	for(i=eaSize(&pPowerDefs)-1; i>=0; --i) {
		PowerDef *pdef = pPowerDefs[i];
		PEPowerDefGroup *pdefgroup = NULL;
		pdefgroup = PEGetGroup(pdef->pchGroup?pdef->pchGroup:"Ungrouped");
		eaPush(&pdefgroup->ppPowers,pdef);
	}

	// Remove empty groups, sort the non-empty
	PESortGroup(&s_PEPowerDefTopGroup);
}


/***** END Power Grouping END *****/

// Returns true if the PowerDef ignores the given attrib
S32 powerdef_IgnoresAttrib(PowerDef *pdef, AttribType eType)
{
	return (-1!=eaiFind(&pdef->piAttribIgnore,eType));
}

// Returns the lowest preferred tray for the PowerDef, which is based on its categories.  If there
//  is no preference, returns 0.
S32 powerdef_GetPreferredTray(PowerDef *pdef)
{
	S32 i,iTray = 0;
	for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
	{
		if(g_PowerCategories.ppCategories[pdef->piCategories[i]]->iPreferredTray)
		{
			if(!iTray || g_PowerCategories.ppCategories[pdef->piCategories[i]]->iPreferredTray < iTray)
			{
				iTray = g_PowerCategories.ppCategories[pdef->piCategories[i]]->iPreferredTray;
			}
		}
	}
	return iTray;
}

S32 powerdef_GetCategorySortID(SA_PARAM_NN_VALID PowerDef *pdef)
{
	S32 i,iMaxSort = 0;
	for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
	{
		if(g_PowerCategories.ppCategories[pdef->piCategories[i]]->iSortGroup > iMaxSort)
		{
			iMaxSort = g_PowerCategories.ppCategories[pdef->piCategories[i]]->iSortGroup;
		}
	}
	return iMaxSort;
}

// Figures out which WorldRegionType the PowerDef will likely activate in
WorldRegionType powerdef_GetBestRegionType(PowerDef *pDef)
{
	int i;
	for(i=0;i<WRT_COUNT;i++)
	{
		if(!getRegionRulesFromRegionType(i))
			continue;

		if(powerdef_RegionAllowsActivate(pDef,i))
		{
			return i;
		}
	}

	return WRT_None;
}




/***** BEGIN Key Management BEGIN *****/

// Checks to see if this power currently has a valid attrib key block of its own
static bool PowerHasValidAttribKeyBlock(PowerDef *pdef)
{
	if(pdef->iAttribKeyBlock)
	{
		if(pdef->pInheritance)
		{
			PowerDef *pdefParent = powerdef_Find(pdef->pInheritance->pParentName);
			if(pdefParent && pdef->iAttribKeyBlock!=pdefParent->iAttribKeyBlock)
				return true;
		}
		else
		{
			return true;
		}
	}

	return false;
}

// Randomly generates key blocks until it finds one that is unused.  May need to be tweaked
//  to be smarter if it ends up slow.
static void GenerateNewAttribKeyBlock(PowerDef *pdef)
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(g_hPowerDefDict);
	PowerDef **ppDefs = (PowerDef**)pArray->ppReferents;
	int iKey = 0;
	bool bValid = false;

	while(!bValid)
	{
		int i;
		iKey = (randomInt() >> ATTRIB_KEY_BLOCK_BITS) << ATTRIB_KEY_BLOCK_BITS;
		if(iKey)
		{
			for(i=eaSize(&ppDefs)-1; i>=0; i--)
			{
				if(ppDefs[i]->iAttribKeyBlock==iKey)
					break;
			}
			bValid = (i < 0);
		}
	}

	// Set the key block
	pdef->iAttribKeyBlock = iKey;
	
	// Add it to the inheritance structure if this is a child of another power
	if(pdef->pInheritance)
	{
		StructInherit_CreateFieldOverride(parse_PowerDef, pdef, ".AttribKeyBlock");
	}
}

// Returns the next valid key for an attrib mod of the power def.  Optionally pass in a
//  key this function just returned to speed up the search process.
int powerdef_GetNextAttribKey(PowerDef *pdef, int iPreviousKey)
{
	int iOffset = 0;
	if(!PowerHasValidAttribKeyBlock(pdef))
	{
		GenerateNewAttribKeyBlock(pdef);
	}
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pdef->iAttribKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pdef->iAttribKeyBlock + iOffset;
			for(j=eaSize(&pdef->ppMods)-1; j>=0; j--)
			{
				if(pdef->ppMods[j]->iKey==iKey)	break; 
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pdef->iAttribKeyBlock + iOffset;
}

int powerdef_GetNextComboKey(PowerDef *pdef, int iPreviousKey)
{
	int iOffset = 0;
	if(!PowerHasValidAttribKeyBlock(pdef))
		GenerateNewAttribKeyBlock(pdef);
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pdef->iAttribKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pdef->iAttribKeyBlock + iOffset;
			for(j=eaSize(&pdef->ppCombos)-1; j>=0; j--)
			{
				if(pdef->ppCombos[j]->iKey==iKey)	break;
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pdef->iAttribKeyBlock + iOffset;
}

void powerdef_FillMissingComboKeys(PowerDef *pdef)
{
	int i;
	int iLastKey = 0;
	for(i=0; i<eaSize(&pdef->ppCombos); i++)
	{
		if(pdef->ppCombos[i]->iKey==0)
		{
			pdef->ppCombos[i]->iKey = iLastKey = powerdef_GetNextComboKey(pdef,iLastKey);
		}
	}
}

// Goes through all the attrib mod defs of the power and gives ones without a key
//  a valid key
void powerdef_FillMissingAttribKeys(PowerDef *pdef)
{
	int i;
	int iLastKey = 0;
	for(i=0; i<eaSize(&pdef->ppMods); i++)
	{
		if(pdef->ppMods[i]->iKey==0)
		{
			pdef->ppMods[i]->iKey = iLastKey = powerdef_GetNextAttribKey(pdef,iLastKey);
		}
	}
}


/***** END Key Management END *****/

static S32 PowerDefSafeForSelfOnly(SA_PARAM_NN_VALID PowerDef *pdef)
{
	PowerTarget *ptarget = GET_REF(pdef->hTargetMain);
	PowerTarget *ptargetAffected = GET_REF(pdef->hTargetAffected);
	return ptarget && ptarget->bSafeForSelfOnly && ptargetAffected && ptargetAffected->bSafeForSelfOnly;
}

static void PowerExprAttribDependency(Expression* pExpr, S32** ppiAttribs)
{
	static const char * s_pchAttrib = NULL;
	static const char * s_pchAttribBasicNatural = NULL;
	static const char * s_pchSuperStatSum = NULL;
	static const char * s_pchSuperStatSumNatural = NULL;
	static const char * s_pchAttribBasicByTag = NULL;
	static int iAttribStatSet = -1;
	static MultiVal **s_ppStack = NULL;

	int i,s;

	if(!pExpr)
		return;
	
	if(!s_pchAttrib)
	{
		s_pchAttrib = allocAddString("Attrib");
		s_pchAttribBasicNatural = allocAddString("AttribBasicNatural");
		s_pchSuperStatSum = allocAddString("SuperStatSum");
		s_pchSuperStatSumNatural = allocAddString("SuperStatSumNatural");
		s_pchAttribBasicByTag = allocAddString("AttribBasicByTag");
		iAttribStatSet = StaticDefineIntGetInt(AttribTypeEnum,"StatSet");
	}
	
	eaClearFast(&s_ppStack);

	s=beaSize(&pExpr->postfixEArray);
	for(i=0; i<s; i++)
	{
		MultiVal *pVal = &pExpr->postfixEArray[i];
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue; // No idea why these are in the postfix stack, but they are
		if(pVal->type==MULTIOP_FUNCTIONCALL)
		{
			const char *pchFunction = pVal->str;
			if(pchFunction==s_pchAttrib || pchFunction==s_pchAttribBasicNatural || pchFunction==s_pchAttribBasicByTag)
			{
				MultiVal* pAttribVal; // Name of Attribute
				MultiVal* pCharVal; // Character on which to do the lookup

				// these functions take an extra param
				if (pchFunction==s_pchAttribBasicNatural || pchFunction==s_pchAttribBasicByTag) 
				{
					eaPop(&s_ppStack);
				}

				// This assert doesn't do what I wanted.  Don't know how to do what I wanted.
				//devassert(eaSize(&s_ppStack) == 2); // If you change the number of params one of these functions takes, you gotta fix this function

				pAttribVal = eaPop(&s_ppStack);
				pCharVal = eaPop(&s_ppStack);

				if(pAttribVal)
				{
					const char *pchAttrib = MultiValGetString(pAttribVal,NULL);
					AttribType eAttrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttrib);

					if (eAttrib >= 0)
						eaiPushUnique(ppiAttribs, eAttrib);
				}
			}
			else if(iAttribStatSet>=0 && (pchFunction==s_pchSuperStatSum || pchFunction==s_pchSuperStatSumNatural))
			{
				int j;
				MultiVal* pCharVal = eaPop(&s_ppStack); // Character on which to do the lookup
				AttribType *piStats = attrib_Unroll(iAttribStatSet);
				for(j=eaiSize(&piStats)-1; j>=0; j--)
					eaiPushUnique(ppiAttribs, piStats[j]);
			}
		}
		eaPush(&s_ppStack,pVal);
	}
}




#define ModErrorf(moddef,fmt,...) { moddef->eError |= kPowerError_Error; ErrorFilenamef(ppowdef->pchFile,fmt,__VA_ARGS__); }
#define ModWarningf(moddef,fmt,...) { moddef->eError |= kPowerError_Warning; if(bWarnings) Alertf(fmt,__VA_ARGS__); }
// If you need to use this macro, you should probably replace all existing instances with ModErrorf
#define ModRetroErrorf(moddef,fmt,...) { moddef->eError |= kPowerError_Error; ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",14,16,4,2011,fmt,__VA_ARGS__); }


static void AttribModDefValidateAppliedPowerAnimFX(PowerDef *ppowdef, AttribModDef *pdef, S32 bWarnings, PowerDef *ppowdefApplied)
{
	PowerAnimFX *pafx = ppowdefApplied ? GET_REF(ppowdefApplied->hFX) : NULL;
//	if(pafx && pafx->bHasSticky)
//		ModErrorf(pdef,"%s: %d: Applied PowerDef %s has PowerArt %s with sticky bits or fx data that will not be played",powerdef_NameFull(ppowdef),pdef->uiDefIdx,ppowdefApplied->pchName,pafx->cpchName);
}

// Does validation at the attrib mod level
static void AttribModDefValidate(PowerDef *ppowdef, AttribModDef *pdef, S32 bWarnings)
{
	S32 i;

	pdef->eError = kPowerError_Valid;

	if(pdef->uiDefIdx>255)
	{
		ModWarningf(pdef,"%s: %d: Abnormally high index",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// offAttrib
	if(ppowdef->eType==kPowerType_Innate)
	{
		if(IS_SPECIAL_ATTRIB(pdef->offAttrib) && pdef->offAttrib != kAttribType_SpeedCooldownCategory && pdef->offAttrib != kAttribType_DynamicAttrib)
		{
			AttribType *piUnroll = attrib_Unroll(pdef->offAttrib);
			if(!piUnroll)
			{
				ModWarningf(pdef,"%s: %d: Innates do not support most special attributes (%s)\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			}
			else
			{
				for(i=eaiSize(&piUnroll)-1; i>=0; i--)
				{
					if(IS_SPECIAL_ATTRIB(piUnroll[i]))
					{
						ModWarningf(pdef,"%s: %d: Innates do not support attribute sets that contain special attributes (%s)\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
					}
				}
			}
		}
	}
	else if(ppowdef->eType==kPowerType_Enhancement)
	{
		if(pdef->offAttrib==kAttribType_IncludeEnhancement)
			ModErrorf(pdef,"%s: %d: Enhancements do not support %s AttribMods",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
	}

	// offAspect
	if(pdef->offAspect==kAttribAspect_BasicAbs)
	{
		if(ATTRIB_SCALAR(pdef->offAttrib))
		{
			ModWarningf(pdef,"%s: %d: %s is a scalar, should probably be BasicFactPos or BasicFactNeg instead of BasicAbs\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
		}
	}

	// bPersonal
	if(pdef->bPersonal)
	{
		if(ppowdef->eType==kPowerType_Innate)
		{
			ModErrorf(pdef,"%s: %d: Personal flag is not supported on Innates\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		
		if((ppowdef->eType==kPowerType_Enhancement && !pdef->bEnhancementExtension)
			|| (ppowdef->eType!=kPowerType_Enhancement && pdef->bEnhancementExtension))
		{
			ModErrorf(pdef,"%s: %d: Personal flag is not supported on Enhancement-style AttribMods\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}

		if(IS_BASIC_ASPECT(pdef->offAspect))
		{
			if(IS_SPECIAL_ATTRIB(pdef->offAttrib) && 
				(pdef->offAttrib != kAttribType_PowerMode && pdef->offAttrib != kAttribType_CombatAdvantage))
			{
				ModErrorf(pdef,"%s: %d: Personal flag is not supported on Basic Special AttribMods\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
			}
		}
	}

	// Validate magnitude
	if(pdef->pExprMagnitude)
	{
		if(exprIsZero(pdef->pExprMagnitude) && pdef->offAttrib != kAttribType_AttribOverride && pdef->offAttrib != kAttribType_AIAggroTotalScale)
		{
			ModWarningf(pdef,"%s: %d: Magnitude expression of 0, should be empty",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		else if(!(pdef->pFragility && pdef->pFragility->bMagnitudeIsHealth))
		{
			if(IS_BASIC_ASPECT(pdef->offAspect) && ATTRIB_IGNORES_MAGNITUDE(pdef->offAttrib))
			{
				ModWarningf(pdef,"%s: %d: Basic %s mods do not currently use magnitude",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			}
		}
		if(!exprCompare(pdef->pExprMagnitude,s_pExprForever))
		{
			ModErrorf(pdef,"%s: %d: Magnitude expression of \"forever\" is not legal, please use a real number",powerdef_NameFull(ppowdef),pdef->uiDefIdx)
		}
	}
	else
	{
		if(!IS_SPECIAL_ATTRIB(pdef->offAttrib))
		{
			ModWarningf(pdef,"%s: %d: No magnitude, %s mods require a magnitude to have any effect",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
		}
		if(!IS_BASIC_ASPECT(pdef->offAspect))
		{
			ModWarningf(pdef,"%s: %d: No magnitude, %s aspect mods require a magnitude to have any effect",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect));
		}
		if(pdef->eType==kModType_Both || pdef->eType==kModType_Magnitude)
		{
			ModWarningf(pdef,"%s: %d: No magnitude, should not be of type magnitude",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
	}

	// Validate duration
	if(pdef->pExprDuration)
	{
		F64 fVal = 1.f;
		if((ppowdef->eType==kPowerType_Enhancement && !pdef->bEnhancementExtension)
			|| (ppowdef->eType!=kPowerType_Enhancement && pdef->bEnhancementExtension))
		{
			ModWarningf(pdef,"%s: %d: AttribMod is for enhancing a Power, it does not use duration",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(ppowdef->eType==kPowerType_Innate)
		{
			ModWarningf(pdef,"%s: %d: AttribMods on Innate Powers do not use duration",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(exprIsZero(pdef->pExprDuration))
		{
			ModWarningf(pdef,"%s: %d: Duration expression of 0, should be empty",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(pdef->bForever && pdef->eType&kModType_Duration)
		{
			ModErrorf(pdef,"%s: %d: Forever duration, should not be of type duration",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(exprIsSimpleNumber(pdef->pExprDuration,&fVal) && fVal >= 3600)
		{
			ModWarningf(pdef,"%s: %d: Duration expression is large, consider using \"forever\" if you want the AttribMod to be permanent",powerdef_NameFull(ppowdef),pdef->uiDefIdx,fVal);
		}
	}
	else if(pdef->eType==kModType_Both || pdef->eType==kModType_Duration)
	{
		ModWarningf(pdef,"%s: %d: No duration, should not be of type duration",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// Validate default table
	if(pdef->pchTableDefault)
	{
		if(!powertable_Find(pdef->pchTableDefault))
		{
			ModErrorf(pdef,"%s: %d: Default table does not exist: %s",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->pchTableDefault);
		}
		if(pdef->eType==kModType_None)
		{
			ModErrorf(pdef,"%s: %d: Mod with no type, default table (%s) has no effect\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->pchTableDefault);
		}
	}

	// Validate sensitivities
	if(!sensitivity_ValidateSet(pdef->piSensitivities,ppowdef->pchFile))
	{
		//No error message, message handled inside Validate function
		pdef->eError&=kPowerError_Error;
	}
	if(IS_BASIC_ASPECT(pdef->offAspect) && 1.f!=moddef_GetSensitivity(pdef,kSensitivityType_AttribCurve))
	{
		ModErrorf(pdef,"%s: %d: AttribCurve sensitivity is not supported for Basic aspects\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// Validate variance
	if(pdef->fVariance<0.0f || pdef->fVariance>1.0f)
	{
		ModErrorf(pdef,"%s: %d: Invalid variance %f (must be [0.0..1.0])\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->fVariance);
	}
	else if(pdef->eType==kModType_None)
	{
		// This is probably not needed
		// ModWarningf(pdef,"%s: %d: Mod with no type, should have no variance",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// pExprRequires
	if(pdef->pExprRequires)
	{
		if(exprIsZero(pdef->pExprRequires))
		{
			ModWarningf(pdef,"%s: %d: Requires expression of 0, should be deleted if possible",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if((bool)pdef->bEnhancementExtension!=(ppowdef->eType==kPowerType_Enhancement))
		{
			// Enhancement mod or inline enhancement mod
			if(!IS_STRENGTH_ASPECT(pdef->offAspect))
			{
				ModErrorf(pdef,"%s: %d: Requires expression is not supported for non-Strength Enhancement mods\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
			}
			else if(g_CombatConfig.bIgnoreStrEnhanceRequiresCheck)
			{
				ModErrorf(pdef,"%s: %d: Requires expressions are currently ignored for Strength Enhancement mods\n",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
			}
		}
	}

	if(pdef->pExprAffects)
	{
		if(exprIsZero(pdef->pExprAffects))
		{
			ModWarningf(pdef,"%s: %d: Affects expression of 0, should be deleted if possible",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(IS_NORMAL_ATTRIB(pdef->offAttrib) && IS_BASIC_ASPECT(pdef->offAspect) && !POWER_AFFECTOR(pdef->offAttrib))
		{
			ModErrorf(pdef,"%s: %d: Basic %s mods do not support an Affects expression",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
		}
		if(ppowdef->eType==kPowerType_Innate)
		{
			ModWarningf(pdef,"%s: %d: AttribMods on Innate Powers do not support an Affects expression",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
	}
	else if(ATTRIB_AFFECTOR(pdef->offAttrib) && IS_BASIC_ASPECT(pdef->offAspect))
	{
		ModWarningf(pdef,"%s: %d: %s mods should have an Affects expression",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
	}

	if(pdef->bAffectsOnlyOnFirstModTick)
	{
		if (!IS_RESIST_OR_IMMUNITY_ASPECT(pdef->offAspect) && pdef->offAttrib!=kAttribType_Shield)
		{
			ModWarningf(pdef,"%s: %d: mod is flagged 'Affects First Application Only' but is not a resist or immune aspect, or Shield Attrib type.",
						powerdef_NameFull(ppowdef), pdef->uiDefIdx);
		}
	}

	if(pdef->pParams)
	{
		if(pdef->pParams->eType!=pdef->offAttrib)
		{
			ModErrorf(pdef,"%s: %d: Parameter type (%s) does not match attribute (%s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->pParams->eType),StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
		}
	} 
	else if (characterattribs_GetSpecialParseTable(pdef->offAttrib))
	{
		ModErrorf(pdef,"%s: %d: Parameter is missing its data structure",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// fDelay
	if(pdef->fDelay<0.0f)
	{
		ModErrorf(pdef,"%s: %d: Negative delay",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}
	else if(pdef->fDelay>300.f)
	{
		ModWarningf(pdef,"%s: %d: Abnormally large delay %f",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->fDelay);
	}

	// fPeriod
	if(pdef->fPeriod<0.0f)
	{
		ModErrorf(pdef,"%s: %d: Negative period",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}
	else if(pdef->fPeriod>10.0f || (pdef->fPeriod>0.0f && pdef->fPeriod<0.125f))
	{
		ModWarningf(pdef,"%s: %d: Abnormally large or small period %f",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->fPeriod);
	}

	if(pdef->pExprDuration)
	{
		if(!IS_BASIC_ASPECT(pdef->offAspect))
		{
			// Over-time strength or resistance
			if(pdef->fPeriod!=0)
			{
				ModErrorf(pdef,"%s: %d: Modifies strength or resistance over time, but has a period",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
			}
		}
		else
		{
			if(IS_POOL_ATTRIBASPECT(pdef->offAttrib,pdef->offAspect))
			{
				// Over-time damage/healing/power mod
				if(pdef->fPeriod==0)
				{
					ModErrorf(pdef,"%s: %d: Modifies a pool over time, but has no period",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
				}
			}
			else if(pdef->offAttrib==kAttribType_KnockBack || pdef->offAttrib==kAttribType_KnockUp || pdef->offAttrib==kAttribType_Repel)
			{
				// Core Knocks/Repels should be instant or periodic
				if(pdef->fPeriod==0)
				{
					ModWarningf(pdef,"%s: %d: %s mods should either be instant (no duration) or periodic (duration and period)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
				}
			}
			else if(!IS_SPECIAL_ATTRIB(pdef->offAttrib))
			{
				// Over-time basic mod that isn't damage/healing/power
				if(pdef->fPeriod!=0)
				{
					ModErrorf(pdef,"%s: %d: Modifies a non-pool over time, but has a period",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
				}
			}
			else if(IS_SPECIAL_ATTRIB(pdef->offAttrib))
			{
				// Over-time basic special mod
				if(pdef->offAttrib==kAttribType_ApplyPower
					|| pdef->offAttrib==kAttribType_AttribModHeal)
				{
					// These types must be periodic if they have a duration
					if(pdef->fPeriod==0)
					{
						ModErrorf(pdef,"%s: %d: %s mods should either be instant (no duration) or periodic (duration and period)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
					}
				}
			}
		}
	}
	else if(pdef->fPeriod != 0)
	{
		// No duration expresion, but has non-zero period
		ModWarningf(pdef,"%s: %d: Period specified on mod with no duration",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// fArcAffects
	if(pdef->fArcAffects<0.f || pdef->fArcAffects>=360.f)
	{
		ModErrorf(pdef,"%s: %d: Invalid ArcAffects %f (must be [0 .. 360))",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->fArcAffects);
	}
	else if(pdef->fArcAffects!=0.f)
	{
		// Check if it's on an AttribMod that can actually use it
		if(!(pdef->offAttrib==kAttribType_Shield || 
			 pdef->offAttrib==kAttribType_DamageTrigger ||
			 pdef->offAttrib==kAttribType_TriggerComplex ||
			 IS_RESIST_ASPECT(pdef->offAspect) ||
			 IS_IMMUNITY_ASPECT(pdef->offAspect)))
		{
			ModErrorf(pdef,"%s: %d: ArcAffects must be 0 on this type of AttribMod",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		else if (pdef->offAttrib==kAttribType_TriggerComplex)
		{
			TriggerComplexParams *pParams = (TriggerComplexParams*)(pdef->pParams);
			FOR_EACH_IN_EARRAY_INT(pParams->piCombatEvents, S32, iEvent)
			{
				switch (iEvent)
				{
					case kCombatEvent_DamageIn:
					case kCombatEvent_DamageOut:
						continue;
					default:
						ModErrorf(pdef,"%s: %d: ArcAffects cannot handle these TriggerComplex event(s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
						break;
				}
			}
			FOR_EACH_END
		}
	}

	// fYaw
	if(pdef->fYaw<=-180.f || pdef->fYaw>180.f)
	{
		ModErrorf(pdef,"%s: %d: Invalid Yaw %f (must be (-180 .. 180])",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->fYaw);
	}
	else if(pdef->fYaw!=0.f && pdef->fArcAffects==0.f)
	{
		ModErrorf(pdef,"%s: %d: Yaw must be 0 if ArcAffects is 0",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// bIgnoreFirstTick
	if(pdef->bIgnoreFirstTick && !pdef->fPeriod)
	{
		ModErrorf(pdef,"%s: %d: IgnoreFirstTick only valid on periodic mods",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// bReplaceKeepsTimer
	if(pdef->bReplaceKeepsTimer && !(pdef->fPeriod && pdef->eStack==kStackType_Replace))
	{
		ModErrorf(pdef,"%s: %d: ReplaceKeepsTimer only valid on periodic Replace mods",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// bChanceNormalized
	if(pdef->bChanceNormalized)
	{
		if(pdef->fPeriod)
		{
			ModErrorf(pdef,"%s: %d: Normalized chance not allowed on periodic mods",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
	}

	// Expiration structure
	if(pdef->pExpiration)
	{
		if(!GET_REF(pdef->pExpiration->hDef))
		{
			ModErrorf(pdef,"%s: %d: Expiration using invalid PowerDef",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(!pdef->pExpiration->eTarget)
		{
			ModErrorf(pdef,"%s: %d: Expiration must specify target",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		if(pdef->pExpiration->bPeriodic && !pdef->fPeriod)
		{
			ModErrorf(pdef,"%s: %d: Expiration can only be Periodic if the AttribMod is periodic",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		AttribModDefValidateAppliedPowerAnimFX(ppowdef, pdef, bWarnings, GET_REF(pdef->pExpiration->hDef));
	}

	// eStack
	if(pdef->eStack==kStackType_Extend)
	{
		if(!pdef->pExprDuration)
		{
			ModErrorf(pdef,"%s: %d: Stack type Extend only valid on AttribMods with a duration",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
	}

	// uiStackLimit
	if(pdef->uiStackLimit)
	{
		if(!(pdef->eStack==kStackType_Discard || pdef->eStack==kStackType_Replace))
		{
			ModErrorf(pdef,"%s: %d: Stack Limit can only be set on Discard and Replace stack type",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
		else if(pdef->uiStackLimit==1)
		{
			ModErrorf(pdef,"%s: %d: Stack Limit must be left as default (zero, which means no stacking) or must be greater than one (which means limited stacking)",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		}
	}

	// bPowerInstanceStacking
	if(pdef->bPowerInstanceStacking && pdef->eStack==kStackType_Stack)
	{
		ModErrorf(pdef,"%s: %d: PowerInstanceStacking is set, but it is not supported with StackType 'Stack'",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// eHitTest
	if(pdef->eHitTest==kModHitTest_Miss || pdef->eHitTest==kModHitTest_HitOrMiss)
	{
		if((bool)pdef->bEnhancementExtension!=(ppowdef->eType==kPowerType_Enhancement))
			ModErrorf(pdef,"%s: %d: Miss and HitOrMiss tests not legal on Enhancement-style mods",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		if(pdef->eTarget==kModTarget_SelfOnce)
			ModErrorf(pdef,"%s: %d: Miss and HitOrMiss tests not legal on SelfOnce mods",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
		if(!g_CombatConfig.pHitChance)
			ModErrorf(pdef,"%s: %d: Miss and HitOrMiss tests not legal unless missing is enabled",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// Validate Fragility structure
	if(!moddef_ValidateFragility(pdef,ppowdef))
	{
		ModErrorf(pdef,"%s: %d: Bad fragility data",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// bAutoDescDisabled and msgAutoDesc
	if(pdef->bAutoDescDisabled && IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage))
	{
		ModErrorf(pdef,"%s: %d: AutoDesc is Disabled but there is an AutoDesc Message",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
	}

	// uiAutoDescKey
	if(pdef->uiAutoDescKey)
	{
		for(i=eaSize(&ppowdef->ppMods)-1; i>=0; i--)
		{
			if(ppowdef->ppMods[i]!=pdef && ppowdef->ppMods[i]->uiAutoDescKey==pdef->uiAutoDescKey)
				ModErrorf(pdef,"%s: %d: AutoDescKey %d duplicated",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->uiAutoDescKey);
		}
	}

	// Validate any custom parameter structures
	if(pdef->pParams)
	{
		if(pdef->offAttrib==kAttribType_ApplyPower)
		{
			ApplyPowerParams *pParams = (ApplyPowerParams*)(pdef->pParams);
			PowerDef *ppowerApplyDef = GET_REF(pParams->hDef);
			if(!ppowerApplyDef)
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}
			else if(ppowdef->bSafeForSelfOnly)
			{
				if(!PowerDefSafeForSelfOnly(ppowerApplyDef))
				{
					ppowdef->bSafeForSelfOnly = false;
				}
				else
				{
					S32 iapplyModIdx;
					for(iapplyModIdx=eaSize(&ppowerApplyDef->ppMods)-1; iapplyModIdx>=0; iapplyModIdx--)
					{
						AttribModDef *pmodApplyDef = ppowerApplyDef->ppMods[iapplyModIdx];
						if(pmodApplyDef && 
							(pmodApplyDef->eType == kAttribType_EntCreate ||
							 pmodApplyDef->eType == kAttribType_EntCreateVanity) )
						{
							ppowdef->bSafeForSelfOnly = false;
						}
					}
				}
			}
			AttribModDefValidateAppliedPowerAnimFX(ppowdef, pdef, bWarnings, GET_REF(pParams->hDef));
		}
		else if(pdef->offAttrib==kAttribType_AttribModFragilityScale)
		{
			AttribModFragilityScaleParams *pParams = (AttribModFragilityScaleParams*)(pdef->pParams);
			if(IS_HANDLE_ACTIVE(pParams->hScaleIn) && !GET_REF(pParams->hScaleIn))
			{
				ModErrorf(pdef,"%s: %d: %s using invalid ScaleIn (%s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hScaleIn));
			}
			if(IS_HANDLE_ACTIVE(pParams->hScaleOut) && !GET_REF(pParams->hScaleOut))
			{
				ModErrorf(pdef,"%s: %d: %s using invalid ScaleOut (%s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hScaleOut));
			}
		}
		else if(pdef->offAttrib==kAttribType_DamageTrigger)
		{
			DamageTriggerParams *pParams = (DamageTriggerParams*)(pdef->pParams);
			if(!GET_REF(pParams->hDef) && pdef->offAspect == kAttribAspect_BasicAbs)
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}

			if(pdef->pExprMagnitude)
			{
				if(!pParams->bMagnitudeIsCharges && !(pdef->pFragility && pdef->pFragility->bMagnitudeIsHealth))
				{
					ModWarningf(pdef,"%s: %d: %s isn't using magnitude for anything",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
				}
			}
			if (pParams->offAttrib == -1)
			{
				ModWarningf(pdef,"%s: %d: must specify an attrib type for DamageTrigger Attrib",powerdef_NameFull(ppowdef),pdef->uiDefIdx);
			}
			AttribModDefValidateAppliedPowerAnimFX(ppowdef, pdef, bWarnings, GET_REF(pParams->hDef));
		}
		else if(!!ppowdef->bSafeForSelfOnly && pdef->offAttrib==kAttribType_EntCreate)
		{
			ppowdef->bSafeForSelfOnly = false;
		}
		else if(pdef->offAttrib==kAttribType_EntCreateVanity)
		{
			if(pdef->eTarget!=kModTarget_Self)
			{
				ModErrorf(pdef,"%s: %d: %s must target Self",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			}
			if(pdef->eStack==kStackType_Stack)
			{
				ModErrorf(pdef,"%s: %d: %s must not be stack type Stack",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			}

			if(!!ppowdef->bSafeForSelfOnly)
				ppowdef->bSafeForSelfOnly = false;
		}
		else if(pdef->offAttrib==kAttribType_GrantPower)
		{
			GrantPowerParams *pParams = (GrantPowerParams*)(pdef->pParams);
			PowerDef* ppowdefGranted = GET_REF(pParams->hDef);

			if(!ppowdefGranted)
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}
			else if (pdef->pExprDuration && (ppowdefGranted->fLifetimeGame || ppowdefGranted->fLifetimeReal))
			{
				ModErrorf(pdef,"%s: %d: %s has a duration and grants power (%s) that has a lifetime game or lifetime real",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}
		}
		else if(pdef->offAttrib==kAttribType_KillTrigger)
		{
			KillTriggerParams *pParams = (KillTriggerParams*)(pdef->pParams);
			if(!GET_REF(pParams->hDef))
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}
			if(POWERS_CHANCE_ERROR(pParams->fChance))
			{
				ModErrorf(pdef,"%s: %d: Invalid params chance %f (must be (0..1])",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pParams->fChance);
			}
			else if(POWERS_CHANCE_WARNING(pParams->fChance))
			{
				ModWarningf(pdef,"%s: %d: Abnormally large or small params chance %f",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pParams->fChance);
			}
			if(pdef->pExprMagnitude)
			{
				if(!pParams->bMagnitudeIsCharges && !(pdef->pFragility && pdef->pFragility->bMagnitudeIsHealth))
				{
					ModWarningf(pdef,"%s: %d: %s isn't using magnitude for anything",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
				}
			}
			AttribModDefValidateAppliedPowerAnimFX(ppowdef, pdef, bWarnings, GET_REF(pParams->hDef));
		}
		else if(pdef->offAttrib==kAttribType_PowerMode)
		{
			PowerModeParams *pParams = (PowerModeParams*)(pdef->pParams);
			if(pdef->offAspect==kAttribAspect_BasicAbs && pParams->iPowerMode <= kPowerMode_LAST_CODE_SET)
			{
				ModErrorf(pdef,"%s: %d: %s attempting to use %s, which is code set",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),StaticDefineIntRevLookup(PowerModeEnum,pParams->iPowerMode));
			}
			else if(pdef->offAspect!=kAttribAspect_BasicAbs && IS_BASIC_ASPECT(pdef->offAspect))
			{
				ModErrorf(pdef,"%s: %d: %s attempting to use unsupported aspect %s",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),StaticDefineIntRevLookup(AttribAspectEnum,pdef->offAspect));
			}
		}
		else if(pdef->offAttrib==kAttribType_RemovePower)
		{
			RemovePowerParams *pParams = (RemovePowerParams*)(pdef->pParams);
			if(!GET_REF(pParams->hDef))
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}
		}
		else if(pdef->offAttrib==kAttribType_Shield)
		{
			ShieldParams *pParams = (ShieldParams*)(pdef->pParams);
			if(pParams->fPercentIgnored < 0 || pParams->fPercentIgnored > 1)
			{
				ModErrorf(pdef,"%s: %d: %s PercentIgnored must be [0 .. 1]",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			}
		}
		else if(pdef->offAttrib==kAttribType_TeleThrow)
		{
			TeleThrowParams *pParams = (TeleThrowParams*)(pdef->pParams);
			if(!GET_REF(pParams->hDef))
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			if(IS_HANDLE_ACTIVE(pParams->hDefFallback) && !GET_REF(pParams->hDefFallback))
				ModErrorf(pdef,"%s: %d: %s DefFallback is set but not a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDefFallback));
		}
		else if(pdef->offAttrib==kAttribType_TriggerComplex)
		{
			TriggerComplexParams *pParams = (TriggerComplexParams*)(pdef->pParams);
			if(!GET_REF(pParams->hDef))
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}


			if(pdef->pExprMagnitude)
			{
				if(!pParams->bMagnitudeIsCharges && !(pdef->pFragility && pdef->pFragility->bMagnitudeIsHealth))
				{
					ModWarningf(pdef,"%s: %d: %s isn't using magnitude for anything",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
				}
			}

			for(i=eaiSize(&pParams->piCombatEvents)-1; i>=0; i--)
			{
				CombatEvent eEvent = pParams->piCombatEvents[i];
				if(!g_ubCombatEventComplexWhitelist[eEvent])
					ModErrorf(pdef,"%s: %d: %s does not support the %s event",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),StaticDefineIntRevLookup(CombatEventEnum,eEvent));
			}

			if (eaiFind(&pParams->piCombatEvents,kCombatEvent_KillIn) >= 0)
			{
				// to handle the KillIn combatEvent, the attrib must be marked to Survive Target Death
				if (!pdef->bSurviveTargetDeath)
				{
					ModErrorf(pdef,"%s: %d: %s - the %s event requires that the attrib be marked as 'Survive Target Death'",
								powerdef_NameFull(ppowdef), pdef->uiDefIdx,
								StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),
								StaticDefineIntRevLookup(CombatEventEnum,kCombatEvent_KillIn));
				}
			}

			AttribModDefValidateAppliedPowerAnimFX(ppowdef, pdef, bWarnings, GET_REF(pParams->hDef));
		}
		else if(pdef->offAttrib==kAttribType_TriggerSimple)
		{
			TriggerSimpleParams *pParams = (TriggerSimpleParams*)(pdef->pParams);
			if(!GET_REF(pParams->hDef))
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
			}

			if(pdef->pExprMagnitude)
			{
				if(!pParams->bMagnitudeIsCharges && !(pdef->pFragility && pdef->pFragility->bMagnitudeIsHealth))
				{
					ModWarningf(pdef,"%s: %d: %s isn't using magnitude for anything",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
				}
			}

			if (eaiFind(&pParams->piCombatEvents,kCombatEvent_KillIn) >= 0)
			{
				// to handle the KillIn combatEvent, the attrib must be marked to Survive Target Death
				if (!pdef->bSurviveTargetDeath)
				{
					ModErrorf(pdef,"%s: %d: %s - the %s event requires that the attrib be marked as 'Survive Target Death'",
						powerdef_NameFull(ppowdef), pdef->uiDefIdx,
						StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),
						StaticDefineIntRevLookup(CombatEventEnum,kCombatEvent_KillIn));
				}
			}

			AttribModDefValidateAppliedPowerAnimFX(ppowdef, pdef, bWarnings, GET_REF(pParams->hDef));
		}
		else if(pdef->offAttrib==kAttribType_SetCostume)
		{
 			SetCostumeParams *pParams = (SetCostumeParams*)pdef->pParams;
			if (!GET_REF(pParams->hCostume) && !pParams->bCopyCostumeFromSourceEnt)
			{
				if ((!pParams->pcBoneGroup) || (!*pParams->pcBoneGroup))
				{
					ModErrorf(pdef,"%s: %d: %s requires a valid Costume (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hCostume));
				}
			}
			else if (!pParams->bCopyCostumeFromSourceEnt)
			{
				costumeLoad_ValidateCostumeForApply(GET_REF(pParams->hCostume), ppowdef->pchFile);
			}
		}
		else if(pdef->offAttrib == kAttribType_PowerRecharge)
		{
			PowerRechargeParams *pParams = (PowerRechargeParams*)pdef->pParams;

			if (pParams->bAffectsGlobalCooldown && g_CombatConfig.fCooldownGlobal <= 0.f)
			{
				ModErrorf(pdef,"%s: %d: %s requires a valid global cooldown time set in combat config.",
					powerdef_NameFull(ppowdef),
					pdef->uiDefIdx,
					StaticDefineIntRevLookup(AttribTypeEnum, pdef->offAttrib));
			}
		}
		else if(pdef->offAttrib == kAttribType_Notify)
		{
			NotifyParams *pParams = (NotifyParams*)pdef->pParams;

			if (pParams->pchMessageKey && !RefSystem_ReferentFromString(gMessageDict, pParams->pchMessageKey))
			{
				ModErrorf(pdef,"%s: %d: %s is not a valid message key!",
					powerdef_NameFull(ppowdef),
					pdef->uiDefIdx,
					pParams->pchMessageKey);
			}
		}
		else if(pdef->offAttrib == kAttribType_Teleport)
		{
			TeleportParams *pParams = (TeleportParams*)pdef->pParams;

			if (pParams->eTeleportTarget == kAttibModTeleportTarget_Expression && pParams->pTeleportTargetExpr == NULL)
			{
				ModErrorf(pdef,"%s: %d: Teleport attribMod has teleportTarget as expression but has no valid expression!",
						powerdef_NameFull(ppowdef),
						pdef->uiDefIdx);
			}
			else if (pParams->pTeleportTargetExpr && pParams->eTeleportTarget != kAttibModTeleportTarget_Expression)
			{
				ModErrorf(pdef,"%s: %d: Teleport attribMod has an expression, but is not marked as teleportTarget expression!",
					powerdef_NameFull(ppowdef),
					pdef->uiDefIdx);
			}
			else if (pParams->eTeleportTarget == kAttibModTeleportTarget_Expression && pParams->bClientViewTeleport)
			{
				ModErrorf(pdef,"%s: %d: Teleport target expression is not valid with the Client View Teleport flag!",
					powerdef_NameFull(ppowdef),
					pdef->uiDefIdx);
			}

		}
		
	}

	if (pdef->offAttrib == kAttribType_AttribLink)
	{
		if (pdef->eTarget != kModTarget_Self)
			ModErrorf(pdef, "%s: %d: AttribLink Target must be Self", powerdef_NameFull(ppowdef), pdef->uiDefIdx);

		if (pdef->offAspect != kAttribAspect_BasicAbs)
			ModErrorf(pdef, "%s: %d: AttribLink Aspect should be BasicAbs", powerdef_NameFull(ppowdef), pdef->uiDefIdx);
	}

	// Validation that should only be done on a game server, for whatever reason
#ifdef GAMESERVER

	// ppchContinuingFX
	for(i=eaSize(&pdef->ppchContinuingFX)-1; i>=0; i--)
	{
		if(!dynFxInfoExists(pdef->ppchContinuingFX[i]))
		{
			ModErrorf(pdef,"%s: %d: Bad ContinuingFX %s",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->ppchContinuingFX[i]);
		}
	}

	// ppchConditionalFX
	for(i=eaSize(&pdef->ppchConditionalFX)-1; i>=0; i--)
	{
		if(!dynFxInfoExists(pdef->ppchConditionalFX[i]))
		{
			ModErrorf(pdef,"%s: %d: Bad ConditionalFX %s",powerdef_NameFull(ppowdef),pdef->uiDefIdx,pdef->ppchConditionalFX[i]);
		}
	}
#endif
}

// Does REF validation at the attrib mod level.  Does not use ModErrorf because we can't change the state
//  of the Error enum on the PowerDef at this point in time.
static S32 AttribModDefValidateReferences(PowerDef *ppowdef, AttribModDef *pdef)
{
	S32 bReturn = true;

	if(pdef->offAttrib==kAttribType_EntCreate)
	{
		EntCreateParams *pParams = (EntCreateParams*)(pdef->pParams);

		// Validate that we've got a CritterDef OR a CritterGroup
		if(!REF_STRING_FROM_HANDLE(pParams->hCritter) && !REF_STRING_FROM_HANDLE(pParams->hCritterGroup))
		{
			ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,6,11,2009,"%s: %d: %s has neither a critter def nor a critter group defined!\n You must have at least one.",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			bReturn = false;
		}
		else if(REF_STRING_FROM_HANDLE(pParams->hCritter) && REF_STRING_FROM_HANDLE(pParams->hCritterGroup))
		{
			ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,6,11,2009,"%s: %d: %s has both a critter def and critter group defined!\n You may only have one.",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			bReturn = false;
		}

		// Validate existence of reference
		if(REF_STRING_FROM_HANDLE(pParams->hCritter) && !GET_REF(pParams->hCritter))
		{
			ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,6,11,2009,"%s: %d: %s has a critter defined but it is invalid (currently %s).",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hCritter));
			bReturn = false;
		}
		if(REF_STRING_FROM_HANDLE(pParams->hCritterGroup) && !GET_REF(pParams->hCritterGroup))
		{
			ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,6,11,2009,"%s: %d: %s has a critter group defined but it is invalid (currently %s).",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hCritterGroup));
			bReturn = false;
		}

		if(pParams->pcRank && pParams->eCreateType == kEntCreateType_Critter)
		{
			ErrorFilenamef(ppowdef->pchFile, "%s: %d: %s is type 'Critter' but also specifies a rank.  Specifying a rank for a critter type %s does nothing.",
				powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),
				StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			bReturn = false;
		}

		if(pParams->eStrength != kEntCreateStrength_Independent)
		{
			if(pParams->pcRank)
			{
				ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,2,18,2010,"%s: %d: %s has a rank defined but it's create strength is not Independent (currently %s).  Specifying rank does nothing when the create strength is not Independent.",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),StaticDefineIntRevLookup(EntCreateStrengthEnum,pParams->eStrength));
				bReturn = false;
			}
			if(pParams->pcSubRank)
			{
				ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,2,18,2010,"%s: %d: %s has a subrank defined but it's create strength is not Independent (currently %s).  Specifying subrank does nothing when the create strength is not Independent.",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),StaticDefineIntRevLookup(EntCreateStrengthEnum,pParams->eStrength));
				bReturn = false;
			}
		}
	}
	else if(pdef->offAttrib==kAttribType_EntCreateVanity)
	{
		EntCreateVanityParams *pParams = (EntCreateVanityParams*)(pdef->pParams);
		CritterDef *pdefCritter = GET_REF(pParams->hCritter);
		if(!pdefCritter)
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s requires a valid CritterDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hCritter));
			bReturn = false;
		}
		else
		{
			if(!pdefCritter->bNonCombat)
			{
				ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s CritterDef must be flagged as NonCombat",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
				bReturn = false;
			}
		}
	}
	else if(pdef->offAttrib==kAttribType_BecomeCritter)
	{
		BecomeCritterParams *pParams = (BecomeCritterParams*)(pdef->pParams);
		if(!GET_REF(pParams->hCritter))
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s critter defined is invalid (currently %s).",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hCritter));
			bReturn = false;
		}
		if(REF_STRING_FROM_HANDLE(pParams->hClass) && !GET_REF(pParams->hClass))
		{
			ErrorFilenameGroupRetroactivef(ppowdef->pchFile,"Powers",7,6,11,2009,"%s: %d: %s has a class defined but it is invalid (currently %s).",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hClass));
			bReturn = false;
		}
	}
	else if(pdef->offAttrib==kAttribType_GrantReward)
	{
		GrantRewardParams *pParams = (GrantRewardParams*)(pdef->pParams);
		if(!GET_REF(pParams->hRewardTable))
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s requires a valid RewardTable (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hRewardTable));
			bReturn = false;
		}
	}
	else if(pdef->offAttrib==kAttribType_IncludeEnhancement)
	{
		// This is here instead of normal validation so if someone changes an Enhancement this will notice the Enhancement is
		//  no longer valid.
		IncludeEnhancementParams *pParams = (IncludeEnhancementParams*)(pdef->pParams);
		PowerDef *ppowdefEnh = GET_REF(pParams->hDef);
		if(!(ppowdefEnh && ppowdefEnh->eType==kPowerType_Enhancement))
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s requires a valid Enhancement-type PowerDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
		}
		else
		{
			int i;
			for(i=eaSize(&ppowdefEnh->ppMods)-1; i>=0; i--)
			{
				if(!ppowdefEnh->ppMods[i]->bEnhancementExtension)
					break;
			}
			if(i>=0)
				ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s requires the Enhancement PowerDef (%s) to contain only Enhance Extend AttribMods",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hDef));
		}
	}
	else if(pdef->offAttrib==kAttribType_RewardModifier)
	{
		RewardModifierParams *pParams = (RewardModifierParams*)(pdef->pParams);
		ItemDef *pItemDef = GET_REF(pParams->hNumeric);
		if(!pItemDef)
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s requires a valid ItemDef (currently %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hNumeric));
			bReturn = false;
		}
		else if(pItemDef->eType != kItemType_Numeric)
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s requires a valid Numeric ItemDef (currently %s is a %s)",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hNumeric),StaticDefineIntRevLookup(ItemTypeEnum,pItemDef->eType));
			bReturn = false;
		}
	}
	else if (pdef->offAttrib==kAttribType_ProjectileCreate)
	{
		ProjectileCreateParams *pParams = (ProjectileCreateParams*)(pdef->pParams);

		// Validate that we've got a projectile def
		if(!REF_STRING_FROM_HANDLE(pParams->hProjectileDef))
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s has no projectile def defined!",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib));
			bReturn = false;
		}

		if(REF_STRING_FROM_HANDLE(pParams->hProjectileDef) && !GET_REF(pParams->hProjectileDef))
		{
			ErrorFilenamef(ppowdef->pchFile,"%s: %d: %s has a projectile defined but it is invalid (currently %s).",powerdef_NameFull(ppowdef),pdef->uiDefIdx,StaticDefineIntRevLookup(AttribTypeEnum,pdef->offAttrib),REF_STRING_FROM_HANDLE(pParams->hProjectileDef));
			bReturn = false;
		}

	}

	return bReturn;
}


// Validates the input PowerDef, optionally with respect to the previous PowerDef.  Also optionally includes warnings.
S32 powerdef_Validate(PowerDef *pdef, S32 bWarnings)
{
	S32 bRet = 1;
	S32 i, j;
	const char *pchTempFileName;
	const char *pchErrorString;

	pdef->eError = kPowerError_Valid;

#define PowerErrorf(pdef,fmt,...) { pdef->eError |= kPowerError_Error; ErrorFilenamef((pdef)->pchFile,fmt,##__VA_ARGS__); }
#define PowerWarningf(pdef,fmt,...) { pdef->eError |= kPowerError_Warning; if(bWarnings) Alertf(fmt,##__VA_ARGS__); }
// If you need to use this macro, you should probably replace all existing instances with ErrorFilenamef
#define PowerRetroErrorf(pdef,fmt,...) { pdef->eError |= kPowerError_Error; ErrorFilenameGroupRetroactivef((pdef)->pchFile,"Powers",7,8,25,2009,fmt,##__VA_ARGS__); }

	if(!resIsValidNameEx(pdef->pchName,&pchErrorString))
	{
		PowerErrorf(pdef,"%s: Invalid power name - %s",powerdef_NameFull(pdef),pchErrorString);
	}

	if(!resIsValidScope(pdef->pchGroup))
	{
		PowerErrorf(pdef,"%s: Invalid power scope",pdef->pchGroup);
	}

	if(StructInherit_GetParentName(parse_PowerDef, pdef)
		&& StructInherit_GetOverrideType(parse_PowerDef, pdef, ".group") == OVERRIDE_NONE)
	{
		ErrorFilenamef(pdef->pchFile, "Power scope should not be inherited from parent");
		return 0;
	}

	if(IsServer()) 
	{
		pchTempFileName = pdef->pchFile;
		if(resFixPooledFilename(&pchTempFileName, POWERS_BASE_DIR, pdef->pchGroup, pdef->pchName, POWERS_EXTENSION))
		{
			ErrorFilenamef(pdef->pchFile, "Power filename does not match name '%s' scope '%s'", pdef->pchName, pdef->pchGroup);
		}

		if(!REF_STRING_FROM_HANDLE(pdef->msgDisplayName.hMessage)) 
		{
			PowerErrorf(pdef,"Missing display name message");
		}
		else if(!GET_REF(pdef->msgDisplayName.hMessage))
		{
			PowerErrorf(pdef,"Display name message does not exist: %s",REF_STRING_FROM_HANDLE(pdef->msgDisplayName.hMessage));
		}
		else
		{
			Message *pMessage = GET_REF(pdef->msgDisplayName.hMessage);
			if(!pMessage->pcDefaultString || !strlen(pMessage->pcDefaultString))
			{
				PowerErrorf(pdef,"Display name message has no or empty default string: %s",REF_STRING_FROM_HANDLE(pdef->msgDisplayName.hMessage));
			}
			else if(!stricmp(pMessage->pcDefaultString,"New Power"))
			{
				PowerWarningf(pdef,"Display name message is just \"New Power\": %s",REF_STRING_FROM_HANDLE(pdef->msgDisplayName.hMessage));
			}
			else if(strStartsWith(pMessage->pcDefaultString," ") || strEndsWith(pMessage->pcDefaultString," "))
			{
				PowerErrorf(pdef,"Display name message starts or ends with space \"%s\": %s",pMessage->pcDefaultString,REF_STRING_FROM_HANDLE(pdef->msgDisplayName.hMessage));
			}
		}
	}
	else
	{
		// Client side editor-mode copy of the "New Power" name warning
		if(pdef->msgDisplayName.bEditorCopyIsServer
			&& pdef->msgDisplayName.pEditorCopy
			&& !stricmp(pdef->msgDisplayName.pEditorCopy->pcDefaultString,"New Power"))
		{
			PowerWarningf(pdef,"Display name message is just \"New Power\": %s",pdef->pchName);
		}
	}

	if(strlen(pdef->pchName) > MAX_POWER_NAME_LEN)
	{
		PowerErrorf(pdef,"%s: Invalid power name (Too long, cannot exceed %d characters",powerdef_NameFull(pdef),MAX_POWER_NAME_LEN);
	}

	// eAttribCost
	if(ATTRIB_NOT_DEFAULT(pdef->eAttribCost))
	{
		if(IS_SPECIAL_ATTRIB(pdef->eAttribCost))
		{
			PowerErrorf(pdef,"%s: Cost Attrib %s is not valid, must be a basic Attribute",powerdef_NameFull(pdef),StaticDefineIntRevLookup(AttribTypeEnum,pdef->eAttribCost));
		}
	}

	// pExprCost
	if(pdef->pExprCost)
	{
		if(exprIsZero(pdef->pExprCost))
		{
			PowerWarningf(pdef,"%s: Cost expression of 0, should be empty",powerdef_NameFull(pdef));
		}
		if(pdef->eType==kPowerType_Innate || pdef->eType==kPowerType_Enhancement || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Power of type %s doesn't use cost",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		}
	}

	// pExprCostPeriodic
	if(pdef->pExprCostPeriodic)
	{
		if(pdef->eType==kPowerType_Innate || pdef->eType==kPowerType_Enhancement || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Power of type %s doesn't use cost",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		}
	}

	// pExprCostSecondary
	if(pdef->pExprCostSecondary)
	{
		if(exprIsZero(pdef->pExprCostSecondary))
		{
			PowerWarningf(pdef,"%s: Secondary Cost expression of 0, should be empty",powerdef_NameFull(pdef));
		}
		if(pdef->eType==kPowerType_Innate || pdef->eType==kPowerType_Enhancement || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Power of type %s doesn't use cost",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		}
		if(!ATTRIB_NOT_DEFAULT(pdef->eAttribCost))
		{
			PowerErrorf(pdef,"%s: Secondary costs aren't valid when the primary cost attribute is the default",powerdef_NameFull(pdef));
		}
	}

	// pExprCostPeriodicSecondary
	if(pdef->pExprCostPeriodicSecondary)
	{
		if(pdef->eType==kPowerType_Innate || pdef->eType==kPowerType_Enhancement || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Power of type %s doesn't use cost",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		}
		if(!ATTRIB_NOT_DEFAULT(pdef->eAttribCost))
		{
			PowerErrorf(pdef,"%s: Secondary costs aren't valid when the primary cost attribute is the default",powerdef_NameFull(pdef));
		}
	}

	// pExprCostPeriodic
	if(pdef->pExprCostPeriodic)
	{
		if(exprIsZero(pdef->pExprCostPeriodic))
		{
			PowerWarningf(pdef,"%s: Periodic Cost expression of 0, should be empty",powerdef_NameFull(pdef));
		}
		if(pdef->eType==kPowerType_Innate || pdef->eType==kPowerType_Enhancement || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Power of type %s doesn't use cost",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		}
	}

	// iCostPowerMode
	if(pdef->iCostPowerMode)
	{
		if(pdef->iCostPowerMode < kPowerMode_LAST_CODE_SET)
		{
			PowerErrorf(pdef,"%s: Cost PowerMode %s is not valid, only data-set PowerModes may be used",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerModeEnum,pdef->iCostPowerMode));
		}
	}
		
	// bEnhanceCopyLevel
	if(pdef->bEnhanceCopyLevel && pdef->eType!=kPowerType_Enhancement)
		PowerErrorf(pdef,"%s: Non-enhancement Power with Enhance Copy Level",powerdef_NameFull(pdef));

	// hTargetMain
	if(POWERTYPE_TARGETED(pdef->eType))
	{
		if(!IS_HANDLE_ACTIVE(pdef->hTargetMain) || !GET_REF(pdef->hTargetMain))
		{
			PowerErrorf(pdef,"%s: Invalid power main target",powerdef_NameFull(pdef));
		}
	}

	// hTargetAffected
	if(POWERTYPE_TARGETED(pdef->eType))
	{
		if(!IS_HANDLE_ACTIVE(pdef->hTargetAffected) || !GET_REF(pdef->hTargetAffected))
		{
			PowerErrorf(pdef,"%s: Invalid power affected target",powerdef_NameFull(pdef));
		}
	}

	if(pdef->fTargetArc)
	{
		if(pdef->fTargetArc < 0 || pdef->fTargetArc > 360.f)
		{
			PowerErrorf(pdef,"%s: Invalid power target arc. Must be a postive number under 360",powerdef_NameFull(pdef));
		}
	}

	// eEffectArea
	if(!POWERTYPE_TARGETED(pdef->eType) && pdef->eEffectArea!=kEffectArea_Character)
	{
		if (pdef->eType != kPowerType_Enhancement || !pdef->bEnhancePowerFields)
		{
			PowerWarningf(pdef,"%s: Powers of type %s should be Character effect area",powerdef_NameFull(pdef),StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		}
	}
	if(pdef->eEffectArea==kEffectArea_Character)
	{
		PowerTarget* pTargetMain = GET_REF(pdef->hTargetMain);
		PowerTarget* pTargetAffected = GET_REF(pdef->hTargetAffected);

		if(pdef->eTargetVisibilityMain!=pdef->eTargetVisibilityAffected)
		{
			PowerWarningf(pdef,"%s: Character effect area should have same main and affected visibility",powerdef_NameFull(pdef));
		}
	}

	// iMaxTargetsHit
	if(pdef->iMaxTargetsHit < 0)
	{
		PowerErrorf(pdef,"%s: Negative max targets hit",powerdef_NameFull(pdef));
	}
	else if(pdef->iMaxTargetsHit > 0)
	{
		if(pdef->eEffectArea==kEffectArea_Character)
		{
			PowerWarningf(pdef,"%s: Character effect area should not have max targets specified",powerdef_NameFull(pdef));
		}
	}

	// fRange
	if(pdef->fRange < 0)
	{
		PowerErrorf(pdef,"%s: Negative range",powerdef_NameFull(pdef));
	}
	else if(pdef->fRange > 0)
	{
		PowerTarget *pPowerTarget = GET_REF(pdef->hTargetMain);

		if(!POWERTYPE_ACTIVATABLE(pdef->eType) && 
			(pdef->eType != kPowerType_Enhancement && !pdef->bEnhancePowerFields))
		{
			PowerWarningf(pdef,"%s: Non-activating Power with range",powerdef_NameFull(pdef));
		}

		// If it targets Self, and it's not a cylinder or cone, it shouldn't have a range
		if(pPowerTarget
			&& pPowerTarget->bRequireSelf
			&& pdef->eEffectArea!=kEffectArea_Cone
			&& pdef->eEffectArea!=kEffectArea_Cylinder
			&& !pdef->bHasProjectileCreateAttrib)
		{
			PowerWarningf(pdef,"%s: Self-targeting Power with range isn't valid for anything other than a cone, cylinder or power with a projectile create.",powerdef_NameFull(pdef));
		}
	}
	else if (pdef->fRange == 0.f  && 
				(pdef->eEffectArea == kEffectArea_Cone || pdef->eEffectArea == kEffectArea_Cylinder))
	{
		PowerWarningf(pdef,"%s: ",powerdef_NameFull(pdef));
	}

	// fRangeMin
	if(pdef->fRangeMin < 0)
	{
		PowerErrorf(pdef,"%s: Negative min range",powerdef_NameFull(pdef));
	}
	else if(pdef->fRangeMin > 0)
	{
		PowerTarget *pPowerTarget = GET_REF(pdef->hTargetMain);

		if(!POWERTYPE_ACTIVATABLE(pdef->eType))
			PowerErrorf(pdef,"%s: Non-activating Power with min range",powerdef_NameFull(pdef));

		// If it targets Self it shouldn't have a min range
		if(pPowerTarget	&& pPowerTarget->bRequireSelf)
			PowerErrorf(pdef,"%s: Self-targeting Power with min range",powerdef_NameFull(pdef));

		if(pdef->fRangeMin >= pdef->fRange)
			PowerErrorf(pdef,"%s: Min range >= range",powerdef_NameFull(pdef));
	}

	// pExprRadius
	if(pdef->pExprRadius)
	{
		if(!(pdef->eEffectArea==kEffectArea_Cylinder || pdef->eEffectArea==kEffectArea_Sphere || pdef->eEffectArea==kEffectArea_Team))
		{
			PowerErrorf(pdef,"%s: Radius Expression not valid for effect area %s",powerdef_NameFull(pdef),StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
		}
	}

	// pExprInnerRadius
	if(pdef->pExprInnerRadius)
	{
		if(pdef->eEffectArea!=kEffectArea_Sphere)
		{
			PowerErrorf(pdef,"%s: Inner Radius Expression not valid for effect area %s",powerdef_NameFull(pdef),StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
		}
		if (!pdef->pExprRadius)
		{
			PowerErrorf(pdef,"%s: Inner Radius Expression specified with no Radius Expression",powerdef_NameFull(pdef))
		}
	}

	// pExprArc
	if(pdef->pExprArc && pdef->eEffectArea!=kEffectArea_Cone)
		PowerErrorf(pdef,"%s: Arc Expression not valid for effect area %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))

	// cone's fStartingRadius
	if (pdef->fStartingRadius && pdef->eEffectArea != kEffectArea_Cone)
		PowerErrorf(pdef,"%s: Cone Starting Radius only valid for Cone effect area type, not %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))


	// fYaw
	if(pdef->fYaw || pdef->fPitch || pdef->fFrontOffset || pdef->fRightOffset || pdef->fUpOffset)
	{
		PowerTarget *pPowerTarget = GET_REF(pdef->hTargetMain);

		if (pdef->fYaw)
		{
			if (pdef->eEffectArea!=kEffectArea_Cone && pdef->eEffectArea!=kEffectArea_Cylinder)
				PowerErrorf(pdef,"%s: Yaw not valid for effect area %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
			if (pPowerTarget && !pPowerTarget->bRequireSelf)
				PowerErrorf(pdef,"%s: Yaw only allowed on self target only. Talk to a powers programmer about fixing this.",powerdef_NameFull(pdef))
		}

		if (pdef->fPitch)
		{
			if (pdef->eEffectArea!=kEffectArea_Cone && pdef->eEffectArea!=kEffectArea_Cylinder)
				PowerErrorf(pdef,"%s: Pitch not valid for effect area %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
			if (pPowerTarget && !pPowerTarget->bRequireSelf)
				PowerErrorf(pdef,"%s: Pitch only allowed on self target only. Talk to a powers programmer about fixing this.",powerdef_NameFull(pdef))
		}

		if (pdef->fFrontOffset)
		{
			if (pdef->eEffectArea!=kEffectArea_Cone && pdef->eEffectArea!=kEffectArea_Cylinder && pdef->eEffectArea!=kEffectArea_Sphere)
				PowerErrorf(pdef,"%s: Front offset not valid for effect area %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
			if (pPowerTarget && !pPowerTarget->bRequireSelf)
				PowerErrorf(pdef,"%s: Front offset only allowed on self target only. Talk to a powers programmer about fixing this.",powerdef_NameFull(pdef))
		}

		if (pdef->fRightOffset)
		{
			if (pdef->eEffectArea!=kEffectArea_Cone && pdef->eEffectArea!=kEffectArea_Cylinder && pdef->eEffectArea!=kEffectArea_Sphere)
				PowerErrorf(pdef,"%s: Right offset not valid for effect area %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
				if (pPowerTarget && !pPowerTarget->bRequireSelf)
					PowerErrorf(pdef,"%s: Right offset only allowed on self target only. Talk to a powers programmer about fixing this.",powerdef_NameFull(pdef))
		}

		if (pdef->fUpOffset)
		{
			if (pdef->eEffectArea!=kEffectArea_Cone && pdef->eEffectArea!=kEffectArea_Cylinder && pdef->eEffectArea!=kEffectArea_Sphere)
				PowerErrorf(pdef,"%s: Up offset not valid for effect area %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(EffectAreaEnum,pdef->eEffectArea))
			if (pPowerTarget && !pPowerTarget->bRequireSelf)
				PowerErrorf(pdef,"%s: Up offset only allowed on self target only. Talk to a powers programmer about fixing this.",powerdef_NameFull(pdef))
		}
	}
			

	// pExprRequiresQueue
	if(pdef->pExprRequiresQueue)
	{
		if(!(POWERTYPE_ACTIVATABLE(pdef->eType) && pdef->eType!=kPowerType_Combo))
			PowerErrorf(pdef,"%s: Queue requires expression not valid for type %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));
		if(exprUsesNOT(pdef->pExprRequiresQueue))
			PowerErrorf(pdef,"%s: Queue requires expression can not use NOT due to prediction limitations",powerdef_NameFull(pdef));
	}

	// fTimeCharge
	if(pdef->fTimeCharge < 0)
	{
		PowerErrorf(pdef,"%s: Negative charge time",powerdef_NameFull(pdef));
	}
	else if(pdef->fTimeCharge > 0)
	{
		if(!POWERTYPE_ACTIVATABLE(pdef->eType) || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Non-charging Power with charge time",powerdef_NameFull(pdef));
		}
	}

	// fTimeActivate
	if(pdef->fTimeActivate < 0)
	{
		PowerErrorf(pdef,"%s: Negative activate time",powerdef_NameFull(pdef));
	}
	else if(pdef->fTimeActivate > 0)
	{
		if(!POWERTYPE_ACTIVATABLE(pdef->eType) || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Non-activatable Power with activate time",powerdef_NameFull(pdef));
		}
	}

	// fTimeActivatePeriod
	if(pdef->fTimeActivatePeriod < 0)
	{
		PowerErrorf(pdef,"%s: Negative activate period time",powerdef_NameFull(pdef));
	}
	else if(pdef->fTimeActivatePeriod == 0)
	{
		if(POWERTYPE_PERIODIC(pdef->eType) && !pdef->bAutoReapply)
		{
			PowerErrorf(pdef,"%s: Periodic Power must have positive activate period time unless it is AutoReapply",powerdef_NameFull(pdef));
		}
	}
	else
	{
		if(!POWERTYPE_PERIODIC(pdef->eType))
		{
			PowerWarningf(pdef,"%s: Non-periodic Power with activate period time",powerdef_NameFull(pdef));
		}
	}

	// uiPeriodsMax
	if(pdef->uiPeriodsMax)
	{
		if(pdef->eType!=kPowerType_Maintained && pdef->eType!=kPowerType_Toggle)
		{
			PowerWarningf(pdef,"%s: Non-maintainable Power with max periods",powerdef_NameFull(pdef));
		}
	}

	// bAutoReapply
	if(pdef->bAutoReapply)
	{
		if(pdef->eType!=kPowerType_Passive && pdef->eType!=kPowerType_Toggle)
		{
			PowerErrorf(pdef,"%s: AutoReapply only allowed for Passives and Toggles",powerdef_NameFull(pdef));
		}
#ifdef GAMESERVER
		if(!eaiSize(&pdef->piAttribDepend))
		{
			PowerErrorf(pdef,"%s: AutoReapply enabled but Power has no dependencies",powerdef_NameFull(pdef));
		}
#endif
	}

	// fTimeRecharge
	if(pdef->fTimeRecharge < 0)
	{
		PowerErrorf(pdef,"%s: Negative recharge time",powerdef_NameFull(pdef));
	}
	else if(pdef->fTimeRecharge == 0)
	{
		if(pdef->bRechargeRequiresHit && !pdef->bRechargeDisabled)
			PowerErrorf(pdef,"%s: RechargeRequiresHit requires a recharge time",powerdef_NameFull(pdef));
		if(pdef->bRechargeRequiresCombat && !pdef->bRechargeDisabled)
			PowerErrorf(pdef,"%s: RechargeRequiresCombat requires a recharge time",powerdef_NameFull(pdef));
	}
	else if(pdef->fTimeRecharge > 0)
	{
		if(!POWERTYPE_ACTIVATABLE(pdef->eType) || pdef->eType==kPowerType_Combo)
			PowerWarningf(pdef,"%s: Non-activatable Power with recharge time",powerdef_NameFull(pdef));
		if(pdef->fTimeRecharge >= 3600)
			PowerWarningf(pdef,"%s: Recharge time is large, use RechargeDisabled if you do not want it to recharge",powerdef_NameFull(pdef));
		if(pdef->bRechargeDisabled)
			PowerErrorf(pdef,"%s: RechargeDisabled requires the recharge time be set to 0",powerdef_NameFull(pdef));
	}

	// fTimeOverride
	if(pdef->fTimeOverride < 0)
	{
		PowerErrorf(pdef,"%s: Negative override time",powerdef_NameFull(pdef));
	}
	else if(pdef->fTimeOverride > 0)
	{
		if(!POWERTYPE_ACTIVATABLE(pdef->eType) || pdef->eType==kPowerType_Combo)
		{
			PowerWarningf(pdef,"%s: Non-activatable Power with override time",powerdef_NameFull(pdef));
		}
	}

	// piPowerModesRequired, piPowerModesDisallowed
	if(eaiSize(&pdef->piPowerModesRequired) || eaiSize(&pdef->piPowerModesDisallowed))
	{
		if(pdef->eType==kPowerType_Innate)
		{
			PowerWarningf(pdef,"%s: Innate Powers do not respect PowerModes",powerdef_NameFull(pdef));
		}
	}

	// piAttribIgnore
	if(eaiSize(&pdef->piAttribIgnore))
	{
		if(-1!=eaiFind(&pdef->piAttribIgnore,kAttribType_Interrupt))
		{
			PowerWarningf(pdef,"%s: Ignoring the Interrupt attribute is handled by removing it from the Power's the Interrupt list, not by including it in the Power's Ignored Attribute list.",powerdef_NameFull(pdef));
		}
	}

	// pExprEnhanceAttach
	if(pdef->pExprEnhanceAttach && pdef->eType!=kPowerType_Enhancement)
		PowerErrorf(pdef,"%s: Enhancement Attach expression not valid for type %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));

	// pExprEnhanceApply
	if(pdef->pExprEnhanceApply && pdef->eType!=kPowerType_Enhancement)
		PowerErrorf(pdef,"%s: Enhancement Apply expression not valid for type %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));

	// pExprEnhanceEntCreate
	if(pdef->pExprEnhanceEntCreate && pdef->eType!=kPowerType_Enhancement)
		PowerErrorf(pdef,"%s: Enhance For EntCreate expression not valid for type %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));

	// pExprRequiresApply
	if(pdef->pExprRequiresApply && (pdef->eType != kPowerType_Maintained && pdef->eType != kPowerType_Toggle))
		PowerErrorf(pdef,"%s: Apply expression not valid for type %s",powerdef_NameFull(pdef), StaticDefineIntRevLookup(PowerTypeEnum,pdef->eType));

	if(pdef->eType==kPowerType_Combo)
	{
		if(eaSize(&pdef->ppMods))
		{
			PowerErrorf(pdef,"%s: Combo power has attrib mods",powerdef_NameFull(pdef));
		}
		for(i=0; i<eaSize(&pdef->ppCombos); i++)
		{
			PowerCombo *pCombo = pdef->ppCombos[i];
			if(IsServer() && !GET_REF(pCombo->hPower))
			{
				PowerErrorf(pdef,"%s: Combo child %d is invalid or unknown Power %s",powerdef_NameFull(pdef),i+1,REF_STRING_FROM_HANDLE(pCombo->hPower)?REF_STRING_FROM_HANDLE(pCombo->hPower):"(NULL)");
			}
		}
		if(i==0)
		{
			PowerErrorf(pdef,"%s: Combo power with no child Powers",powerdef_NameFull(pdef));
		}
	}
	else
	{
		if(eaSize(&pdef->ppCombos))
		{
			PowerErrorf(pdef,"%s: Non-combo power has combos",powerdef_NameFull(pdef));
		}
	}

	// aiRanges
	if(gConf.bExposeDeprecatedPowerConfigVars)
	{
		if(pdef->iAIMaxRange < pdef->iAIMinRange)
		{
			PowerErrorf(pdef,"%s: AI Max range smaller than AI Min range",powerdef_NameFull(pdef));
		}
	}

	// eAITags
	if (pdef->eAITags & kPowerAITag_Cure && pdef->pAIPowerConfigDefInst)
	{
		if (eaiSize(&pdef->pAIPowerConfigDefInst->curePowerTags) == 0)
		{
			PowerRetroErrorf(pdef, "%s: Powers tagged with Cure AITag must also have 'AI Cure Tags' specified", powerdef_NameFull(pdef));
		}
	}
	
	// piCategories
	if (eaiSize(&pdef->piCategories))
	{
		bool bFoundWeapon = false;
		for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
		{
			PowerCategory *pcat = eaGet(&g_PowerCategories.ppCategories,pdef->piCategories[i]);
			if (pcat && pcat->bWeaponBased)
			{
				if (bFoundWeapon)
				{
					PowerErrorf(pdef,"%s: Cannot have multiple PowerModes that specify DDWeaponBased!",powerdef_NameFull(pdef));
				}
				bFoundWeapon = true;
			}
		}
	}

	if(pdef->pExprRequiresCharge)
	{
		// TODO(JW): Validation
	}

	// bDeactivationLeavesMods and bDeactivationDisablesMods
	if(pdef->bDeactivationLeavesMods || pdef->bDeactivationDisablesMods)
	{
		if(!POWERTYPE_PERIODIC(pdef->eType))
			PowerWarningf(pdef,"%s: Leaving Mods is only legal on periodic Powers",powerdef_NameFull(pdef));
		if(pdef->bDeactivationLeavesMods && pdef->bDeactivationDisablesMods)
			PowerErrorf(pdef,"%s: Leave Mods and Leave Disabled Mods are mutually exclusive",powerdef_NameFull(pdef));
		if(pdef->bModsExpireWithoutPower)
			PowerErrorf(pdef,"%s: Leave Mods and Leave Disabled Mods are mutually exclusive with Mods Expire Without Power",powerdef_NameFull(pdef));
	}

	// eSourceEnterCombat
	if(pdef->eSourceEnterCombat == kPowerEnterCombatType_Always)
	{
		if(pdef->eType == kPowerType_Passive || pdef->eType == kPowerType_Innate)
			PowerErrorf(pdef,"%s: Passive and Innate powers cannot set combat!",powerdef_NameFull(pdef));
	}

	// bHitChanceOneTime
	if(pdef->bHitChanceOneTime)
	{
		if(pdef->eEffectArea!=kEffectArea_Character || !(pdef->eType==kPowerType_Toggle || pdef->eType==kPowerType_Maintained))
		{
			PowerErrorf(pdef,"%s: OneTime HitChance can only be enabled on Toggle and Maintained powers with Character effect area",powerdef_NameFull(pdef));
		}
	}

	//Set safe for self only based on main/affected targets.
	// This gets annulled if certain attribs or apply powers won't allow it to be used
	//  while only affecting self later.
	pdef->bSafeForSelfOnly = PowerDefSafeForSelfOnly(pdef);

	if(IS_HANDLE_ACTIVE(pdef->hFX))
	{
		PowerAnimFX *pafx = GET_REF(pdef->hFX);
		if(!pafx)
		{
			const char *pchBad = REF_STRING_FROM_HANDLE(pdef->hFX);
			// don't generate the error if it's name starts with "arttest" (sadly we can't look at the scope)
			if(!strStartsWith(pchBad,"arttest"))
			{
				PowerErrorf(pdef,"%s: Attempting to reference an unknown powerart: %s",powerdef_NameFull(pdef),pchBad);
			}
		}
	}

	if (pdef->bActivationImmunity && !g_CombatConfig.pPowerActivationImmunities)
	{
		PowerErrorf(pdef,"%s: Activation Immunity is set, but the combatConfig does not define PowerActivationImmunities.",powerdef_NameFull(pdef));
	}

	// Validate Key block is not inherited
	if ((StructInherit_GetParentName(parse_PowerDef, pdef) != NULL) &&
		(StructInherit_GetOverrideType(parse_PowerDef, pdef, ".AttribKeyBlock") == OVERRIDE_NONE))
	{
		PowerErrorf(pdef,"%s: does not have its own attrib key block assigned",powerdef_NameFull(pdef));
	}

	if (pdef->fTimePostMaintain != 0.f && pdef->eType != kPowerType_Maintained)
	{
		PowerErrorf(pdef,"%s: is not a maintained power, but has Post-Maintain Time",powerdef_NameFull(pdef));
	}
	else if (pdef->fTimePostMaintain < 0.f)
	{
		PowerErrorf(pdef,"%s: Post-Maintain Time must be greater than 0",powerdef_NameFull(pdef));
	}

	// Validate all attrib mods
	{
		bool bHasTeleportAttribmod = false;
		bool bHasLinkAttrib = false;
		S32 iAllowInQueueMap = -1;

		for(i=eaSize(&pdef->ppMods)-1; i>=0; i--)
		{
			AttribModDef *pAttribModeDef = pdef->ppMods[i];
			AttribModDefValidate(pdef, pAttribModeDef, bWarnings);
			pdef->eError |= pAttribModeDef->eError;

			pdef->bHasPredictedMods |= !!moddef_IsPredictedAttrib(pAttribModeDef);
			pdef->bHasAttribApplyUnownedPowers |= moddef_HasUnownedPowerApplication(pAttribModeDef);

			if (pAttribModeDef->offAttrib == kAttribType_Teleport)
			{
				if (pAttribModeDef->offAspect == kAttribAspect_BasicAbs)
				{
					if (bHasTeleportAttribmod)
					{
						PowerErrorf(pdef,"%s: has multiple teleport attribMods. This is currently not legal, talk to a systems programmer if you need to do this.",powerdef_NameFull(pdef));
					}
					bHasTeleportAttribmod = true;
				}
			}
			else if (pAttribModeDef->offAttrib == kAttribType_WarpTo)
			{
				WarpToParams *pParams = (WarpToParams*)pAttribModeDef->pParams;
				if (iAllowInQueueMap != -1)
				{
					if ((U32)iAllowInQueueMap != pParams->bAllowedInQueueMap)
					{
						PowerErrorf(pdef,"%s: has multiple WarpTo attribMods, but have conflicting AllowedInQueueMap. This is currently not legal. Talk to a systems programmer if you need to do this.",powerdef_NameFull(pdef));
					}
				}
				else
				{
					iAllowInQueueMap = pParams->bAllowedInQueueMap;
				}
			}
			else if (pAttribModeDef->offAttrib == kAttribType_AttribLink)
			{
				if (bHasLinkAttrib)
				{
					PowerErrorf(pdef,"%s: has multiple AttribLink attribMods. This is currently not legal. Talk to a systems programmer if you need to do this.",powerdef_NameFull(pdef));
				}
				else
				{
					bHasLinkAttrib = true;
				}
			}

			// Slightly crummy hack to copy the error state over to the ordered mods array
			{
				int k=pdef->ppMods[i]->iKey;
				for(j=eaSize(&pdef->ppOrderedMods)-1; j>=0; j--)
				{
					if(pdef->ppOrderedMods[j]->iKey==k)
					{
						pdef->ppOrderedMods[j]->eError = pdef->ppMods[i]->eError;
						break;
					}
				}
			}
		}

		// if we have a link attrib, we need to do some further validation on the attribs
		if (bHasLinkAttrib)
		{
			bool bHasLinkedTargetMod = false;

			for(i=eaSize(&pdef->ppMods)-1; i>=0; i--)
			{
				AttribModDef *pAttribModeDef = pdef->ppMods[i];

				if (pAttribModeDef->offAttrib != kAttribType_AttribLink)
				{
					if (pAttribModeDef->bAttribLinkToSource && pAttribModeDef->eTarget == kModTarget_Target)
					{
						bHasLinkedTargetMod = true;
						break;
					}
				}
			}

			if (!bHasLinkedTargetMod)
			{
				PowerErrorf(pdef,"%s: has an AttribLink attribMod, but no other AttribMods set to Link that go on Target", powerdef_NameFull(pdef));
			}
		}
		
	}
	

	// Validate all PowerCombos
	for(i=eaSize(&pdef->ppCombos)-1;i>=0;i--)
	{
		PowerCombo *pCombo = pdef->ppCombos[i];
		PowerDef *pdefChild = GET_REF(pCombo->hPower);

		if(!IS_HANDLE_ACTIVE(pCombo->hPower))
		{
			PowerErrorf(pdef,"%s: %d: Combo with no PowerDef\n",powerdef_NameFull(pdef),pCombo->iKey);
		}
		
		if(!pdefChild)
		{
			PowerErrorf(pdef,"%s: %d: Combo references invalid PowerDef %s\n",powerdef_NameFull(pdef),pCombo->iKey,REF_STRING_FROM_HANDLE(pCombo->hPower));
		}
		else
		{
			if(!POWERTYPE_ACTIVATABLE(pdefChild->eType))
			{
				PowerErrorf(pdef,"%s: %d: Combo child is non-activatable PowerDef %s\n",powerdef_NameFull(pdef),pCombo->iKey,REF_STRING_FROM_HANDLE(pCombo->hPower));
			}
			else if(pdefChild->eType==kPowerType_Combo)
			{
				PowerErrorf(pdef,"%s: %d: Combo child is also a combo PowerDef %s\n",powerdef_NameFull(pdef),pCombo->iKey,REF_STRING_FROM_HANDLE(pCombo->hPower));
			}

			if(pdefChild->bLimitedUse && pdefChild->fChargeRefillInterval <= 0.f)
			{
				PowerErrorf(pdef,"%s: %d: Combo child %s has limited charges or lifetime\n",powerdef_NameFull(pdef),pCombo->iKey,REF_STRING_FROM_HANDLE(pCombo->hPower));
			}
		}

		if(pCombo->fPercentChargeRequired<0.0f || pCombo->fPercentChargeRequired>1.0f)
		{
			PowerErrorf(pdef,"%s: %d: Combo PercentChargeRequired must be [0 .. 1]",powerdef_NameFull(pdef),pCombo->iKey);
		}

		if(pCombo->pExprRequires)
		{
			if(exprUsesNOT(pCombo->pExprRequires))
			{
				PowerErrorf(pdef,"%s: %d: Combo requires expression can not use NOT due to prediction limitations",powerdef_NameFull(pdef),pCombo->iKey);
			}
			if(exprIsZero(pCombo->pExprRequires))
			{
				PowerWarningf(pdef,"%s %d: Combo requires expression is 0, should be deleted",powerdef_NameFull(pdef),pCombo->iKey);
			}
		}
	}

	if(IsServer())
	{
		// Validate additional display messages
		if (REF_STRING_FROM_HANDLE(pdef->msgDescription.hMessage) && !GET_REF(pdef->msgDescription.hMessage))
		{
			PowerErrorf(pdef,"%s: power description message does not exist",REF_STRING_FROM_HANDLE(pdef->msgDescription.hMessage));
		}
		if (REF_STRING_FROM_HANDLE(pdef->msgDescriptionLong.hMessage) && !GET_REF(pdef->msgDescriptionLong.hMessage))
		{
			PowerErrorf(pdef,"%s: power description long message does not exist",REF_STRING_FROM_HANDLE(pdef->msgDescriptionLong.hMessage));
		}
		if (REF_STRING_FROM_HANDLE(pdef->msgDescriptionFlavor.hMessage) && !GET_REF(pdef->msgDescriptionFlavor.hMessage))
		{
			PowerErrorf(pdef,"%s: power description flavor message does not exist",REF_STRING_FROM_HANDLE(pdef->msgDescriptionFlavor.hMessage));
		}
		if (REF_STRING_FROM_HANDLE(pdef->msgRankChange.hMessage) && !GET_REF(pdef->msgRankChange.hMessage))
		{
			PowerErrorf(pdef,"%s: power rank change message does not exist",REF_STRING_FROM_HANDLE(pdef->msgRankChange.hMessage));
		}
	}

	// This is here because it depends on other powers, that were set up in POST_TEXT_READING,
	// but it has to be before things go into shared memory
	if(pdef->eType==kPowerType_Combo)
	{
		eaiDestroy(&pdef->tags.piTags);
		for(i=eaSize(&pdef->ppOrderedCombos)-1; i>=0; i--)
		{
			PowerDef *ppowSub = GET_REF(pdef->ppOrderedCombos[i]->hPower);
			if(ppowSub)
			{
				for(j=eaiSize(&ppowSub->tags.piTags)-1; j>=0; j--)
				{
					eaiPushUnique(&pdef->tags.piTags,ppowSub->tags.piTags[j]);
				}
			}
		}
	}

	if(pdef->eType == kPowerType_Instant)
	{
		if(pdef->bAlwaysQueue)
		{
			PowerErrorf(pdef,"%s: Instant powers cannot be always queue",REF_STRING_FROM_HANDLE(pdef->msgDescriptionFlavor.hMessage));
		}

		if(pdef->fTimeCharge > 0.f)
		{
			PowerErrorf(pdef,"%s: Instant powers cannot have a charge time",REF_STRING_FROM_HANDLE(pdef->msgDescriptionFlavor.hMessage));
		}
	}

	if (g_CombatConfig.specialAttribModifiers.eaSpecialCriticalAttribs)
	{
		PowerDefDeriveAndSetCritAttribs(pdef, bWarnings);
	}

	if (pdef->eType == kPowerType_Toggle && pdef->eInterrupts)
	{	// if this is a toggle power and it has interrupt conditions, check to see if the interrupts are setup to be handled 
		// kPowerInterruption_Requested is always valid, but the rest need to be opt-ed into in the combatConfig. 
		PowerInterruption eInterruptsDef = (pdef->eInterrupts & ~kPowerInterruption_Requested);
		if ((eInterruptsDef & g_CombatConfig.eInterruptToggles) != eInterruptsDef)
		{	
			U32 bit = kPowerInterruption_Movement; 
			while (bit < kPowerInterruption_Count)
			{
				if (bit & pdef->eInterrupts && !(bit & g_CombatConfig.eInterruptToggles))
				{
					PowerErrorf(pdef,"%s: Toggle interrupt flag %s is not defined to be able to interrupt toggles in the CombatConfig!",
									powerdef_NameFull(pdef), StaticDefineIntRevLookup(PowerInterruptionEnum,bit));
				}
				bit = bit << 1;
			}
		}
	}

	return !(pdef->eError&kPowerError_Error);
}



// Validates the references of the input PowerDef.  This must be called after all data has been loaded, so all
//  references should be filled in.  Used to check references that would be circular during load-time, such as
//  CritterDefs.
S32 powerdef_ValidateReferences(int iPartitionIdx, PowerDef *pdef)
{
	S32 bReturn = true, bCustom = false;
	int i;

	// Client can't do this correctly (e.g. doesn't get reward tables), so don't do it at all
	if(!IsServer())
		return true;
	
	if (IS_HANDLE_ACTIVE(pdef->hCostRecipe))
	{
		ItemDef* pRecipe = GET_REF(pdef->hCostRecipe);
		if (pRecipe->eType != kItemType_ItemRecipe)
		{
			return false;
		}
	}

	for(i = eaSize(&pdef->ppMods)-1; i >= 0; i--)
	{
		if(!AttribModDefValidateReferences(pdef,pdef->ppMods[i]))
			bReturn = false;
		bCustom |= IS_HANDLE_ACTIVE(pdef->ppMods[i]->msgAutoDesc.hMessage);
	}

	bCustom |= IS_HANDLE_ACTIVE(pdef->msgAutoDesc.hMessage);

	// If there is a custom AutoDesc message, or any mods have custom AutoDesc messages,
	//  try to generate it.  We do this here so all CritterDefs are loaded, etc, since
	//  they can be referenced.
	if(bCustom)
	{
		AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
		powerdef_AutoDesc(iPartitionIdx,pdef,NULL,pAutoDescPower,NULL,NULL,NULL,NULL,NULL,NULL,1,true,kAutoDescDetail_Normal,NULL,NULL);
		StructDestroy(parse_AutoDescPower, pAutoDescPower);
	}

	return bReturn;
}

// Sorts various arrays on the PowerDef, done on load and pre-save to keep the data clean.
void powerdef_SortArrays(PowerDef *pdef)
{
	int i;
	eaiQSortG(pdef->piCategories, intCmp);
	eaiQSortG(pdef->tags.piTags, intCmp);
	eaiQSortG(pdef->piPowerModesRequired, intCmp);
	eaiQSortG(pdef->piPowerModesDisallowed, intCmp);

	for(i=eaSize(&pdef->ppMods)-1; i>=0; i--)
	{
		AttribModDef *pmoddef = pdef->ppMods[i];
		eaiQSortG(pmoddef->tags.piTags, intCmp);
		eaiQSortG(pmoddef->piSensitivities, intCmp);
	}
}

static int SortComboDefsByPriority(const PowerCombo **a, const PowerCombo **b)
{
	F32 fDif = (*a)->fOrder - (*b)->fOrder;
	if(fDif>0) return 1;
	if(fDif<0) return -1;
	return 0;
}

static int SortAttribModDefsByPriority(const AttribModDef **a, const AttribModDef **b)
{
	F32 fDif = (*a)->fApplyPriority - (*b)->fApplyPriority;
	if(fDif>0) return 1;
	if(fDif<0) return -1;
	return 0;
}

// Fixes backpointers from AttribMod defs to Power defs
static void PowerDefFixBackpointers(PowerDef *ppow)
{
	int i;
	for(i=eaSize(&ppow->ppOrderedMods)-1; i>=0; i--)
	{
		AttribModDef *pmod = ppow->ppOrderedMods[i];
		pmod->pPowerDef = ppow;
	}
	for(i=eaSize(&ppow->ppMods)-1; i>=0; i--)
	{
		AttribModDef *pmod = ppow->ppMods[i];
		pmod->pPowerDef = ppow;
	}
}

// Walk the (presumably not-unrolled) OrderedMods earray, unroll it, and set up the uiDefIdx
static void PowerDefUnrollOrderedMods(PowerDef *ppow)
{
	int i;

	int iModCount = 0; // Side-effect producing AttribMod count

	for(i=0; i<eaSize(&ppow->ppOrderedMods); i++)
	{
		int k;
		AttribModDef *pDef = ppow->ppOrderedMods[i];
		AttribType *pUnroll = attrib_Unroll(pDef->offAttrib);

		if(pDef->bDerivedInternally)
			continue;
		
		if(!pDef->bEnhancementExtension || ppow->eType==kPowerType_Enhancement)
			iModCount++;

		// Unset the priority, since we don't need it in the ordered list
		pDef->fApplyPriority = 0;

		// Insert each derived def.  Since we're inserting, unroll from the back
		for(k=eaiSize(&pUnroll)-1; k>=0; k--)
		{
			AttribModDef *pDefDerived = StructAlloc(parse_AttribModDef);
			StructCopyFields(parse_AttribModDef,pDef,pDefDerived,0,0);

			pDefDerived->offAttrib = pUnroll[k];
			pDefDerived->bDerivedInternally = true;

			// Don't include bits/fx on derived defs
			eaDestroy(&pDefDerived->ppchContinuingBits);
			eaDestroy(&pDefDerived->ppchContinuingFX);
			eaDestroyStruct(&pDefDerived->ppContinuingFXParams, parse_PowerFXParam);
			eaDestroy(&pDefDerived->ppchConditionalBits);
			eaDestroy(&pDefDerived->ppchConditionalFX);
			eaDestroyStruct(&pDefDerived->ppConditionalFXParams, parse_PowerFXParam);

			// Don't include expiration on derived defs
			if(pDefDerived->pExpiration)
			{
				StructDestroySafe(parse_ModExpiration, &pDefDerived->pExpiration);
			}

			// Clear other unnecessary data
			pDefDerived->uiAutoDescKey = 0;

			eaInsert(&ppow->ppOrderedMods,pDefDerived,i+1);
		}
	}

	// Set the multi attrib power flag
	ppow->bMultiAttribPower = iModCount > 1;

	// Set up indexes
	for(i=eaSize(&ppow->ppOrderedMods)-1; i>=0; i--)
	{
		ppow->ppOrderedMods[i]->uiDefIdx = i;
	}

	// Any final fixup
	PowerDefFixBackpointers(ppow);
}

// Finds the max of this PowerDef's file timestamp and any of its parents
static U32 PowerDefLastChanged(PowerDef *pdef)
{
	U32 uiLast = fileLastChanged(pdef->pchFile);
	if(pdef->pInheritance)
	{
		PowerDef *pdefParent = RefSystem_ReferentFromString(g_hPowerDefDict, pdef->pInheritance->pParentName);
		if(pdefParent)
		{
			U32 uiLastParent = PowerDefLastChanged(pdefParent);
			MAX1(uiLast,uiLastParent);
		}
	}
	return uiLast;
}

// if the combatConfig has special critAttribs, determine what attribs it uses
static void PowerDefDeriveAndSetCritAttribs(PowerDef *pdef, S32 bWarnings)
{
	pdef->eCriticalChanceAttrib = -1;
	pdef->eCriticalSeverityAttrib = -1;

	if (eaSize(&g_CombatConfig.specialAttribModifiers.eaSpecialCriticalAttribs) && 
		pdef->eType != kPowerType_Enhancement && pdef->eType != kPowerType_Innate)
	{
		S32 i;
		S32 iNumMods = eaSize(&pdef->ppOrderedMods);

		for(i = 0; i < iNumMods; ++i)
		{
			AttribModDef *pmod = pdef->ppOrderedMods[i];

			if (pmod->eTarget == kModTarget_Target && IS_DAMAGE_ATTRIBASPECT(pmod->offAttrib, pmod->offAspect))
			{
				AttribType	eCritChance = kAttribType_CritChance, 
					eCritSeverity = kAttribType_CritSeverity;

				combatConfig_FindSpecialCritAttrib(pmod->offAttrib, &eCritChance, &eCritSeverity);

				if (pdef->eCriticalChanceAttrib != -1 &&
					(pdef->eCriticalChanceAttrib != eCritChance || pdef->eCriticalSeverityAttrib != eCritSeverity))
				{
					const char *pchChance = StaticDefineIntRevLookup(AttribTypeEnum,pdef->eCriticalChanceAttrib);
					const char *pchSeverity = StaticDefineIntRevLookup(AttribTypeEnum,pdef->eCriticalSeverityAttrib);

					PowerWarningf(pdef, 
								"%s: Power found multiple damage types mapping to different crit attribs."
								"Using %s, %s", pdef->pchName, (pchChance ? pchChance : ""), (pchSeverity ? pchSeverity : ""));
					break;
				}

				pdef->eCriticalChanceAttrib = eCritChance;
				pdef->eCriticalSeverityAttrib = eCritSeverity;

				// making it valid to have multiple damage types, but only the first will determine which critical attrib to use
				// if g_bPowersDebug is set, it will throw an error so designers can see what powers have potential issues
				if (!bWarnings)
					break;	
			}
		}
	}
}


// Generates a bunch of expressions, and does some other early data tasks
static S32 PowerDefGenerate(PowerDef *ppow)
{
	int i,j;
	float fMaxApplyPriority = 0.0f;
	AttribModDef *pmodDefTeleport = NULL;

	// Set the version
	ppow->uiVersion = PowerDefLastChanged(ppow);

	resAddValueDep("PowerVersion");

	// Fix powerart reference
	if(IS_HANDLE_ACTIVE(ppow->hFX))
	{
		const char *pchFX = REF_STRING_FROM_HANDLE(ppow->hFX);
		char achTemp[256];
		getFileNameNoExtNoDirs(achTemp, pchFX);
		SET_HANDLE_FROM_STRING(g_hPowerAnimFXDict, achTemp, ppow->hFX);
	}

	combateval_Init(false);

	// Default value for this translates to 0.5 (since we can't set that as the actual default)
	if(ppow->fTimeAllowQueue<0)
		ppow->fTimeAllowQueue = 0.5;

	// Cache bLimitedUse flag
	ppow->bLimitedUse = (0!=ppow->iCharges
						|| 0!=ppow->fLifetimeReal
						|| 0!=ppow->fLifetimeGame
						|| 0!=ppow->fLifetimeUsage);

	// Cache PowerCategory-based flags
	ppow->bSlottingRequired = false;
	ppow->bToggleExclusive = false;
	ppow->bAutoAttackServer = false;
	ppow->bAutoAttackEnabler = false;
	ppow->bAutoAttackDisabler = false;
	ppow->bWeaponBased = false;
	for(i=eaiSize(&ppow->piCategories)-1; i>=0; i--)
	{
		PowerCategory *pcat = g_PowerCategories.ppCategories[ppow->piCategories[i]];
		ppow->bSlottingRequired |= pcat->bSlottingRequired;
		ppow->bToggleExclusive |= (pcat->bToggleExclusive);
		ppow->bAutoAttackServer |= (ppow->eType==kPowerType_Click && pcat->bAutoAttackServer);
		ppow->bAutoAttackEnabler |= pcat->bAutoAttackEnabler;
		ppow->bAutoAttackDisabler |= pcat->bAutoAttackDisabler;
		ppow->bWeaponBased |= pcat->bWeaponBased;
	}

	// Temporary fixup for tracking
	if(!ppow->bTrackTarget)
	{
		ppow->eTracking = kTargetTracking_UntilFirstApply;
		ppow->bTrackTarget = true;
	}

	// Derive the fTimeMaintain
	ppow->fTimeMaintain = ppow->uiPeriodsMax * ppow->fTimeActivatePeriod;

	ppow->fTotalCastPeriod = ppow->fTimePreactivate + ppow->fTimeActivate + ppow->fTimeRecharge;

	combateval_Generate(ppow->pExprRequiresQueue,kCombatEvalContext_Activate);
	combateval_Generate(ppow->pExprRequiresCharge,kCombatEvalContext_Activate);
	combateval_Generate(ppow->pExprCost,kCombatEvalContext_Activate);
	combateval_Generate(ppow->pExprCostPeriodic,kCombatEvalContext_Activate);
	combateval_Generate(ppow->pExprCostSecondary,kCombatEvalContext_Activate);
	combateval_Generate(ppow->pExprCostPeriodicSecondary,kCombatEvalContext_Activate);
	combateval_Generate(ppow->pExprRadius,kCombatEvalContext_Target);
	combateval_Generate(ppow->pExprInnerRadius,kCombatEvalContext_Target);
	combateval_Generate(ppow->pExprArc,kCombatEvalContext_Target);
	combateval_Generate(ppow->pExprEnhanceAttach,kCombatEvalContext_Enhance);
	combateval_Generate(ppow->pExprEnhanceApply,kCombatEvalContext_Apply);
	combateval_Generate(ppow->pExprEnhanceEntCreate,kCombatEvalContext_EntCreateEnhancements);
	combateval_Generate(ppow->pExprRequiresApply,kCombatEvalContext_Apply);

	if(ppow->pExprAICommand)
	{
#ifdef GAMESERVER
		ExprContext *pContext = aiGetStaticCheckExprContext();
		exprGenerate(ppow->pExprAICommand, pContext);
#endif
	}

	// Generate AI data exprs
	if(ppow->pAIPowerConfigDefInst)
	{
#ifdef GAMESERVER
		aiPowerConfigDefGenerateExprs(ppow->pAIPowerConfigDefInst);
#endif
	}

	// Generate the combos
	ppow->bComboTargetRules = false;
	ppow->bComboToggle = false;
	for(i=eaSize(&ppow->ppCombos)-1; i>=0; i--)
	{
		PowerDef *pdefChild = GET_REF(ppow->ppCombos[i]->hPower);
		ppow->bComboToggle |= (pdefChild && pdefChild->eType==kPowerType_Toggle);
		ppow->bComboTargetRules |= !!ppow->ppCombos[i]->pExprTargetClient;

		combateval_Generate(ppow->ppCombos[i]->pExprRequires,kCombatEvalContext_Activate);
		combateval_Generate(ppow->ppCombos[i]->pExprTargetClient,kCombatEvalContext_Activate);
	}

	//Fix the order of the combos
	j=eaSize(&ppow->ppCombos);
	fMaxApplyPriority = 0.0f;
	for(i=0;i<j;i++)
	{
		fMaxApplyPriority = max(ppow->ppCombos[i]->fOrder,fMaxApplyPriority);
	}
	fMaxApplyPriority = ceil(fMaxApplyPriority + 1.0f);
	for(i=0;i<j;i++)
	{
		if(ppow->ppCombos[i]->fOrder<=0.0f)
		{
			ppow->ppCombos[i]->fOrder = fMaxApplyPriority;
			fMaxApplyPriority += 1.0f;
		}
	}
	eaCopyStructs(&ppow->ppCombos,&ppow->ppOrderedCombos,parse_PowerCombo);
	eaQSortG(ppow->ppOrderedCombos,SortComboDefsByPriority);

	j = eaSize(&ppow->ppMods);
	fMaxApplyPriority = 0.0f;

	// TODO(JW): Remove this once all the data is cleaned up
	for(i=0; i<j; i++)
	{
		if(ppow->ppMods[i]->offAttrib<0) ppow->ppMods[i]->offAttrib = 0;
		if(ppow->ppMods[i]->offAspect<0) ppow->ppMods[i]->offAspect = 0;
	}

	// Find the max apply priority of the mods in this power def, and then start
	//  at 1 + that, and fill in all the bad apply priorities.
	for(i=0; i<j; i++)
	{
		fMaxApplyPriority = max(ppow->ppMods[i]->fApplyPriority,fMaxApplyPriority);
	}
	fMaxApplyPriority = ceil(fMaxApplyPriority + 1.0f);
	for(i=0; i<j; i++)
	{
		if(ppow->ppMods[i]->fApplyPriority<=0.0f)
		{
			ppow->ppMods[i]->fApplyPriority = fMaxApplyPriority;
			fMaxApplyPriority += 1.0f;
		}
	}



#ifndef GAMECLIENT
	ppow->bModsIgnoreStrength = true;
#endif
	eaDestroyStruct(&ppow->ppSpecialModsClient,parse_AttribModDef);
	
	for(i=0; i<eaSize(&ppow->ppMods); i++)
	{
		AttribModDef *pDef = ppow->ppMods[i];

		// Fix missing params struct.  This happens if a struct is added after some powers
		// have been created
		ParseTable *pParseTable = characterattribs_GetSpecialParseTable(pDef->offAttrib);
		if (pParseTable && !pDef->pParams)
		{
			pDef->pParams = StructCreateVoid(pParseTable);
			pDef->pParams->eType = pDef->offAttrib;
		}
		
		if (pDef->offAttrib == kAttribType_ProjectileCreate)
		{
			ppow->bHasProjectileCreateAttrib = true;
		}
		else if (pDef->offAttrib == kAttribType_Teleport)
		{
			TeleportParams *pParams = (TeleportParams*)(pDef->pParams);
			if (pParams->bClientViewTeleport)
			{	
				// Only flag the power as having a teleport attrib if the teleport attrib is for the client to process
				ppow->bHasTeleportAttrib = true;
				pmodDefTeleport = pDef;
			}
		}
		else if (pDef->offAttrib == kAttribType_DynamicAttrib)
		{
			DynamicAttribParams *pParams = (DynamicAttribParams*)pDef->pParams;

			if(pParams->pExprAttrib)
			{
				if(ppow->eType==kPowerType_Innate
					|| (ppow->eType==kPowerType_Enhancement
					&& !pDef->bEnhancementExtension
					&& IS_BASIC_ASPECT(pDef->offAspect))
					|| (ppow->eType!=kPowerType_Enhancement
					&& pDef->bEnhancementExtension
					&& IS_BASIC_ASPECT(pDef->offAspect)))
				{
					combateval_Generate(pParams->pExprAttrib,kCombatEvalContext_Simple);
				}
				else
				{
					combateval_Generate(pParams->pExprAttrib,kCombatEvalContext_Apply);
				}
			}
		}

		//Fixup the sensitivity to strength and resistance
#ifndef GAMECLIENT
		pDef->fSensitivityResistance = moddef_GetSensitivity(pDef, kSensitivityType_Resistance);
		pDef->fSensitivityStrength = moddef_GetSensitivity(pDef, kSensitivityType_Strength);
		pDef->fSensitivityImmune = moddef_GetSensitivity(pDef, kSensitivityType_Immune);
		if(pDef->fSensitivityStrength > 0)
			ppow->bModsIgnoreStrength = false;
#endif
	}

	// Check for special AttribMods that need to be client visible.
	// Copy these attribs into a special array.
	if (pmodDefTeleport)
	{
		AttribModDef *pDefClient = StructAlloc(parse_AttribModDef);
		StructCopyFields(parse_AttribModDef,pmodDefTeleport,pDefClient,0,TOK_SERVER_ONLY);
		eaPush(&ppow->ppSpecialModsClient,pDefClient);
	}

	// check for warp attribMods, if we find any add them to the ppSpecialModsClient and flag the power 
	// that it has warp attribMods
	{
		AttribModDef *pmodDefWarp = NULL;
		
		while (pmodDefWarp = powerdef_GetWarpAttribMod(ppow, false, pmodDefWarp))
		{
			AttribModDef *pDefClient = StructAlloc(parse_AttribModDef);

			ppow->bHasWarpAttrib = true;
			
			StructCopyFields(parse_AttribModDef, pmodDefWarp, pDefClient, 0, TOK_SERVER_ONLY);
			eaPush(&ppow->ppSpecialModsClient, pDefClient);
		}
		
	}
	

	// Make the ppOrderedMods array.. starting with a basic sorted copy of ppMods
	eaCopyStructs(&ppow->ppMods,&ppow->ppOrderedMods,parse_AttribModDef);
	eaQSortG(ppow->ppOrderedMods,SortAttribModDefsByPriority);

	// Unroll!
	PowerDefUnrollOrderedMods(ppow);

	// Generate the expressions and other misc attrib-based book-keeping
	eaiDestroy(&ppow->tags.piTags);
	ppow->bEnhancementExtension = false;
	ppow->bSelfOnce = false;
	ppow->bMissMods = false;
	ppow->bApplyObjectDeath = false;
	ppow->bRequiresCooldown = true; //Set to true to trick powerDef_GetCooldown to check the cooldown
	ppow->bRequiresCooldown = powerdef_GetCooldown(ppow) ? true : false;
	for(i=eaSize(&ppow->ppOrderedMods)-1; i>=0; i--)
	{
		AttribModDef *pmod = ppow->ppOrderedMods[i];
		// TODO(JW): Handle Trigger children

		// Set up derived flags
		pmod->bHasAnimFX = (eaSize(&pmod->ppchContinuingBits)
			|| eaSize(&pmod->ppchContinuingFX)
			|| eaSize(&pmod->ppchConditionalBits)
			|| eaSize(&pmod->ppchConditionalFX)
			|| eaSize(&pmod->ppchAttribModDefStanceWordText)
			|| pmod->pchAttribModDefAnimKeywordText
			);

		pmod->bSaveApplyStrengths = (pmod->pExpiration
			|| pmod->offAttrib==kAttribType_ApplyPower
			|| pmod->offAttrib==kAttribType_DamageTrigger
			|| (pmod->offAttrib==kAttribType_EntCreate && pmod->pParams && ((EntCreateParams*)pmod->pParams)->eStrength==kEntCreateStrength_Locked)
			|| pmod->offAttrib==kAttribType_KillTrigger
			|| pmod->offAttrib==kAttribType_TeleThrow
			|| pmod->offAttrib==kAttribType_TriggerComplex
			|| pmod->offAttrib==kAttribType_TriggerSimple
			|| pmod->offAttrib==kAttribType_ProjectileCreate);

		pmod->bSaveSourceDetails = (pmod->pExpiration
			|| pmod->offAttrib==kAttribType_ApplyPower
			|| pmod->offAttrib==kAttribType_DamageTrigger
			|| pmod->offAttrib==kAttribType_EntCreate
			|| pmod->offAttrib==kAttribType_KillTrigger
			|| pmod->offAttrib==kAttribType_TeleThrow
			|| pmod->offAttrib==kAttribType_TriggerComplex
			|| pmod->offAttrib==kAttribType_TriggerSimple);

		//Save the hue of mods that save source details, have conditional or continuing fx, grant a power or are a shield mod with HitFX.
		pmod->bSaveHue = (pmod->bSaveSourceDetails
			|| pmod->ppchContinuingFX 
			|| pmod->ppchConditionalFX
			|| pmod->offAttrib==kAttribType_GrantPower
			|| pmod->offAttrib==kAttribType_BecomeCritter
			|| pmod->offAttrib==kAttribType_ProjectileCreate
			|| (pmod->offAttrib==kAttribType_Shield
				&& pmod->pParams
				&& ((ShieldParams*)pmod->pParams)->pchHitFX) );

		if(pmod->bHasAnimFX && pmod->pPowerDef->pchIconName
			&& (eaSize(&pmod->ppchContinuingFX)
				|| eaSize(&pmod->ppchConditionalFX)))
		{
			for(j=eaSize(&pmod->ppchContinuingFX)-1; j>=0; j--)
			{
				if(strEndsWith(pmod->ppchContinuingFX[j],"_powericon"))
					pmod->bPowerIconCFX = true;
			}
			for(j=eaSize(&pmod->ppchConditionalFX)-1; j>=0; j--)
			{
				if(strEndsWith(pmod->ppchConditionalFX[j],"_powericon"))
					pmod->bPowerIconCFX = true;
			}
		}


		// Note if the power needs to be tagged as an extending enhancement
		ppow->bEnhancementExtension |= (pmod->bEnhancementExtension);

		// Note if the PowerDef has SelfOnce mods
		ppow->bSelfOnce |= (pmod->eTarget==kModTarget_SelfOnce);

		// Note if the PowerDef has Miss or HitOrMiss mods
		ppow->bMissMods |= (pmod->eHitTest==kModHitTest_Miss || pmod->eHitTest==kModHitTest_HitOrMiss);

		// Note if the PowerDef has ApplyObjectDeath mods
		ppow->bApplyObjectDeath |= (pmod->offAttrib==kAttribType_ApplyObjectDeath);

		// Update the power's tags
		for(j=eaiSize(&pmod->tags.piTags)-1; j>=0; j--)
		{
			eaiPushUnique(&ppow->tags.piTags,pmod->tags.piTags[j]);
		}

		// Generate expressions
		combateval_Generate(pmod->pExprRequires,kCombatEvalContext_Apply);
		combateval_Generate(pmod->pExprDuration,kCombatEvalContext_Apply);
		if(pmod->pFragility)
		{
			combateval_Generate(pmod->pFragility->pExprHealth,kCombatEvalContext_Apply);
		}
		pmod->bForever = !exprCompare(pmod->pExprDuration,s_pExprForever);

		// Simple context is used for magnitude on
		//  All mods of Innates
		//  Basic aspect non-extension mods of Enhancements
		//  Basic aspect extension mods of non-Enhancements
		if(ppow->eType==kPowerType_Innate
			|| (ppow->eType==kPowerType_Enhancement
				&& !pmod->bEnhancementExtension
				&& IS_BASIC_ASPECT(pmod->offAspect))
			|| (ppow->eType!=kPowerType_Enhancement
				&& pmod->bEnhancementExtension
				&& IS_BASIC_ASPECT(pmod->offAspect)))
		{
			combateval_Generate(pmod->pExprMagnitude,kCombatEvalContext_Simple);
		}
		else
		{
			combateval_Generate(pmod->pExprMagnitude,kCombatEvalContext_Apply);
		}

		combateval_Generate(pmod->pExprAffects,kCombatEvalContext_Affects);

		if(pmod->pExpiration)
		{
			combateval_Generate(pmod->pExpiration->pExprRequiresExpire,kCombatEvalContext_Expiration);
		}
		if(pmod->pExprChance)
		{
			combateval_Generate(pmod->pExprChance,kCombatEvalContext_Apply);
		}

		if(pmod->bHasAnimFX)
		{
			moddef_GenerateFXParams(pmod);
		}

		if(ppow->bAutoReapply)
		{
			// Currently we only include dependencies from magnitude, because it's expected that
			//  the mods will generally be forever duration when the Power reapplies.
			// We do NOT include dependencies from mods of PowerDefs this mod may apply.  Properly
			//  tracking down such dependencies is difficult, and I don't think there's actually
			//  a valid use case for it - it doesn't make any sense with triggers, so the only
			//  real case would be an ApplyPower mod, which could just set the magnitude in the
			//  ApplyPower mod itself, and then use ModMag() or similar.  Plus, applying
			//  "permanent" mods via an ApplyPower in a periodic Power doesn't make any sense.
			PowerExprAttribDependency(pmod->pExprMagnitude,&ppow->piAttribDepend);
		}

		moddef_Load_Generate(pmod);
	}

	// Copy the non-derived ordered AttribMods into a special client array
	eaDestroyStruct(&ppow->ppOrderedModsClient,parse_AttribModDef);
	for(i=0; i<eaSize(&ppow->ppOrderedMods); i++)
	{
		if(!ppow->ppOrderedMods[i]->bDerivedInternally)
		{
			AttribModDef *pDefClient = StructAlloc(parse_AttribModDef);
			StructCopyFields(parse_AttribModDef,ppow->ppOrderedMods[i],pDefClient,0,TOK_SERVER_ONLY);
			eaPush(&ppow->ppOrderedModsClient,pDefClient);
		}
	}

	// This is here to make sure the IndexTags are proper
	if (ppow->eType == kPowerType_Enhancement)
	{
		if (!ppow->pchIndexTags || stricmp(ppow->pchIndexTags, "Enhancement") != 0)
		{
			ppow->pchIndexTags = (char*)allocAddString("Enhancement");
		}
	}

	ppow->bHasEffectAreaPositionOffsets =	ppow->fFrontOffset != 0.f || 
											ppow->fRightOffset != 0.f || 
											ppow->fUpOffset != 0.f;
	ppow->bHasEffectAreaOffsets =	ppow->bHasEffectAreaPositionOffsets||
									ppow->fYaw != 0.f || 
									ppow->fPitch != 0.f;
	
	

	if (g_CombatConfig.specialAttribModifiers.eaSpecialCriticalAttribs)
	{
		PowerDefDeriveAndSetCritAttribs(ppow, false);
	}
	
	// Sort after we're done
	powerdef_SortArrays(ppow);

	return true;
}


// Called immediately before PowerDefs are actually reloaded, to perform various fixups
static void PowerDefsReloadFixPre(void)
{
#if GAMESERVER || GAMECLIENT
	EntityIterator *piter = entGetIteratorAllTypesAllPartitions(0,0);
	Entity *pent = NULL;

	while(pent = EntityIteratorGetNext(piter))
	{
		if(pent->pChar)
		{
			U32 uiTime = pmTimestamp(0);
			Character *pchar = pent->pChar;
			int iPartitionIdx = entGetPartitionIdx(pent);

			character_RemoveAllMods(iPartitionIdx,pchar,false,false,kModExpirationReason_Unset,NULL);

			character_EnterStance(iPartitionIdx,pchar,NULL,NULL,true,uiTime);
			character_EnterPersistStance(iPartitionIdx,pchar,NULL,NULL,NULL,uiTime,0,false);

			character_DeactivateToggles(iPartitionIdx,pchar,pmTimestamp(0),true,false);
			character_DeactivatePassives(iPartitionIdx,pchar);

			character_DirtyInnateEquip(pchar);
			character_DirtyInnatePowers(pchar);
			character_DirtyPowerStats(pchar);
			character_DirtyInnateAccrual(pchar);
		}
	}

	EntityIteratorRelease(piter);
#endif

	powerdefs_Reload_DirtyInnateCaches();
}

// Resets all Character's Powers arrays
static void PowerDefsReloadFixPost(void)
{
	EntityIterator *piter = entGetIteratorAllTypesAllPartitions(0,0);
	Entity *pent = NULL;

	while(pent = EntityIteratorGetNext(piter))
	{
		if(pent->pChar)
		{
			int iPartitionIdx = entGetPartitionIdx(pent);
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);

			character_ResetPowersArray(iPartitionIdx, pent->pChar, pExtract);
		}
	}

	EntityIteratorRelease(piter);
}

// Don't want a one-line header file for this stupid fix
#ifdef GAMECLIENT
extern void PowersGrantDebugDialogDisable(void);
#endif

// Callback used on the client to unroll the ordered mods array, since sending the pre-unrolled ordered mods is a waste
//  of network bandwidth.
static void PowerDefReferenceReceiveCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, PowerDef *ppow, void *pUserData)
{
	switch (eType)
	{
#ifdef GAMECLIENT
	case RESEVENT_RESOURCE_PRE_MODIFIED:
		PowersGrantDebugDialogDisable();
		break;
#endif
	case RESEVENT_RESOURCE_ADDED:
	case RESEVENT_RESOURCE_MODIFIED:
		eaDestroyStruct(&ppow->ppOrderedMods,parse_AttribModDef);
		eaCopyStructs(&ppow->ppOrderedModsClient,&ppow->ppOrderedMods,parse_AttribModDef);
		eaDestroyStruct(&ppow->ppOrderedModsClient, parse_AttribModDef);
		PowerDefUnrollOrderedMods(ppow);
		break;
	}
}

static int PowerTargetResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	int i;
	PowerTarget *ptarget = pResource;
	switch(eType)
	{	
	case RESVALIDATE_POST_TEXT_READING:

		// Clear derived flags
		ptarget->bRequireSelf = false;
		ptarget->bAllowSelf = false;
		ptarget->bAllowFoe = false;
		ptarget->bAllowFriend = false;
		ptarget->bAllowNearDeath = false;
		ptarget->bSafeForSelfOnly = false;

		// All PowerTargets exclude Neutrals.  This makes it trivial to prevent everyone from affecting
		//  a neutral target.
		ptarget->eExclude = ptarget->eExclude | kTargetType_Neutral;

		// All PowerTargets either require or exclude NearDeath.
		if(!(ptarget->eRequire & kTargetType_NearDeath))
			ptarget->eExclude = ptarget->eExclude | kTargetType_NearDeath;

#define ALLOW_SELF_NR (kTargetType_Foe | kTargetType_PrimaryPet | kTargetType_Owner | kTargetType_Creator | kTargetType_Owned | kTargetType_Created)
#define ALLOW_SELF_NE (kTargetType_Self | kTargetType_Friend | kTargetType_Teammate)
#define ALLOW_FOE_NR (kTargetType_Self | kTargetType_Friend | kTargetType_Teammate | kTargetType_Owner | kTargetType_Creator | kTargetType_Owned | kTargetType_Created)
#define ALLOW_FOE_NE (kTargetType_Foe)
#define ALLOW_FRIEND_NR (kTargetType_Foe)
#define ALLOW_FRIEND_NE (kTargetType_Friend)
#define SAFE_FOR_SELFONLY (kTargetType_Self | kTargetType_Friend | kTargetType_Alive | kTargetType_Player)

		// Set basic state for derived flags
		ptarget->bRequireSelf = !!(ptarget->eRequire & kTargetType_Self);
		ptarget->bAllowSelf = !(ptarget->eRequire & ALLOW_SELF_NR) && !(ptarget->eExclude & ALLOW_SELF_NE);
		ptarget->bAllowFoe = !(ptarget->eRequire & ALLOW_FOE_NR) && !(ptarget->eExclude & ALLOW_FOE_NE);
		ptarget->bAllowFriend = !(ptarget->eRequire & ALLOW_FRIEND_NR) && !(ptarget->eExclude & ALLOW_FRIEND_NE);
		ptarget->bAllowNearDeath = !(ptarget->eExclude & kTargetType_NearDeath);
		ptarget->bSafeForSelfOnly = (ptarget->eRequire & SAFE_FOR_SELFONLY) == ptarget->eRequire && !(ptarget->eExclude & SAFE_FOR_SELFONLY);

		// Process the OrPairs similarly
		for(i=eaSize(&ptarget->ppOrPairs)-1; i>=0; i--)
		{
			ptarget->ppOrPairs[i]->eExclude = ptarget->ppOrPairs[i]->eExclude | kTargetType_Neutral;
			if(!(ptarget->ppOrPairs[i]->eRequire & kTargetType_NearDeath))
				ptarget->ppOrPairs[i]->eExclude = ptarget->ppOrPairs[i]->eExclude | kTargetType_NearDeath;

			ptarget->bRequireSelf = ptarget->bRequireSelf && (ptarget->ppOrPairs[i]->eRequire & kTargetType_Self);
			ptarget->bAllowSelf = ptarget->bAllowSelf || (!(ptarget->ppOrPairs[i]->eRequire & ALLOW_SELF_NR) && !(ptarget->ppOrPairs[i]->eExclude & ALLOW_SELF_NE));
			ptarget->bAllowFoe = ptarget->bAllowFoe || (!(ptarget->ppOrPairs[i]->eRequire & ALLOW_FOE_NR) && !(ptarget->ppOrPairs[i]->eExclude & ALLOW_FOE_NE));
			ptarget->bAllowFriend = ptarget->bAllowFriend || (!(ptarget->ppOrPairs[i]->eRequire & ALLOW_FRIEND_NR) && !(ptarget->ppOrPairs[i]->eExclude & ALLOW_FRIEND_NE));
			ptarget->bAllowNearDeath = ptarget->bAllowNearDeath || !(ptarget->ppOrPairs[i]->eExclude & kTargetType_NearDeath);
			ptarget->bSafeForSelfOnly = ptarget->bSafeForSelfOnly && (ptarget->ppOrPairs[i]->eRequire & SAFE_FOR_SELFONLY) == ptarget->ppOrPairs[i]->eRequire && !(ptarget->ppOrPairs[i]->eExclude & SAFE_FOR_SELFONLY);
		}

		if (!GET_REF(ptarget->hMsgDescription) && REF_STRING_FROM_HANDLE(ptarget->hMsgDescription)) {
			ErrorFilenamef("defs/config/PowerTargets.def", "Power target '%s' references non-existent message '%s", ptarget->pchName, REF_STRING_FROM_HANDLE(ptarget->hMsgDescription));
		}
		return VALIDATE_HANDLED;;
	}

	return VALIDATE_NOT_HANDLED;
}


static int PowerDefResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	PowerDef *pdef = pResource;
	switch(eType)
	{	
	case RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename(&pdef->pchFile, POWERS_BASE_DIR, pdef->pchGroup, pdef->pchName, POWERS_EXTENSION);
		return VALIDATE_HANDLED;

	case RESVALIDATE_POST_TEXT_READING:
		resAddFileDep("defs/config/CombatConfig.def");
		resAddFileDep("defs/config/PowerCategories.def");
		resAddFileDep("defs/config/PowerTags.def");
		resAddFileDep("defs/config/PowerAITags.def");
		resAddFileDep("defs/config/PowerTargets.def");
		resAddFileDep("defs/config/SensitivityMods.def");
		PowerDefGenerate(pdef);
		FOR_EACH_IN_EARRAY(pdef->ppMods, AttribModDef, pAttribModDef)
		{
			moddef_PostTextReadFixup(pAttribModDef, pdef);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pdef->ppOrderedMods, AttribModDef, pAttribModDef)
		{
			moddef_PostTextReadFixup(pAttribModDef, pdef);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pdef->ppOrderedModsClient, AttribModDef, pAttribModDef)
		{
			moddef_PostTextReadFixup(pAttribModDef, pdef);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pdef->ppOrderedModsClient, AttribModDef, pAttribModDef)
		{
			moddef_PostTextReadFixup(pAttribModDef, pdef);
		}
		FOR_EACH_END;
		return VALIDATE_HANDLED;
	
	case RESVALIDATE_POST_BINNING:
		powerdef_Validate(pdef,false);
		return VALIDATE_HANDLED;

	case RESVALIDATE_FINAL_LOCATION:
		PowerDefFixBackpointers(pdef);
		return VALIDATE_HANDLED;

	case RESVALIDATE_CHECK_REFERENCES:
#ifdef GAMESERVER
		if (!isProductionMode() && GetAppGlobalType() != GLOBALTYPE_GATEWAYSERVER) {
			// We only run reference checking on the gameserver in dev mode, since the other servers might not have the data
			powerdef_ValidateReferences(PARTITION_STATIC_CHECK, pdef);
		}
		return VALIDATE_HANDLED;
#endif
		break;
	}

	return VALIDATE_NOT_HANDLED;
}

static void PowerDefReloadCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	PowerDef *pdef = pResource;
	switch(eType)
	{
	case RESEVENT_RESOURCE_PRE_MODIFIED:
	case RESEVENT_RESOURCE_REMOVED:
		PowerDefsReloadFixPre();
		break;
	case RESEVENT_RESOURCE_ADDED:
	case RESEVENT_RESOURCE_MODIFIED:
		PowerDefsReloadFixPost();
		break;
	}
}

AUTO_RUN;
void RegisterPowerVersion(void)
{
	ParserBinRegisterDepValue("PowerVersion", 1);
}

// Reload PowerTargets top level callback
static void PowerTargetsReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading PowerTargets...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hPowerTargetDict);

	loadend_printf(" done (%d PowerTargets)", RefSystem_GetDictionaryNumberOfReferents(g_hPowerTargetDict));
}

// Reload PowerDefs top level callback
static void PowerDefsReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading Powers...");
	
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	
	combateval_Init(false);
	
	if(entIsServer())
	{
		PowerDefsReloadFixPre();
	}

	ParserReloadFileToDictionaryWithFlags(pchRelPath,g_hPowerDefDict,PARSER_OPTIONALFLAG);
	
	if(entIsServer())
	{
		PowerDefsReloadFixPost();
	}

	loadend_printf(" done (%d powers)", RefSystem_GetDictionaryNumberOfReferents(g_hPowerDefDict));
}

AUTO_RUN;
void RegisterPowersDict(void)
{
	RegisterNamedStaticDefine(CombatTrackerFlagEditorEnum, "CombatTrackerFlagEditor");

	// Set up reference dictionaries
	g_hPowerTargetDict = RefSystem_RegisterSelfDefiningDictionary("PowerTarget", false, parse_PowerTarget, true, true, NULL);
	g_hPowerDefDict = RefSystem_RegisterSelfDefiningDictionary("PowerDef", false, parse_PowerDef, true, true, "Power");
	
	resDictManageValidation(g_hPowerTargetDict, PowerTargetResValidateCB);
	resDictManageValidation(g_hPowerDefDict, PowerDefResValidateCB);
	resDictSetDisplayName(g_hPowerDefDict, "Power", "Powers", RESCATEGORY_DESIGN);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hPowerDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hPowerDefDict, ".msgDisplayName.Message", ".Group", ".IndexTags", ".Notes", ".IconName");
			resDictMaintainInfoIndex(g_hPowerTargetDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hPowerDefDict, 16, false, resClientRequestSendReferentCommand );
	}

	// Also register some Enums for static checking
	RegisterNamedStaticDefine(PowerModeEnum, "PowerMode");
	RegisterNamedStaticDefine(PowerCategoriesEnum, "PowerCategory");
	RegisterNamedStaticDefine(PowerTagsEnum, "PowerTag");
}


/***** Load Powers *****/

AUTO_STARTUP(PowerCategories) ASTRT_DEPS(ItemTags);
void PowerCategoriesLoad(void)
{
	int i,s;
	char *pchTemp = NULL;

	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType() && !IsLoginServer() && !IsAuctionServer() && !IsQueueServer()) {
		return;
	}

	estrStackCreateSize(&pchTemp,20);

	loadstart_printf("Loading PowerCategories...");
	ParserLoadFiles(NULL, "defs/config/PowerCategories.def", "PowerCategories.bin", PARSER_OPTIONALFLAG, parse_PowerCategories, &g_PowerCategories);
	s_pDefinePowerCategories = DefineCreate();
	s = eaSize(&g_PowerCategories.ppCategories);
	for(i=0; i<s; i++)
	{
		estrPrintf(&pchTemp,"%d", i);	// This must be the index, since PowerDefs currently index directly
		DefineAdd(s_pDefinePowerCategories,g_PowerCategories.ppCategories[i]->pchName,pchTemp);
	}
	loadend_printf(" done (%d PowerCategories).", s);

	estrDestroy(&pchTemp);
}

static void powertags_Load(void)
{
	int i,s, v;
	char *pchTemp = NULL;
	int codeAITags;

	estrStackCreateSize(&pchTemp,40);

	MakeSharedMemoryName("PowerTags", &pchTemp);

	loadstart_printf("Loading PowerTags...");

	//do non shared memory load
	//ParserLoadFilesShared(pchTemp, NULL, "defs/config/powertags.def", "PowerTags.bin", PARSER_OPTIONALFLAG, parse_PowerTagNames, &s_PowerTagNames);
	
	ParserLoadFiles(NULL, "defs/config/PowerTags.def", "PowerTags.bin", PARSER_OPTIONALFLAG, parse_PowerTagNames, &s_PowerTagNames);
	s_pDefinePowerTags = DefineCreate();
	s = eaSize(&s_PowerTagNames.ppchNames);
	for(i=0; i<s; i++)
	{
		estrPrintf(&pchTemp,"%d", i+1);
		DefineAdd(s_pDefinePowerTags,s_PowerTagNames.ppchNames[i],pchTemp);
	}
	loadend_printf(" done (%d PowerTags).", s);


	loadstart_printf("Loading power AI tags...");

	//do non shared memory load
	if(!ParserLoadFiles(NULL, "defs/config/PowerAITags.def", "PowerAITags.bin", PARSER_OPTIONALFLAG, parse_PowerAITagNames, &s_PowerAITagNames))
	{
		// Error loading, already got a pop-up
	}
	g_pDefinePowerAITags = DefineCreate();
	s = eaSize(&s_PowerAITagNames.ppchNames);
	// Make sure we haven't gone past the current limitations of the system
	codeAITags = log2(kPowerAITag_CODEMAX);
	if(s>32-codeAITags)
	{
		ErrorFilenamef("defs/config/PowerAITags.def","Too many power tags defined for current system.  %d defined, 32 supported.",s);
		s=32-log2(kPowerAITag_CODEMAX);
	}
	v = 1 << codeAITags;
	for(i=0; i<s; i++)
	{
		devassert(s_PowerAITagNames.ppchNames);

		if(StaticDefineIntGetInt(PowerAITagsEnum, s_PowerAITagNames.ppchNames[i])==-1)
		{
			v <<= 1;
			estrPrintf(&pchTemp,"%d", v);
			DefineAdd(g_pDefinePowerAITags,s_PowerAITagNames.ppchNames[i],pchTemp);
		}
		else
		{
			ErrorFilenamef("defs/config/PowerAITags.def", "Ignoring duplicate AI tag: %s", s_PowerAITagNames.ppchNames[i]);
		}
	}

	loadend_printf(" done (%d power AI tags).", s);

	estrDestroy(&pchTemp);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(PowerSourceEnum, "PowerSource_");
	ui_GenInitStaticDefineVars(PowerTagsEnum, "PowerTag_");
#endif
}

// load up additional power configs
static void PowerConfigLoad(void)
{
	loadstart_printf("Loading PowerConfig... ");

	ParserLoadFiles(NULL, "defs/config/PowerConfig.def", "PowerConfig.bin", PARSER_OPTIONALFLAG, parse_PowerConfig, &gPowerConfig);

	loadend_printf("Done loading PowerConfig");
}

static void PowerTargetsLoad(void)
{
	resLoadResourcesFromDisk(g_hPowerTargetDict, NULL, "defs/config/PowerTargets.def", "PowerTargets.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PowerTargets.def", PowerTargetsReload);
	}
}

static void PowerPurposesLoad(void)
{
	PowerPurposeNames purposeNames = {0};
	S32 i;

	g_pPowerPurposes = DefineCreate();

	loadstart_printf("Loading Power Purposes... ");

	ParserLoadFiles(NULL, "defs/config/powerpurposes.def", "powerpurposes.bin", PARSER_OPTIONALFLAG, parse_PowerPurposeNames, &purposeNames);

	// I use i+1 for the default "uncategorized" index, which is always present
	for (i = 0; i < eaSize(&purposeNames.ppchNames); i++)
		DefineAddInt(g_pPowerPurposes, purposeNames.ppchNames[i], i+1);

	if(gConf.bEnableClientComboTracker)
	{
		i++;
		comboTracker_initPowerPurposes(g_pPowerPurposes,&i);
	}

	g_iNumOfPurposes = i+1;

	StructDeInit(parse_PowerPurposeNames, &purposeNames);

	loadend_printf(" done (%d PowerPurposes).", i);
}

static void ModStackGroupsLoad(void)
{
	S32 i,s;
	S32 iOffset = kModStackGroup_CODEMAX+1;
	S32 iMaxDataDefined = (1<<ModStackGroup_NUMBITS)-iOffset;
	ModStackGroupNames names = {0};

	g_pModStackGroups = DefineCreate();

	loadstart_printf("Loading AttribModStackGroups... ");

	ParserLoadFiles(NULL, "defs/config/AttribModStackGroups.def", "AttribModStackGroups.bin", PARSER_OPTIONALFLAG, parse_ModStackGroupNames, &names);

	s = eaSize(&names.ppchNames);
	if(s>iMaxDataDefined)
	{
		ErrorFilenamef("defs/config/AttribModStackGroups.def","Too many AttribModStackGroups defined");
		s = iMaxDataDefined;
	}

	for(i=0; i<s; i++)
		DefineAddInt(g_pModStackGroups, names.ppchNames[i], i+iOffset);

	StructDeInit(parse_ModStackGroupNames, &names);

	loadend_printf(" done (%d AttribModStackGroups).", i);
}


AUTO_RUN;
void PowerEmitsRegDictionary(void)
{
	g_hPowerEmitDict = RefSystem_RegisterSelfDefiningDictionary("PowerEmit",false,parse_PowerEmit,true,true,NULL);
}

AUTO_STARTUP(PowerEmits);
void PowerEmitsLoad(void)
{
	resLoadResourcesFromDisk(g_hPowerEmitDict, NULL, "defs/config/PowerEmits.def", "PowerEmits.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
}

AUTO_STARTUP(Powers) ASTRT_DEPS(AS_Messages, PowerEmits, PowerAnimFX, SensitivityMods, PowerVars, PowerCategories, PowerSlots, PowerSubtarget, PowerModes, AS_AttribSets, AS_CharacterAttribs, CharacterClasses, AIBeforePowers, DynAnimStances);
void PowerDefsLoad(void)
{
	char *binFile = "Powers.bin";
	const char *pchMessageFail = NULL;
#ifdef APPSERVER
	if (isDevelopmentMode() && !gbMakeBinsAndExit)
		binFile = "DevAppPowers.bin";
#endif	
	
	s_pExprForever = exprCreateFromString("Forever",NULL);

	// Load CritterTags, PowerTags, FragileScaleSets, PowerTargets and PowerPurposes before actual powers
	crittertags_Load();
	powertags_Load();
	fragileScaleSets_Load();
	PowerTargetsLoad();
	PowerPurposesLoad();
	ModStackGroupsLoad();
	PowerConfigLoad();

	// Misc checks for messages
	if(pchMessageFail = StaticDefineVerifyMessages(CombatTrackerFlagEnum))
	{
		Errorf("Not all CombatTrackerNet messages were found: %s", pchMessageFail);
	}

	// Init the eval system
	combateval_Init(true);

	CombatConfig_PostCombatEvalGenerateExpressions();

	if(IsClient())
	{
		// Client resource receive callback
		resDictRegisterEventCallback(g_hPowerDefDict,PowerDefReferenceReceiveCB,NULL);
	}
	else
	{
		loadstart_printf("Loading Powers...");

		resLoadResourcesFromDisk(g_hPowerDefDict, "defs/powers/", ".powers", binFile, PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY
			
			//ABW uncomment this to try the new fast-bin-loading code for powers only
			| PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING 
			
			);

		// Special code to perform manual fixup stuff, when we need it
		if(0)
		{
			PowerDef *pdef;
			PowerDef **ppdefsFix = NULL;
			RefDictIterator iter;
			RefSystem_InitRefDictIterator(g_hPowerDefDict, &iter);
			while(pdef = (PowerDef*)RefSystem_GetNextReferentFromIterator(&iter))
			{
				// Do stuff here to fix the PowerDef, optionally push it into the ppdefsFix earray

			}

			if(ppdefsFix)
			{
				int i;
				for(i=eaSize(&ppdefsFix)-1; i>=0; i--)
				{
					pdef = ppdefsFix[i];
					if(pdef->pInheritance)
					{
						pdef->pchFile = allocAddString(pdef->pInheritance->pCurrentFile);
					}
					ParserWriteTextFileFromDictionary(pdef->pchFile,g_hPowerDefDict,0,0);
				}
				eaDestroy(&ppdefsFix);
			}

			if(0)
			{
				// Optionally fix everything
				RefSystem_InitRefDictIterator(g_hPowerDefDict, &iter);
				while(pdef = (PowerDef*)RefSystem_GetNextReferentFromIterator(&iter))
				{
					if(pdef->pInheritance)
					{
						pdef->pchFile = allocAddString(pdef->pInheritance->pCurrentFile);
					}
					ParserWriteTextFileFromDictionary(pdef->pchFile,g_hPowerDefDict,0,0);
				}
			}
		}

		// Reload callbacks
		if(isDevelopmentMode())
		{
			if(entIsServer())
			{
				resDictRegisterEventCallback(g_hPowerDefDict, PowerDefReloadCB, NULL);
			}
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "defs/powers/*.powers", PowerDefsReload);
		}

		loadend_printf(" done (%d powers).", RefSystem_GetDictionaryNumberOfReferents(g_hPowerDefDict));
	}
}


// Finds a power def based on the name
//  TODO(JW): Optimize: Make this a #define?
PowerDef *powerdef_Find(const char *pchName)
{
	return pchName ? (PowerDef*)RefSystem_ReferentFromString(g_hPowerDefDict,pchName) : NULL;
}



// Allocates a Power, sets the def reference from the name
AUTO_TRANS_HELPER_SIMPLE;
Power *power_Create(const char *pchName)
{
	NOCONST(Power) *ppow = StructCreateNoConst(parse_Power);
	SET_HANDLE_FROM_STRING(g_hPowerDefDict,pchName,ppow->hDef);
	return (Power *)ppow;
}

// Destroys the Power.  Optionally takes the Character it's on to make sure it's not left behind.
void power_Destroy(Power *ppow, Character *pchar)
{
	if(pchar)
	{
		// Make totally damn sure it's not left in their Powers array.  Somehow, though it should
		//  be totally impossible, Powers keep getting free'd without the Character's array getting
		//  correctly reset.  So we do the absolutely maximally aggressive test to get it out of there,
		//  and then we mark them to be reset.
		int i;
		while((i=eaFind(&pchar->ppPowers,ppow)) >= 0)
			eaRemove(&pchar->ppPowers,i);
		while((i=eaFind(&pchar->ppPowersLimitedUse,ppow)) >= 0)
			eaRemove(&pchar->ppPowersLimitedUse,i);
		pchar->bResetPowersArray = true;
	}
	StructDestroy(parse_Power, ppow);
}

// Copies out the Power's ID and sub-Power idx, if it's a sub-Power (-1 if it's not)
//  Set to 0,-1 if the Power is NULL
void power_GetIDAndSubIdx(Power *ppow, U32 *puiIDOut, S32 *piSubIdxOut, S16 *piLinkedSubIdxOut)
{
	if(ppow)
	{
		if(ppow->pParentPower)
		{
			int i;
			Power *ppowParent = ppow->pParentPower;
			for(i=eaSize(&ppowParent->ppSubPowers)-1; i>=0; i--)
			{
				if(ppow == ppowParent->ppSubPowers[i])
					break;
			}
			
			*puiIDOut = ppowParent->uiID;
			*piSubIdxOut = i;
			
			// get the index of the linked power
			if (ppowParent->pCombatPowerStateParent)
			{
				Power *pCombatPowerStateParent = ppowParent->pCombatPowerStateParent;
				for(i=eaSize(&pCombatPowerStateParent->ppSubCombatStatePowers)-1; i>=0; i--)
				{
					if(ppowParent == pCombatPowerStateParent->ppSubCombatStatePowers[i])
						break;
				}
				*piLinkedSubIdxOut = i;
				*puiIDOut = pCombatPowerStateParent->uiID;
			}
			else
			{
				*piLinkedSubIdxOut = -1;
			}
		}
		else if (ppow->pCombatPowerStateParent)
		{
			int i;
			Power *ppowParent = ppow->pCombatPowerStateParent;
			for(i=eaSize(&ppowParent->ppSubCombatStatePowers)-1; i>=0; i--)
			{
				if(ppow == ppowParent->ppSubCombatStatePowers[i])
					break;
			}
			*puiIDOut = ppowParent->uiID;
			*piSubIdxOut = -1;
			*piLinkedSubIdxOut = i;
		}
		else
		{
			*puiIDOut = ppow->uiID;
			*piSubIdxOut = -1;
			*piLinkedSubIdxOut = -1;
		}
	}
	else
	{
		*puiIDOut = 0;
		*piSubIdxOut = -1;
		*piLinkedSubIdxOut = -1;
	}
}


// Set the Power to be replaced by uiReplacementID when executed.  Set to 0 to turn off replacement.
//  Fails and returns false in the case that a non-0 replacement is set on a Power that already has a 
//  replacement.
int power_SetPowerReplacementID(Power *ppow, U32 uiReplacementID)
{
	if(!ppow->uiReplacementID || !uiReplacementID)
	{
		ppow->uiReplacementID = uiReplacementID;
		return true;
	}
	else
	{
		return false;
	}
}


void power_ResetCachedEnhancementFields(SA_PARAM_NN_VALID Power *ppow)
{
	ppow->fEnhancedRange = 0.f;
	ppow->fEnhancedRadius = 0.f;

	FOR_EACH_IN_EARRAY(ppow->ppSubPowers, Power, pSubPower)
	{
		pSubPower->fEnhancedRange = 0.f;
		pSubPower->fEnhancedRadius = 0.f;
	}
	FOR_EACH_END
}

// Lifetime limitations

// Returns the number of seconds of real-world time left for the given Power, or -1 if unlimited
F32 power_GetLifetimeRealLeft(Power *ppow)
{
	F32 f = -1.f;
	PowerDef *pdef = NULL;
	if(ppow->pParentPower) ppow = ppow->pParentPower;
	pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->fLifetimeReal)
	{
		U32 uiNow = timeServerSecondsSince2000();
		f = MAX(0,pdef->fLifetimeReal - (F32)(uiNow - ppow->uiTimeCreated));
	}
	return f;
}

// Returns the number of seconds of logged-in game time left for the given Power, or -1 if unlimited
F32 power_GetLifetimeGameLeft(Power *ppow)
{
	F32 f = -1.f;
	PowerDef *pdef = NULL;
	if(ppow->pParentPower) ppow = ppow->pParentPower;
	pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->fLifetimeGame)
	{
		f = MAX(0,pdef->fLifetimeGame - ppow->fLifetimeGameUsed);
	}
	return f;
}

// Returns the number of seconds of active time left for the given Power, or -1 if unlimited
F32 power_GetLifetimeUsageLeft(Power *ppow)
{
	F32 f = -1.f;
	PowerDef *pdef = NULL;
	if(ppow->pParentPower) ppow = ppow->pParentPower;
	pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->fLifetimeUsage)
	{
		f = MAX(0,pdef->fLifetimeUsage - ppow->fLifetimeUsageUsed);
	}
	return f;

}

// Returns true if the given Power should be expired due to usage limitations
//  or lifetimes
// Added bIncludeCharges so this call can be performed to only look at lifetime limiations
int power_IsExpired(Power *ppow, bool bIncludeCharges)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef
		&& pdef->bLimitedUse
		&& ((bIncludeCharges && pdef->fChargeRefillInterval <= 0.f && 0==power_GetChargesLeft(ppow))
			|| 0==power_GetLifetimeRealLeft(ppow)
			|| 0==power_GetLifetimeGameLeft(ppow)
			|| 0==power_GetLifetimeUsageLeft(ppow)))
	{
		return true;
	}
	return false;
}

// Misc utility

// Returns the power's base power, which is the parent combo power 
// and if it's a combatPowerState power, its combatPowerState parent
Power* power_GetBasePower(Power *pPower)
{
	if (pPower->pParentPower)
		pPower->pParentPower = pPower;
	if (pPower->pCombatPowerStateParent)
		 return pPower->pCombatPowerStateParent;
	return pPower;
}

F32 power_GetRange(Power *pPower, PowerDef *pDef)
{
	return (pPower) ? (pPower->fEnhancedRange + pDef->fRange) : (pDef->fRange);
}


F32 power_GetRadius(int iPartitionIdx, Character *pChar, Power *pPower, 
					PowerDef *pDef, Character *pCharTarget, PowerApplication *pApp)
{
	F32 fRadius = 0.f;
	
	if (pDef->pExprRadius)
	{
		combateval_ContextReset(kCombatEvalContext_Target);
		combateval_ContextSetupTarget(pChar, pCharTarget, pApp);
		fRadius = combateval_EvalNew(iPartitionIdx, pDef->pExprRadius, kCombatEvalContext_Target, NULL);

		if (pPower)
			fRadius += pPower->fEnhancedRadius;
	}
	
	return fRadius;
}


// Debug version of POWERLEVEL macro, makes sure the return value is >=1 and generates a detailed
//  error with callstack if it wouldn't have been.
S32 power_PowerLevelDebug(SA_PARAM_OP_VALID Power *ppow, S32 iDefault)
{
	S32 iPowerLevel = POWERLEVEL(ppow,iDefault);
	if(iPowerLevel<1)
	{
		S32 iLevel = INT_MIN, iLevelAdjustment = INT_MIN;
		if(ppow)
		{
			if(ppow->pParentPower)
			{
				iLevel = ppow->pParentPower->iLevel;
				iLevelAdjustment = ppow->pParentPower->iLevelAdjustment;
			}
			else
			{
				iLevel = ppow->iLevel;
				iLevelAdjustment = ppow->iLevelAdjustment;
			}
		}
		ErrorfForceCallstack("POWERLEVEL < 1: iLevel %d; iLevelAdjustment %d; iDefault %d",iLevel,iLevelAdjustment,iDefault);
		iPowerLevel = 1;
	}
	return iPowerLevel;
}

// Returns the default hue of the PowerDef, including its PowerAnimFX if necessary
F32 powerdef_GetHue(PowerDef *pdef)
{
	F32 fHue = pdef->fHueOverride;
	if(!fHue)
	{
		PowerAnimFX *pafx = GET_REF(pdef->hFX);
		if(pafx)
		{
			fHue = pafx->fDefaultHue;
		}
	}
	return fHue;
}

// Returns the PowerEmit of the Power, using its PowerDef and PowerAnimFX if necessary
PowerEmit *power_GetEmit(Power *ppow, Character *pchar)
{
	PowerEmit *pEmit = NULL;

	if(ppow)
	{
		// Try and get the player-set emit from the parent if there is a parent
		if(!(g_CombatConfig.bPowerCustomizationDisabled
			&& isProductionMode()
			&& pchar
			&& pchar->pEntParent
			&& entGetAccessLevel(pchar->pEntParent)<ACCESS_GM))
		{
			if(ppow->pParentPower)
			{
				pEmit = GET_REF(ppow->pParentPower->hEmit);
			}
			else
			{
				pEmit = GET_REF(ppow->hEmit);
			}
		}

		if(!pEmit)
		{
			PowerDef *pdef = GET_REF(ppow->hDef);
			if(pdef)
			{
				pEmit = GET_REF(pdef->hEmitOverride);
				if(!pEmit)
				{
					PowerAnimFX *pafx = GET_REF(pdef->hFX);
					if(pafx)
					{
						pEmit = GET_REF(pafx->hDefaultEmit);
					}
				}
			}
		}
	}

	return pEmit;
}

// Returns if the PowerDef can have its PowerEmit customized by the player
S32 powerdef_EmitCustomizable(PowerDef *pdef)
{
	S32 bRet = false;

	// Combos look to see if any child power has a customizable emit
	if(pdef->eType==kPowerType_Combo)
	{
		int i;
		for(i=eaSize(&pdef->ppOrderedCombos)-1; i>=0 && !bRet; i--)
		{
			PowerDef *pdefChild = GET_REF(pdef->ppOrderedCombos[i]->hPower);
			if(pdefChild)
			{
				bRet = powerdef_EmitCustomizable(pdefChild);
			}
		}
	}
	else
	{
		PowerAnimFX *pafx = GET_REF(pdef->hFX);
		bRet = (pafx && pafx->bEmitCustomizable);
	}

	return bRet;
}

// Returns the first AttribModDef for the PowerDef (or its children) that can
//  have its EntCreateCostume customized by the player
AttribModDef* powerdef_EntCreateCostumeCustomizable(PowerDef *pdef)
{
	int i;
	AttribModDef *pmoddef = NULL;

	// Combos look to see if any child power has a customizable EntCreateCostume
	if(pdef->eType==kPowerType_Combo)
	{
		for(i=eaSize(&pdef->ppOrderedCombos)-1; i>=0 && !pmoddef; i--)
		{
			PowerDef *pdefChild = GET_REF(pdef->ppOrderedCombos[i]->hPower);
			if(pdefChild)
			{
				pmoddef = powerdef_EntCreateCostumeCustomizable(pdefChild);
			}
		}
	}
	else
	{
		for(i=eaSize(&pdef->ppOrderedMods)-1; i>=0; i--)
		{
			if(pdef->ppOrderedMods[i]->offAttrib==kAttribType_EntCreate && pdef->ppOrderedMods[i]->pParams && ((EntCreateParams*)pdef->ppOrderedMods[i]->pParams)->bCanCustomizeCostume)
			{
				pmoddef = pdef->ppOrderedMods[i];
				break;
			}
		}
	}

	return pmoddef;
}

// Gets the teleport attribMod for a power if it has one
SA_RET_OP_VALID AttribModDef* powerdef_GetTeleportAttribMod(SA_PARAM_NN_VALID PowerDef *pPowerDef)
{
	// get the teleport attribModDef
	AttribModDef **eaDefArray = NULL;
#if GAMESERVER
	eaDefArray = pPowerDef->ppMods;
#else
	eaDefArray = pPowerDef->ppSpecialModsClient;
#endif
	
	FOR_EACH_IN_EARRAY(eaDefArray, AttribModDef, pModDef)
	{
		if (pModDef->offAttrib == kAttribType_Teleport)
		{
			return pModDef;
		}
	}
	FOR_EACH_END

	return NULL;
}

SA_RET_OP_VALID static AttribModDef* powerdef_GetWarpAttribModRecurse(	SA_PARAM_NN_VALID PowerDef *pdef, bool bUseSpecialModArray, 
																	PowerDef*** peaVisitedDefs, AttribModDef **ppLastWarpAttrib)
{
	AttribModDef** eaDefArray;
	int i;

	if (bUseSpecialModArray)
		eaDefArray = pdef->ppSpecialModsClient;
	else
		eaDefArray = pdef->ppMods;

	if (eaFind(peaVisitedDefs, pdef) >= 0)
	{
		return NULL;
	}
	eaPush(peaVisitedDefs, pdef);

	for (i = eaSize(&pdef->ppCombos)-1; i >= 0; i--)
	{
		PowerDef *pdefChild = GET_REF(pdef->ppCombos[i]->hPower);

		if (pdefChild)
		{
			AttribModDef *pdefMod = powerdef_GetWarpAttribModRecurse(pdefChild, bUseSpecialModArray, peaVisitedDefs, ppLastWarpAttrib);
			if (pdefMod)
			{
				return pdefMod;
			}
		}
	}
	for (i = eaSize(&eaDefArray)-1; i >= 0; i--)
	{
		AttribModDef *pdefMod = eaDefArray[i];

		switch (pdefMod->offAttrib)
		{
			xcase kAttribType_WarpTo:
			acase kAttribType_WarpSet:
			{
				if (*ppLastWarpAttrib)
				{	// we are currently iterating through the Warp mods
					if (*ppLastWarpAttrib == pdefMod)
					{
						*ppLastWarpAttrib = NULL;
					}
					continue;
				}

				return pdefMod;
			}
			xcase kAttribType_ApplyPower:
			{
				ApplyPowerParams* pParams = (ApplyPowerParams*)pdefMod->pParams;
				PowerDef *pdefApply = SAFE_GET_REF(pParams, hDef);
				if (pdefApply)
				{
					AttribModDef* pdefModApply = powerdef_GetWarpAttribModRecurse(pdefApply, bUseSpecialModArray, peaVisitedDefs, ppLastWarpAttrib);
					if (pdefModApply)
					{
						return pdefModApply;
					}
				}
			}
			xcase kAttribType_GrantPower:
			{
				GrantPowerParams* pParams = (GrantPowerParams*)pdefMod->pParams;
				PowerDef *pdefGrant = SAFE_GET_REF(pParams, hDef);
				if (pdefGrant)
				{
					AttribModDef* pdefModGrant = powerdef_GetWarpAttribModRecurse(pdefGrant, bUseSpecialModArray, peaVisitedDefs, ppLastWarpAttrib);
					if (pdefModGrant)
					{
						return pdefModGrant;
					}
				}
			}
		}
	}
	return NULL;
}

AttribModDef* powerdef_GetWarpAttribMod(PowerDef *pdef, bool bUseSpecialModArray, AttribModDef *pLastWarpAttrib)
{
	static PowerDef** eaVisitedDefs = NULL;
	
	eaClearFast(&eaVisitedDefs);
	
	return powerdef_GetWarpAttribModRecurse(pdef, bUseSpecialModArray, &eaVisitedDefs, &pLastWarpAttrib);
}

// If this power is enabled due to power modes, return the time remaining
// else return 0
// If bSelfOnly is true, only check attribs that target 'Self'
F32 character_GetModeEnabledTime(Character *pchar, Power *ppow, bool bSelfOnly)
{
	int *piModes = NULL;
	F32 fTimeRemaining = 0;
	PowerDef *pDef = GET_REF(ppow->hDef);
	int i;

	if(!pDef)
		return 0;

	/* Remove modes required from this list for now, only evaluate combo powers
	if(ea32Size(&pDef->piPowerModesRequired))
	{
		for(i=0;i<ea32Size(&pDef->piPowerModesRequired);i++)
			ea32PushUnique(&piModes,pDef->piPowerModesRequired[i]);
	}
	*/

	if(ppow->pParentPower)
	{
		PowerDef *pParentDef = GET_REF(ppow->pParentPower->hDef);

		if(pParentDef)
		{
			/*
			for(i=0;i<ea32Size(&pParentDef->piPowerModesRequired);i++)
				ea32PushUnique(&piModes,pParentDef->piPowerModesRequired[i]);
			*/

			if(pParentDef->eType == kPowerType_Combo)
			{
				for(i=0;i<eaSize(&pParentDef->ppOrderedCombos);i++)
				{
					if(GET_REF(pParentDef->ppOrderedCombos[i]->hPower) == pDef)
					{
						int j;
						for(j=0;j<ea32Size(&pParentDef->ppOrderedCombos[i]->piModeRequire);j++)
							ea32PushUnique(&piModes,pParentDef->ppOrderedCombos[i]->piModeRequire[j]);
					}
				}
			}
		}
	}

	for(i=0;i<eaSize(&pchar->ppModsNet);i++)
	{
		AttribModDef *pModDef;

		if(!ATTRIBMODNET_VALID(pchar->ppModsNet[i]))
			continue;
		
		pModDef = modnet_GetDef(pchar->ppModsNet[i]);

		if(pModDef && 
			(!bSelfOnly || pModDef->eTarget == kModTarget_Self) &&
			pModDef->offAttrib == kAttribType_PowerMode)
		{
			PowerModeParams *pParams = (PowerModeParams*)(pModDef->pParams);
			if(ea32Find(&piModes,pParams->iPowerMode) != -1
				&& pchar->ppModsNet[i]->uiDuration <= pchar->ppModsNet[i]->uiDurationOriginal)
			{
				fTimeRemaining = max(fTimeRemaining,(F32)pchar->ppModsNet[i]->uiDuration);
			}
		}
	}

	ea32Destroy(&piModes);

	return fTimeRemaining;
}




static bool power_CheckComboValidity(Power *ppow)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->eType==kPowerType_Combo)
	{
		if(ppow->ppSubPowers)
		{
			int iDef = eaSize(&pdef->ppOrderedCombos);
			int iCur = eaSize(&ppow->ppSubPowers);
			int i;
			if(iDef!=iCur)
			{
				return false;
			}
			for(i=0; i<iDef; i++)
			{
				PowerDef *psubdef = ppow->ppSubPowers[i] ? GET_REF(ppow->ppSubPowers[i]->hDef) : NULL;
				if(!psubdef || psubdef!=GET_REF(pdef->ppOrderedCombos[i]->hPower))
				{
					return false;
				}
			}
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		if(ppow->ppSubPowers)
		{
			return false;
		}
		else
		{
			return true;
		}
	}
}

// Checks the validity of a character's powers, and fixes them if requested
bool character_CheckPowerValidity(Character *p, bool bFix)
{
	int i;
	bool bRet = true;
	for(i=eaSize(&p->ppPowers)-1; i>=0; i--)
	{
		bool bGood = power_CheckComboValidity(p->ppPowers[i]);
		if(!bGood && bFix)
		{
			eaDestroyStruct(&p->ppPowers[i]->ppSubPowers,parse_Power);
			power_CreateSubPowers(p->ppPowers[i]);
			bGood = true;
		}
		bRet &= bGood;
	}
	return bRet;
}


// Fills in the subpowers of a power.  Won't do anything if the power already has subpowers!
void power_CreateSubPowers(Power *ppow)
{
	bool bGood = false;
	if(ppow->ppSubPowers)
	{
		bGood = power_CheckComboValidity(ppow);
		if(!bGood)
		{
			power_DestroySubPowers(ppow);
		}
	}

	if(!bGood)
	{
		int i;
		PowerDef *pdef = GET_REF(ppow->hDef);
		if(pdef)
		{
			for(i=0; i<eaSize(&pdef->ppOrderedCombos); i++)
			{
				Power *psub = power_Create(REF_STRING_FROM_HANDLE(pdef->ppOrderedCombos[i]->hPower));
				if(psub)
				{
					eaPush(&ppow->ppSubPowers,psub);
					psub->pParentPower = ppow;
				}
				else
				{
					Errorf("Power_Create did not find %s",REF_STRING_FROM_HANDLE(pdef->ppOrderedCombos[i]->hPower));
				}
			}
			if(i != eaSize(&pdef->ppOrderedCombos))
				Errorf("Power_CreateSubPowers not executed properly on power %s",pdef->pchName);
		}
	}

	power_FixSubPowers(ppow);
}

// Quick function to fix backpointers to parent power, generally only used by the client
void power_FixSubPowers(Power *ppow)
{
	int i;
	for(i=eaSize(&ppow->ppSubPowers)-1; i>=0; i--)
	{
		ppow->ppSubPowers[i]->pParentPower = ppow;
		ppow->ppSubPowers[i]->eSource = ppow->eSource;
		ppow->ppSubPowers[i]->pSourceItem = ppow->pSourceItem;
		ppow->ppSubPowers[i]->fYaw = ppow->fYaw;
	}
}

// Helper function to destroy the sub powers array on a power
void power_DestroySubPowers(Power *ppow)
{
	eaDestroyStruct(&ppow->ppSubPowers, parse_Power);
}

CooldownTimer *cooldowntimer_Create()
{
	return MP_ALLOC(CooldownTimer);
}

void cooldowntimer_Free(CooldownTimer *pTimer)
{
	MP_FREE(CooldownTimer,pTimer);
}

// Saves the cooldown data to a special persistent field
void character_SaveCooldowns(Character *pchar)
{
	int i;

	for(i=0;i<eaSize(&pchar->ppCooldownTimers);i++)
	{
		if(pchar->ppCooldownTimers[i]->fCooldown > 0.f)
		{
			if(pchar->ppCooldownTimers[i]->pchCategory)
			{
				StructFreeString(pchar->ppCooldownTimers[i]->pchCategory);
			}
			pchar->ppCooldownTimers[i]->pchCategory = StructAllocString(StaticDefineIntRevLookup(PowerCategoriesEnum, pchar->ppCooldownTimers[i]->iPowerCategory));
		}
	}
}

void character_CategorySetCooldown(int iPartitionIdx, Character *pchar, S32 iPowerCategory, F32 fTime)
{
	CooldownTimer *pTimer = character_GetCooldownTimerForCategory(pchar,iPowerCategory);

	if(!pTimer)
	{
		pTimer = MP_ALLOC(CooldownTimer);
		pTimer->iPowerCategory = iPowerCategory;
		eaPush(&pchar->ppCooldownTimers,pTimer);
	}

	pTimer->fCooldown = fTime;

#ifdef GAMESERVER
	gslSendPublicCommandf(pchar->pEntParent, 0, "PowersSetCooldownClient %d %f",iPowerCategory,fTime);

	if(pchar->pEntParent && pchar->pEntParent->erOwner)
	{
		// Find the owning Player and send the cooldown update
		Entity *pentOwner = entFromEntityRef(iPartitionIdx, pchar->pEntParent->erOwner);

		if(pentOwner && pentOwner->pPlayer && entCheckFlag(pentOwner,ENTITYFLAG_IS_PLAYER))
		{
			EntityRef erPet = entGetRef(pchar->pEntParent);
			PlayerPetInfo *pInfo = player_GetPetInfo(pentOwner->pPlayer,erPet);
			if(pInfo)
			{
				gslSendPublicCommandf(pentOwner,
					0,
					"PowersSetPetCooldownClient %d %d %f",
					erPet,
					iPowerCategory,
					fTime);
			}
		}
	}
#endif
}

void character_LoadFixCooldowns(int iPartitionIdx, Character *pchar)
{
	int i;

	for(i=eaSize(&pchar->ppCooldownTimers)-1;i>=0;i--)
	{
		CooldownTimer *pTimer = pchar->ppCooldownTimers[i];
		S32 iPowerCategory = StaticDefineIntGetInt(PowerCategoriesEnum,pTimer->pchCategory);

		if(iPowerCategory >= 0)
		{
			pTimer->iPowerCategory = iPowerCategory;
			character_CategorySetCooldown(iPartitionIdx,pchar,iPowerCategory,pTimer->fCooldown);
		}
		else
		{
			eaRemove(&pchar->ppCooldownTimers,i);
			MP_FREE(CooldownTimer,pTimer);
		}
	}
}

void power_SetCooldownDefault(int iPartitionIdx, Character *pchar, Power *ppow)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef && ea32Size(&pdef->piCategories) > 0 && (!pdef->bRechargeRequiresCombat || entIsInCombat(pchar->pEntParent)))
	{
		int i;

		for(i=0;i<ea32Size(&pdef->piCategories);i++)
		{
			PowerCategory *pCat = g_PowerCategories.ppCategories[pdef->piCategories[i]];

			if (pchar->uiTimeCombatExit == 0 && pCat->fTimeCooldownOutOfCombat >= 0.f)
			{
				character_CategorySetCooldown(iPartitionIdx, pchar, pdef->piCategories[i], pCat->fTimeCooldownOutOfCombat);
			}
			else if(pCat->fTimeCooldown)
			{
				character_CategorySetCooldown(iPartitionIdx,pchar, pdef->piCategories[i], pCat->fTimeCooldown);
			}
		}
	}
}

F32 powerdef_GetCooldown(PowerDef *pDef)
{
	int i;
	F32 fCooldown = 0.f;

	if(pDef->bRequiresCooldown == false)
		return 0.f;

	for(i=0;i<ea32Size(&pDef->piCategories);i++)
	{
		PowerCategory *pCat = g_PowerCategories.ppCategories[pDef->piCategories[i]];

		if(pCat->fTimeCooldown > fCooldown)
			fCooldown = pCat->fTimeCooldown;
	}

	return fCooldown;
}

F32 power_GetCooldown(Power *ppow)
{
	PowerDef *pDef = NULL;

	if(ppow->pParentPower) ppow = ppow->pParentPower;
	pDef = GET_REF(ppow->hDef);

	return powerdef_GetCooldown(pDef);
}

CooldownTimer *character_GetCooldownTimerForCategory(Character *pChar, S32 iPowerCategory)
{
	int i;

	for(i=0;i<eaSize(&pChar->ppCooldownTimers);i++)
	{
		if(pChar->ppCooldownTimers[i]->iPowerCategory == iPowerCategory)
		{
			return pChar->ppCooldownTimers[i];
		}
	}

	return NULL;
}

F32 character_GetCooldownFromPowerDef(Character *pChar, PowerDef *pDef)
{
	int i;
	F32 fCooldownResult = 0.f;

	if(!pDef)
		return 0.0f;

	for(i = ea32Size(&pDef->piCategories)-1; i >= 0; --i)
	{
		CooldownTimer *pCooldown = character_GetCooldownTimerForCategory(pChar,pDef->piCategories[i]);
		if(pCooldown)
		{
			CooldownRateModifier* pCooldownModifier = eaIndexedGetUsingInt(&pChar->ppSpeedCooldown, pCooldown->iPowerCategory);
			F32 fSpeed, fCooldown;

			if (!pCooldownModifier && pChar->pInnateAccrualSet)
			{
				pCooldownModifier = eaIndexedGetUsingInt(&pChar->pInnateAccrualSet->ppSpeedCooldown, pCooldown->iPowerCategory);
			}
			fSpeed = pCooldownModifier ? pCooldownModifier->fValue : pChar->pattrBasic->fSpeedCooldown;
			if (fSpeed <= 0.0f)
			{
				fSpeed = 1.0f;
			}
			fCooldown = pCooldown->fCooldown / fSpeed;
			
			if (fCooldown > fCooldownResult)
			{
				fCooldownResult = fCooldown;
			}
			break;
		}
	}

	return fCooldownResult;
}

F32 character_GetCooldownFromPower(Character *pChar, Power *ppow)
{
	PowerDef *pDef = NULL;

	if(ppow->pParentPower) ppow = ppow->pParentPower;
	pDef = GET_REF(ppow->hDef);

	return character_GetCooldownFromPowerDef(pChar,pDef);
}

// Recharge

// Utility function for setting a specific recharge on a specific Power ID (0 means all Powers), with
//  proper notification to the client.
void character_PowerSetRecharge(int iPartitionIdx, Character *pchar, U32 uiID, F32 fTime)
{
	if(uiID)
	{
		Power *ppow = character_FindPowerByID(pchar,uiID);
		if(ppow)
		{
			power_SetRecharge(iPartitionIdx,pchar,ppow,fTime);
		}
	}
	else
	{
		int i;
		for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
		{
			power_SetRecharge(iPartitionIdx,pchar,pchar->ppPowers[i],fTime);
		}
	}

#ifdef GAMESERVER
	ClientCmd_PowerSetRechargeClient(pchar->pEntParent,uiID,fTime);
#endif
}

// Sets the Power to recharge in its default recharge time.  Also takes a flag to indicate if the
//  Power happened to Miss all of its targets, which may trigger slightly different behavior thanks
//  to the bRechargeRequiresHit flag.
void power_SetRechargeDefault(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID Power *ppow, S32 bMissedAllTargets)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef)
	{
		if((!bMissedAllTargets || !pdef->bRechargeRequiresHit) && 
			(!pdef->bRechargeRequiresCombat || entIsInCombat(pchar->pEntParent)) &&
			(!pdef->bChargesSetCooldownWhenEmpty || power_GetChargesUsed(ppow)==0))
			power_SetRecharge(iPartitionIdx, pchar,ppow,pdef->bRechargeDisabled ? POWERS_FOREVER : pdef->fTimeRecharge);

#ifdef GAMESERVER
		// Due to possible misprediction on the client, if these flags are set the server sends down
		//  the final recharge state.  This is kinda crummy and we probably need to rethink how
		//  client prediction of recharge works, but it should be effective for now.
		if(pdef->bRechargeRequiresHit || pdef->bRechargeRequiresCombat || pdef->bChargesSetCooldownWhenEmpty)
		{
			if (ppow->pParentPower)
				ppow = ppow->pParentPower;
			if (ppow->pCombatPowerStateParent)
				ppow = ppow->pCombatPowerStateParent;
			ClientCmd_PowerSetRechargeClient(pchar->pEntParent,ppow->uiID,ppow->fTimeRecharge);
		}
#endif
	}
}

// Sets the Power to recharge in the given time
void power_SetRecharge(int iPartitionIdx, Character *pchar, Power *ppow, F32 fTime)
{
	int i;
	PowerDef* pDef = NULL;
	
	// Recharge the parent if one exists
	if(ppow->pParentPower)
	{
		ppow = ppow->pParentPower;

		pDef = GET_REF(ppow->hDef);

		// this is probably abusing the flag, bCheckComboBeforeToggle, but this mechanic creates problems when toggles are within combos
		// the toggle can go off while in the middle of another activation, so when the current activation finishes it overrides
		// the recharge time that was set by the toggle.
		// I am not completely satisfied with this, but it will do for now until requirements change
		// if the incoming recharge time is greater than the current that is set, and it's on recharge then ignore it.
		if (pDef && pDef->bCheckComboBeforeToggle)
		{
			if (ppow->fTimeRecharge > 0 && fTime < ppow->fTimeRechargeBase)
			{
				PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, pchar->pEntParent, 
									"Recharge: Ignoring recharge due to larger recharge placed on power (%s)\n",
									POWERNAME(ppow));
				return;
			}
		}
	}

	// See if it's in the list
	i = power_FindInRefs(ppow,&pchar->ppPowerRefRecharge);

	if(fTime > 0.0f)
	{
		// Want to cause some recharge
		if(i<0)
		{
			eaPush(&pchar->ppPowerRefRecharge,power_CreateRef(ppow));
			
			// Cancel queued and overflow activations if they are the Power we're recharging
			if(pchar->pPowActQueued && pchar->pPowActQueued->ref.uiID==ppow->uiID)
			{
				character_ActQueuedCancel(iPartitionIdx,pchar,NULL,0);
			}
			if(pchar->pPowActOverflow && pchar->pPowActOverflow->ref.uiID==ppow->uiID)
			{
				character_ActOverflowCancel(iPartitionIdx,pchar,NULL,0);
			}
		}

		if (ppow->fTimeRecharge <= 0.0f || fTime > ppow->fTimeRechargeBase)
			ppow->fTimeRechargeBase = fTime;

		ppow->fTimeRecharge = fTime;
		PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, pchar->pEntParent, "Recharge: Placed in recharge for %f (%s)\n",fTime,POWERNAME(ppow));
	}
	else
	{
		// Done recharging
		if(i>=0)
		{
			powerref_Destroy(pchar->ppPowerRefRecharge[i]);
			eaRemoveFast(&pchar->ppPowerRefRecharge,i);
			if(character_CombatEventTrack(pchar,kCombatEvent_PowerRecharged))
				character_CombatEventTrackComplex(pchar,kCombatEvent_PowerRecharged,NULL,GET_REF(ppow->hDef),NULL,0,0,NULL);
			PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, pchar->pEntParent, "Recharge: Removed from recharge (%s)\n",POWERNAME(ppow));
		}
#ifdef GAMESERVER
		{//If we're clearing the cooldown of a ChargesSetCooldownWhenEmpty power, it should reset the charges too.
			if (!pDef)
				pDef = GET_REF(ppow->hDef);
			if (pDef->bChargesSetCooldownWhenEmpty && pDef->fChargeRefillInterval <= 0.f && power_GetChargesUsed(ppow) > 0)
			{
				power_SetChargesUsed(pchar, ppow, pDef, 0);
				
			}
		}
#endif
		PowersDebugPrintEnt(EPowerDebugFlags_RECHARGE, pchar->pEntParent, "Recharge: Recharge cleared (%s)\n", POWERNAME(ppow));
		
		ppow->fTimeRechargeBase = 0.f;

		ppow->fTimeRecharge = 0.0f;
	}
	
	// for linked powers, we're going to apply the cooldown to all the linked powers
	if (eaSize(&ppow->ppSubCombatStatePowers))
	{
		FOR_EACH_IN_EARRAY(ppow->ppSubCombatStatePowers, Power, pLinkedPower)
		{
			if (pLinkedPower->fTimeRecharge != ppow->fTimeRecharge)
			{
				power_SetRecharge(iPartitionIdx, pchar, pLinkedPower, fTime);
			}
		}
		FOR_EACH_END
	}
	else if (ppow->pCombatPowerStateParent && ppow->pCombatPowerStateParent->fTimeRecharge != ppow->fTimeRecharge)
	{
		power_SetRecharge(iPartitionIdx, pchar, ppow->pCombatPowerStateParent, fTime);
	}


#ifdef GAMESERVER
	if(pchar->pEntParent && pchar->pEntParent->erOwner)
	{
		// Find the owning Player, find this pet's PetPowerState (to make sure the player cares), and send the update
		Entity *pentOwner = entFromEntityRef(iPartitionIdx, pchar->pEntParent->erOwner);
		if (!pDef)
			pDef = GET_REF(ppow->hDef);
		if(pentOwner && pentOwner->pPlayer && entCheckFlag(pentOwner,ENTITYFLAG_IS_PLAYER) && pDef)
		{
			EntityRef erPet = entGetRef(pchar->pEntParent);
			PetPowerState *pState = player_GetPetPowerState(pentOwner->pPlayer,erPet,pDef);
			if(pState)
			{
				ClientCmd_PowerPetSetRechargeClient(pentOwner,erPet,pDef->pchName,fTime);
			}
		}
	}
#endif
}

// Gets the PowerDef's default recharge time.  If the PowerDef is a Combo it
//  returns the largest default recharge time of its children.  If recharge is
//  disabled it returns POWERS_FOREVER.
F32 powerdef_GetRechargeDefault(PowerDef *pdef)
{
	S32 bDisabled = false;
	F32 f = 0.f;

	if(pdef->eType==kPowerType_Combo)
	{
		int i;
		for(i=eaSize(&pdef->ppCombos)-1; i>=0; i--)
		{
			PowerDef *pdefChild = GET_REF(pdef->ppCombos[i]->hPower);
			if(pdefChild && (pdefChild->fTimeRecharge > f || pdefChild->bRechargeDisabled))
			{
				f = pdefChild->fTimeRecharge;
				bDisabled |= pdefChild->bRechargeDisabled;
			}
		}
	}
	else
	{
		f = pdef->fTimeRecharge;
		bDisabled = pdef->bRechargeDisabled;
	}

	return bDisabled ? POWERS_FOREVER : f;
}

// Gets the Power's current recharge time remaining
F32 power_GetRecharge(Power *ppow)
{
	// Find recharge of the parent if one exists
	if(ppow->pParentPower) ppow = ppow->pParentPower;

	// Return the recharge time
	// TODO(JW): Prediction: There might be ways to improve client prediction of this
	return ppow->fTimeRecharge;
}

// returns the effective basic aspect of kAttribType_SpeedRecharge for the given power.
// this will check if the powerDef wants to ignore kAttribType_SpeedRecharge 
F32 power_GetSpeedRecharge(int iPartitionIdx, Character *pchar, Power *ppow, PowerDef *pDef)
{
	if (!pDef)
	{
		pDef = GET_REF(ppow->hDef);
	}
	if (pDef && pDef->piAttribIgnore)
	{
		if (powerdef_IgnoresAttrib(pDef, kAttribType_SpeedRecharge))
			return 1.f;
	}
	return character_PowerBasicAttrib(iPartitionIdx,pchar,ppow,kAttribType_SpeedRecharge,0);
}

// Gets the Power's current recharge time remaining, given the Character's recharge speed with respect to that Power
F32 character_GetPowerRechargeEffective(int iPartitionIdx, Character *pchar, Power *ppow)
{
	F32 fRecharge = power_GetRecharge(ppow);
	const F32 fRechargeSpeedEps = 0.01f;

	if(fRecharge>0)
	{
		F32 fSpeed = power_GetSpeedRecharge(iPartitionIdx,pchar,ppow,NULL);
		// If fSpeed is zero or really small, then show the actual recharge value
		if(fSpeed < fRechargeSpeedEps)
		{
			fSpeed = 1;
		}
		fRecharge /= fSpeed;
	}
	return fRecharge;
}

F32 power_GetRechargeBase(Power *ppow)
{
	// Find recharge of the parent if one exists
	if(ppow->pParentPower && ppow->pParentPower->fTimeRechargeBase > 0.f) ppow = ppow->pParentPower;

	if (ppow->fTimeRechargeBase <= 0.0f)
	{						
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
		if(pdef)
		{
			return powerdef_GetRechargeDefault(pdef);
		}

		return -1.f;
	}

	// Return the base recharge time
	return ppow->fTimeRechargeBase;
}

// Gets the Power's current recharge time remaining, given the Character's recharge speed with respect to that Power
F32 character_GetPowerRechargeBaseEffective(int iPartitionIdx, Character *pchar, Power *ppow)
{
	F32 fRecharge = power_GetRechargeBase(ppow);
	const F32 fRechargeSpeedEps = 0.01f;

	if(fRecharge>0)
	{
		F32 fSpeed = power_GetSpeedRecharge(iPartitionIdx,pchar,ppow,NULL);
		// If fSpeed is zero or really small, then show the actual recharge value
		if(fSpeed<fRechargeSpeedEps)
		{
			fSpeed = 1;
		}
		fRecharge /= fSpeed;
	}
	return fRecharge;
}

// Charge

// Gets the time remaining until the power gains another charge
F32 power_GetChargeRefillTime(Power *ppow)
{
	// Find the parent power if one exists
	if(ppow->pParentPower) ppow = ppow->pParentPower;

	// Return the recharge time
	return ppow->fTimeChargeRefill;
}

// Returns the number of charges used for the given Power
int power_GetChargesUsed(Power *ppow)
{
	if (ppow->eSource != kPowerSource_Item)
	{
		return ppow->iChargesUsed;
	}
	else
	{	// currently iChargesUsedTransact is only used by powers from Items
		return ppow->iChargesUsedTransact;
	}
}

// Returns the number of charges left for the given Power, or -1 if unlimited
int power_GetChargesLeft(Power *ppow)
{
	int i = -1;
	PowerDef *pdef = NULL;
	
	if(ppow->pParentPower) 
		ppow = ppow->pParentPower;

	pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->iCharges)
	{
		S32 iChargesLeft = pdef->iCharges - power_GetChargesUsed(ppow);
		i = MAX(0, iChargesLeft);
	}
	return i;
}

// Sets the Power's used charges, and performs all the logic that goes along with changing the number of charges used
void power_SetChargesUsed(Character *pchar, Power *ppow, PowerDef *pDef, int iChargesUsed)
{
	if (!pDef)
	{
		if(ppow->pParentPower)
		{
			pDef = GET_REF(ppow->pParentPower->hDef);
		}
		else
		{
			pDef = GET_REF(ppow->hDef);
		}
	}

	if (!pDef)
		return;

	iChargesUsed = CLAMP(iChargesUsed, 0, pDef->iCharges);
	
	if (iChargesUsed != power_GetChargesUsed(ppow))
	{
		// the charges used changed

		if (ppow->eSource == kPowerSource_Item)
		{	// this power uses iChargesUsedTransact
			// we need to do a transaction to modify it 
			// these types of charges have more limited functionality, so exit after we begin this transaction
#ifdef GAMESERVER
			TransactionReturnVal * pRetVal = LoggedTransactions_CreateManagedReturnVal("trPlayer_PowerUseChargeItem", NULL, NULL);
			AutoTrans_trPlayer_PowerSetChargesItem(pRetVal, GetAppGlobalType(), entGetType(pchar->pEntParent), 
													entGetContainerID(pchar->pEntParent), ppow->uiID, iChargesUsed);
#endif
			return;
		}


		if (iChargesUsed < power_GetChargesUsed(ppow))
		{
			if (pDef && character_CombatEventTrack(pchar, kCombatEvent_PowerChargeGained))
			{
				character_CombatEventTrackComplex(pchar, kCombatEvent_PowerChargeGained, NULL, pDef, NULL, 0, 0, NULL);
			}
			PowersDebugPrintEnt(EPowerDebugFlags_POWERS, pchar->pEntParent, "Charge gained: %s\n",POWERNAME(ppow));
		}

		ppow->iChargesUsed = iChargesUsed;
			
		{
			// set the dirty bit
			Power *pBasePower = power_GetBasePower(ppow);
			character_DirtyPower(pchar, pBasePower);
		}
						
		// check if this power has a refill interval, and if it does see if we need to update the power's fTimeChargeRefill
		if (pDef && pDef->fChargeRefillInterval > 0.f)
		{
			if (ppow->iChargesUsed)
			{	// we currently have charges used, so make sure we are currently refilling
				if (ppow->fTimeChargeRefill <= 0.f)
				{			
					ppow->fTimeChargeRefill = pDef->fChargeRefillInterval;
					if (power_FindInRefs(ppow, &pchar->ppPowerRefChargeRefill) < 0)
					{
						// Add to the countdown tracking list
						eaPush(&pchar->ppPowerRefChargeRefill, power_CreateRef(ppow));
					}
					
#ifdef GAMESERVER
					if (entIsPlayer(pchar->pEntParent))
					{
						U32 uiID = 0;
						S32 iIdxSub = 0;
						S16 iLinkedSub = 0;
						power_GetIDAndSubIdx(ppow, &uiID, &iIdxSub, &iLinkedSub);
						ClientCmd_PowerSetChargeRefillClient(pchar->pEntParent, uiID, iIdxSub, iLinkedSub);
					}
#endif
				}
			}
			else if (ppow->fTimeChargeRefill > 0)
			{
				// we don't have anymore charges used, but we have TimeChargeRefill set, clear the cooldown and update the 
				// client and our ppPowerRefChargeRefill list
				int idx;
				
				ppow->fTimeChargeRefill = 0.f;

				idx = power_FindInRefs(ppow, &pchar->ppPowerRefChargeRefill);
				if (idx >= 0)
				{
					PowerRef* pRef = pchar->ppPowerRefChargeRefill[idx];
					powerref_Destroy(pRef);
					eaRemoveFast(&pchar->ppPowerRefChargeRefill, idx);
				}
				
#ifdef GAMESERVER
				if (entIsPlayer(pchar->pEntParent))
				{
					U32 uiID = 0;
					S32 iIdxSub = 0;
					S16 iLinkedSub = 0;
					power_GetIDAndSubIdx(ppow, &uiID, &iIdxSub, &iLinkedSub);
					ClientCmd_PowerSetChargeRefillClient(pchar->pEntParent, uiID, iIdxSub, iLinkedSub);
				}
#endif
			}
		}
		
	}
}


void character_PowerUseCharge(Character *pchar, Power *ppow)
{
#ifdef GAMESERVER
	U32 uiID = 0;
	PowerDef *pdef = NULL;
	Entity *e = pchar->pEntParent;

	if(ppow->pParentPower)
	{
		pdef = GET_REF(ppow->pParentPower->hDef);
		uiID = ppow->pParentPower->uiID;
	}
	else
	{
		pdef = GET_REF(ppow->hDef);
		uiID = ppow->uiID;
	}

	if(pdef && pdef->iCharges > 0)
	{
		bool bHadTimeChargeRefill = (ppow->fTimeChargeRefill > 0);
		
		if (!pdef->bChargesSetCooldownWhenEmpty)
		{
			power_SetChargesUsed(pchar, ppow, pdef, power_GetChargesUsed(ppow) + 1);
		}
		else
		{
			if ((power_GetChargesUsed(ppow) + 1) < pdef->iCharges)
				power_SetChargesUsed(pchar, ppow, pdef, 0);
			else if (pdef->bChargesSetCooldownWhenEmpty)
				power_SetChargesUsed(pchar, ppow, pdef, 0);
		}

	}
#endif
}

#ifdef GAMESERVER

// Restarts the timers for any power which needs to refill its charges
void character_LoadTimeChargeRefillFixup(int iPartitionIdx, Character *pChar)
{
	FOR_EACH_IN_EARRAY_FORWARDS(pChar->ppPowers, Power, pPower)
	{
		PowerDef *pDef;
		if (pPower->pParentPower)
		{
			pPower = pPower->pParentPower;
		}
		pDef = GET_REF(pPower->hDef);

		if (pDef &&
			pDef->iCharges > 0 &&
			pDef->fChargeRefillInterval > 0.f &&
			power_GetChargesUsed(pPower) > 0)
		{
			if (power_FindInRefs(pPower, &pChar->ppPowerRefChargeRefill) < 0)
			{
				// Add to the countdown tracking list
				eaPush(&pChar->ppPowerRefChargeRefill, power_CreateRef(pPower));
			}

			// Set the refill timer
			pPower->fTimeChargeRefill = pDef->fChargeRefillInterval;

			ClientCmd_PowerSetChargeRefillClient(pChar->pEntParent, pPower->uiID, 0, 0);
		}
	}
	FOR_EACH_END
}

#endif

// Active

// Set the Power's active status.  Also sets the active status of the parent.
void power_SetActive(SA_PARAM_NN_VALID Power *ppow, int bActive)
{
	ppow->bActive = !!bActive;
	if(ppow->pParentPower)
	{
		if (bActive || (eaSize(&ppow->pParentPower->ppSubPowers) == 0))
			ppow->pParentPower->bActive = !!bActive;
		else
		{
			int i;
			ppow->pParentPower->bActive = 0;
			for (i=0;i<eaSize(&ppow->pParentPower->ppSubPowers); i++)
			{
				if (ppow->pParentPower->ppSubPowers[i]->bActive)
				{
					ppow->pParentPower->bActive = 1;
					break;
				}
			}
		}
	}
}




// Called by the system to get the costume effects from the powers system
void powers_GetPowerCostumeDataToShow(Entity *pEnt, CostumeDisplayData ***peaData, CostumeDisplayData ***peaMountData)
{
	Character *p = pEnt->pChar;
	bool bEnableFlourish = false;
	bool bStrafeOverride = false;
	F32 fFlourishHoldTimer = 0.f;
	F32 fTurnRateScale = 0.f;

	// Costume Changes
	if(p && eaSize(&p->ppCostumeChanges)>0)
	{
		SetCostumeParams *pParams;
		int i;

		eaQSort(p->ppCostumeChanges,attrib_sortfunc);

		for(i=eaSize(&p->ppCostumeChanges)-1; i>=0; --i)
		{
			pParams = (SetCostumeParams *)p->ppCostumeChanges[i]->pDef->pParams;

			if(pParams)
			{
				fTurnRateScale = pParams->fTurnRateScale;

				if (!pParams->bMount)
				{
					if (pParams->bCopyCostumeFromSourceEnt)
					{
						Entity* pSrcEnt = entFromEntityRef(entGetPartitionIdx(pEnt), p->ppCostumeChanges[i]->erSource);
						if (pEnt)
						{
							NOCONST(PlayerCostume) *pCostumeCopy = StructCloneDeConst(parse_PlayerCostume, costumeEntity_GetEffectiveCostume(pSrcEnt));
							if (pCostumeCopy)
							{
								CostumeDisplayData *pData = calloc(1, sizeof(CostumeDisplayData));
								pData->eMode = pParams->eMode;
								pData->iPriority = pParams->iPriority;
								if (pData->iPriority == 0) {
									if (pParams->eMode == kCostumeDisplayMode_Overlay ||
										pParams->eMode == kCostumeDisplayMode_Overlay_Always)
									{
										pData->iPriority = DEFAULT_POWER_OVERLAY_PRIORITY;
									}
									else
									{
										pData->iPriority = DEFAULT_POWER_REPLACE_PRIORITY;
									}
								}
								eaPush(&pData->eaCostumesOwned, (PlayerCostume*)pCostumeCopy);
								eaPush(peaData, pData);
							}
						}
					}
					else if (GET_REF(pParams->hCostume))
					{
						CostumeDisplayData *pData = calloc(1, sizeof(CostumeDisplayData));
						pData->eMode = pParams->eMode;
						pData->iPriority = pParams->iPriority;
						if (pData->iPriority == 0) {
							if (pParams->eMode == kCostumeDisplayMode_Overlay || 
								pParams->eMode == kCostumeDisplayMode_Overlay_Always)
							{
								pData->iPriority = DEFAULT_POWER_OVERLAY_PRIORITY;
							}
							else
							{
								pData->iPriority = DEFAULT_POWER_REPLACE_PRIORITY;
							}
						}
						eaPush(&pData->eaCostumes, GET_REF(pParams->hCostume));
						eaPush(peaData, pData);
					}
					else if (pParams->pcBoneGroup && *pParams->pcBoneGroup && pEnt->pSaved)
					{
						NOCONST(PlayerCostume) *pBaseCostume = CONTAINER_NOCONST(PlayerCostume, pEnt->pSaved->costumeData.eaCostumeSlots[pEnt->pSaved->costumeData.iActiveCostume]->pCostume);
						NOCONST(PlayerCostume) *pCostume = CONTAINER_NOCONST(PlayerCostume, costumeTailor_MakeCostumeOverlayEx(CONTAINER_RECONST(PlayerCostume, pBaseCostume), GET_REF(pParams->hSkeleton), pParams->pcBoneGroup, true, true));
						if (pCostume)
						{
							CostumeDisplayData *pData = calloc(1, sizeof(CostumeDisplayData));
							pData->eMode = pParams->eMode;
							pData->iPriority = pParams->iPriority;
							if (pData->iPriority == 0) {
								if (pParams->eMode == kCostumeDisplayMode_Overlay ||
									pParams->eMode == kCostumeDisplayMode_Overlay_Always)
								{
									pData->iPriority = DEFAULT_POWER_OVERLAY_PRIORITY;
								}
								else
								{
									pData->iPriority = DEFAULT_POWER_REPLACE_PRIORITY;
								}
							}
							eaPush(&pData->eaCostumesOwned, (PlayerCostume*)pCostume);
							eaPush(peaData, pData);
						}
					}
				}
				else //pParams->bMount
				{
					const PlayerCostume *pCostume =  GET_REF(pParams->hCostume);
					if (pCostume)
					{
						const PCSkeletonDef *pSkelDef = GET_REF(pCostume->hSkeleton);
						if (peaMountData)
						{
							CostumeDisplayData *pData = calloc(1, sizeof(CostumeDisplayData));
							pData->eMode = pParams->eMode;
							pData->iPriority = pParams->iPriority;
							pData->fMountScaleOverride = pParams->fMountScaleOverride;
							if (pData->iPriority == 0) {
								if (pParams->eMode == kCostumeDisplayMode_Overlay || 
									pParams->eMode == kCostumeDisplayMode_Overlay_Always)
								{
									pData->iPriority = DEFAULT_POWER_OVERLAY_PRIORITY;
								}
								else
								{
									pData->iPriority = DEFAULT_POWER_REPLACE_PRIORITY;
								}
							}
							eaPush(&pData->eaCostumes, GET_REF(pParams->hCostume));
							eaPush(peaMountData, pData);
						}

						if (pSkelDef) 
						{
							bEnableFlourish = true;
							bStrafeOverride = true;
							fFlourishHoldTimer = pSkelDef->fFlourishTimer;
						}
					}
				}
			}
		}
	}

	// Costume Modifies
	if(p && eaSize(&p->ppCostumeModifies)>0)
	{
		CostumeDisplayData *pData;
		ModifyCostumeParams *pParams;
		int i;

		eaQSort(p->ppCostumeModifies,attrib_sortfunc);

		for(i=eaSize(&p->ppCostumeModifies)-1; i>=0; --i)
		{
			pParams = (ModifyCostumeParams *)p->ppCostumeModifies[i]->pDef->pParams;

			if (pParams) {
				pData = calloc(1, sizeof(CostumeDisplayData));
				pData->eType = kCostumeDisplayType_Value_Change;
				pData->iPriority = pParams->iPriority;

				pData->eValueArea = pParams->eArea;
				pData->eValueMode = pParams->eMode;
				pData->fValue = pParams->fValue;
				pData->fMinValue = pParams->fMinValue;
				pData->fMaxValue = pParams->fMaxValue;

				if (pData->iPriority == 0) {
					pData->iPriority = DEFAULT_POWER_MODIFY_PRIORITY;
				}
				eaPush(peaData, pData);
			}
		}
	}

#if GAMESERVER || GAMECLIENT
	pmSetFlourishData(	pEnt, bEnableFlourish, fFlourishHoldTimer);
	// todo: the power's costume system is always stomping these- mrSurfaceSetTurnRateScale, mrSurfaceSetIsStrafingOverride
	// once these are needed by other systems this will need to handle multiple requests and priority.
	mrSurfaceSetTurnRateScale(pEnt->mm.mrSurface, fTurnRateScale, fTurnRateScale);
	mrSurfaceSetIsStrafingOverride(pEnt->mm.mrSurface, false, bStrafeOverride);
#endif

}


// Returns Group.Name
char *powerdef_NameFull(PowerDef *pdef)
{
	static char s_achName[MAX_POWER_NAMEFULL_LEN] = {0};
	if(pdef->pchGroup)
	{
		strcpy(s_achName,pdef->pchGroup);
		strcat(s_achName,".");
		strcat(s_achName,pdef->pchName);
	}
	else
	{
		strcpy(s_achName,pdef->pchName);
	}
	return s_achName;
}

bool character_HasRequiredAnyTacticalModes(Character *pchar, PowerDef *pdef)
{
	int i;
	for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
	{
		PowerCategory *pPowerCategory = g_PowerCategories.ppCategories[pdef->piCategories[i]];

		if(pPowerCategory->eMatchTacticalMode)
		{
			if (pPowerCategory->eMatchTacticalMode & kPowerTacticalMovementMode_Aim && pchar->bIsCrouching)
				return true;

			if (pPowerCategory->eMatchTacticalMode & kPowerTacticalMovementMode_Roll && pchar->bIsRolling)
				return true;

			return false;
		}
	}

	return true;
}



PowerDef *power_doesActivate(char *pchPowerName)
{
	PowerDef *pdef = powerdef_Find(pchPowerName);
	return pdef ? (power_DefDoesActivate(pdef) ? pdef : NULL) : NULL;
}

bool power_DefDoesActivate(const PowerDef *pDef)
{
	if (!pDef)
		return false;

	switch (pDef->eType)
	{
	case kPowerType_Combo:
	case kPowerType_Click:
	case kPowerType_Maintained:
	case kPowerType_Toggle:
		return true;
	default:
		return false;
	}
}

// ********************** Common functions for looking up information on a power **************************

S32 PowersUI_GetNumPowersWithCatAndPurposeInternal(Entity *pEntity, char *pcCategory, char *pcPurpose)
{
	if(pEntity)
	{
		S32 cat = -1, purpose = -1;

		if(pcPurpose && *pcPurpose)
		{
			purpose = StaticDefineIntGetInt(PowerPurposeEnum, pcPurpose);
		}
		if(pcCategory && *pcCategory)
		{
			cat = StaticDefineIntGetInt(PowerCategoriesEnum, pcCategory);
			return ent_GetNumPowersWithCatAndPurpose(pEntity, cat, purpose);
		}
	}

	return 0;
}

// Used to check for a specific category and or purpose 
bool powers_IsPowerCatAndPurpose(const Power *pPower, S32 category, PowerPurpose purpose)
{
	if(pPower)
	{
		PowerDef *pPowerDef = GET_REF(pPower->hDef);
		if(pPowerDef)
		{
			bool bFound = false;
			if(purpose < 0 || pPowerDef->ePurpose < 0 || pPowerDef->ePurpose == purpose)
			{
				if(category < 0)
				{
					return true;
				}
				else
				{
					S32 i;
					for(i = 0; i < eaiSize(&pPowerDef->piCategories);++i)
					{
						if(pPowerDef->piCategories[i] == category)
						{
							return true;
						}
					}
				}
			}
		}
	}
	
	return false;
}

void character_FindAllPowersInItems(Entity *pEntity, Power ***pppPowerOut)
{
	S32 i;
	if (!pEntity->pInventoryV2)
	{
		return;
	}
	for (i = 0; i < eaSize(&pEntity->pInventoryV2->ppInventoryBags); ++i)
	{
		InventoryBag* pBag = pEntity->pInventoryV2->ppInventoryBags[i];
		bool bEquipBag = (invbag_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag))!=0;
		BagIterator *iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pBag));
		for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item *pItem  = (Item*)bagiterator_GetItem(iter);
			ItemDef *pItemDef = bagiterator_GetDef(iter);
			int iNumPowers = item_GetNumItemPowerDefs(pItem, true);
			int iMinItemLevel = item_GetMinLevel(pItem);
			S32 k;

			if (!(invbag_flags(pBag) & InvBagFlag_SpecialBag) && (!pItemDef || !(pItemDef->flags & kItemDefFlag_CanUseUnequipped) ||
				!itemdef_VerifyUsageRestrictions(PARTITION_CLIENT, pEntity, pItemDef, iMinItemLevel, NULL, -1)))
			{
				continue;
			}
			for (k = 0; k < iNumPowers; k++)
			{
				Power* pPower = item_GetPower(pItem, k);
				ItemPowerDef* pItemPowerDef = item_GetItemPowerDef(pItem, k);
				PowerDef* pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;
				if (!pPower || !pPowerDef)
				{
					continue;
				}
				if (pBag->BagID == InvBagIDs_ItemSet) // Special logic for item sets
				{
					U32 uSetCount = SAFE_MEMBER(pItem, uSetCount);
					ItemPowerDefRef* pItemPowerDefRef = pItemDef ? eaGet(&pItemDef->ppItemPowerDefRefs, k) : NULL;
					if (!pItemPowerDefRef || uSetCount < pItemPowerDefRef->uiSetMin)
					{
						continue;
					}
				}
				else if (!bEquipBag && !item_ItemPowerActive(pEntity, pBag, pItem, k))
				{
					// if the bag isn't an equip bag, and the power is not usable (item_ItemPowerActive
					// does not actually check to see if it's active it seems)
					continue;
				}
				eaPush(pppPowerOut, pPower);
			}
		}
		bagiterator_Destroy(iter);
	}
	
}

void character_FindAllPowersInPowerTrees(Character *pChar, Power ***pppPowerOut)
{
	S32 i;
	for(i=eaSize(&pChar->ppPowerTrees)-1; i>=0; i--)
	{
		S32 j;
		PowerTree *ptree = pChar->ppPowerTrees[i];
		for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
		{
			S32 k;
			PTNode *pnode = ptree->ppNodes[j];

			if(eaSize(&pnode->ppPowers) == 0)
				continue;

			if(pnode->bEscrow)
				continue;

			if(pnode->ppPowers[0])
			{
				PowerDef *pdef = GET_REF(pnode->ppPowers[0]->hDef);

				//If the first node is an enhancement, then all the nodes are enhancements, and you should
				//only add the last power
				if(pdef && pdef->eType == kPowerType_Enhancement)
				{
					pdef = GET_REF(pnode->ppPowers[eaSize(&pnode->ppPowers)-1]->hDef);

					if(pdef)
					{
						eaPush(pppPowerOut,pnode->ppPowers[eaSize(&pnode->ppPowers)-1]);
					}
					continue;
				}
			}

			for(k=eaSize(&pnode->ppPowers)-1; k>=0; k--)
			{
				PowerDef *pdef = pnode->ppPowers[k] ? GET_REF(pnode->ppPowers[k]->hDef) : NULL;

				if(!pdef || !pnode->ppPowers[k])
					continue;

				eaPush(pppPowerOut,pnode->ppPowers[k]);

				// If the Power we just added isn't an Enhancement, we're done with this node
				if(!pdef || pdef->eType!=kPowerType_Enhancement)
				{
					break;
				}
			}
		}
	}

}

void character_FindAllNodesInPowerTrees(Character *pChar, PTNode ***pppNodeOut, bool bUseEscrow)
{
	S32 i;
	for(i=eaSize(&pChar->ppPowerTrees)-1; i>=0; i--)
	{
		S32 j;
		PowerTree *ptree = pChar->ppPowerTrees[i];
		for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
		{
			PTNode *pnode = ptree->ppNodes[j];

			if(pnode->bEscrow && bUseEscrow)
			{
				continue;
			}

			eaPush(pppNodeOut, pnode);
		}
	}
}

// How many powers (in power tree) does this ent have with this purpose and category?
S32 ent_GetNumPowersWithCatAndPurpose(Entity *pEnt, S32 category, PowerPurpose purpose)
{
	if(pEnt && pEnt->pChar)
	{
		Power **ppPowers = NULL;
		S32 i, iCount = 0;
		
		character_FindAllPowersInPowerTrees(pEnt->pChar, &ppPowers);
		
		for(i = 0; i < eaSize(&ppPowers); ++i)
		{
			if(powers_IsPowerCatAndPurpose(ppPowers[i], category, purpose))
			{
				++iCount;
			}
		}
		eaDestroy(&ppPowers);
		return iCount;
	}
	
	return 0;
}

// Compares two powerdefs by their power purpose
S32 ComparePowerDefsByPurpose(const PowerDef** a, const PowerDef** b)
{
	if (a && *a && b && *b)
	{
		if ((*a)->ePurpose < (*b)->ePurpose)
		{
			return -1;
		}
		else if ((*a)->ePurpose > (*b)->ePurpose)
		{
			return 1;
		}
	}
	return 0;
}

bool ent_canTakePower(Entity *pEntity, PowerDef *pDef)
{
	CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
	if(pDef->powerProp.bPropPower)
	{
		if(pClass && ea32Find(&pDef->powerProp.eCharacterTypes,pClass->eType) > -1)
			return true;
	}

	return false;
}

S32 GetHighestCategorySortID(void)
{
	int maxSort = 0;
	int i;
	for (i = 0; i < eaSize(&g_PowerCategories.ppCategories); i++)
	{
		if (g_PowerCategories.ppCategories[i]->iSortGroup > maxSort)
		{
			maxSort = g_PowerCategories.ppCategories[i]->iSortGroup;
		}
	}
	return maxSort;
}

bool powerdef_ignorePitch(PowerDef *pdef)
{
	int i;
	for(i=eaiSize(&pdef->piCategories)-1; i>=0; i--)
	{
		if(g_PowerCategories.ppCategories[pdef->piCategories[i]]->bIgnoreTargetPitch)
		{
			return true;
		}
	}

	return false;
}

// returns true if the powerdef has one of the given power categories
bool powerdef_hasCategory(PowerDef *pdef, S32 *piPowerCategories)
{
	// todo: we might want to log how often this is occurring per character 
	// and if it exceeds a threshold a client is either hacking or having severe issues
	S32 xx, iNumCategories = eaiSize(&piPowerCategories);

	// we have a blessed list of category types we don't care if the client mispredicts on
	for (xx = 0; xx < iNumCategories; ++xx)
	{
		if (eaiFind(&pdef->piCategories, piPowerCategories[xx]) >= 0)
		{
			return true;
		}
	}

	return false;
}


int powerddef_ShouldDelayTargeting(PowerDef *pDef)
{
	return g_CombatConfig.bDelayAllPowerTargetingOnQueuedDefault || pDef->bDelayTargetingOnQueuedActivation;
}

bool character_IsQueuingDisabledForPower(Character *pChar, PowerDef *pDef)
{
	S32 *peIncludeCategories = g_CombatConfig.peDisableQueuingIncludeCategories;
	S32 *peExcludeCategories = g_CombatConfig.peDisableQueuingExcludeCategories;
	S32 i;

	if (!g_CombatConfig.bDisablePowerQueuing && !pChar->bDisablePowerQueuing)
	{
		return false;
	}
	for (i = eaiSize(&peExcludeCategories)-1; i >= 0; i--)
	{
		if (eaiFind(&pDef->piCategories, peExcludeCategories[i]) >= 0)
		{
			return false;
		}
	}
	if (eaiSize(&peIncludeCategories))
	{
		for (i = eaiSize(&peIncludeCategories)-1; i >= 0; i--)
		{
			if (eaiFind(&pDef->piCategories, peIncludeCategories[i]) >= 0)
			{
				break;
			}
		}
		if (i < 0)
		{
			return false;
		}
	}
	return true;
}

bool character_PowerRequiresValidTarget(Character *pchar, PowerDef *pdef)
{
	if (pdef && pdef->eRequireValidTarget == kPowerRequireValidTarget_Always)
	{
		return true;
	}
	if (!pdef || pdef->eRequireValidTarget == kPowerRequireValidTarget_Default)
	{
		if (pchar && pchar->bRequireValidTarget)
		{
			return true;
		}
		if (g_CombatConfig.bRequireValidTarget)
		{
			return true;
		}
	}
	return false;
}

// This caches the power's expression driven value for area effect powers so it can later be passed to FX
// Note: this doesn't have all the information for kCombatEvalContext_Target
// but for the main cases kEffectArea_Cone, kEffectArea_Cylinder and kEffectArea_Sphere is usually a number 
// and not based on anything in the application or target
void power_RefreshCachedAreaOfEffectExprValue(int iPartitionIdx, Character *pChar, Power *pPower, PowerDef *pPowerDef)
{
	if (pChar && pPower && pPowerDef)
	{
		pPower->fCachedAreaOfEffectExprValue = 0.f;

		switch(pPowerDef->eEffectArea)
		{
			xcase kEffectArea_Cone:
				if(pPowerDef->pExprArc)
				{
					combateval_ContextReset(kCombatEvalContext_Target);
					combateval_ContextSetupTarget(pChar, NULL, NULL);
					pPower->fCachedAreaOfEffectExprValue = combateval_EvalNew(iPartitionIdx, pPowerDef->pExprArc, kCombatEvalContext_Target,NULL);
				}
			xcase kEffectArea_Cylinder:
			acase kEffectArea_Sphere:

				pPower->fCachedAreaOfEffectExprValue = power_GetRadius(iPartitionIdx, pChar, pPower, pPowerDef, NULL, NULL);
				/*
				//if(pPowerDef->pExprRadius)
				{
					combateval_ContextReset(kCombatEvalContext_Target);
					combateval_ContextSetupTarget(pChar, NULL, NULL);
				//	pPower->fCachedAreaOfEffectExprValue = combateval_EvalNew(iPartitionIdx, pPowerDef->pExprRadius,kCombatEvalContext_Target,NULL);
				}
				*/
		}
	}

}

#include "AutoGen/Powers_h_ast.c"
#include "AutoGen/PowersEnums_h_ast.c"
