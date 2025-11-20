/***************************************************************************
*     Copyright (c) 2003-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterClass.h"
#include "dynfxinfo.h"
#include "entCritter.h"
#include "Entity.h"
#include "Character_target.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "EntityInteraction.h"
#include "error.h"
#include "estring.h"
#include "expression.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "inventoryCommon.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "interaction_common.h"
#include "OfficerCommon.h"
#include "oldencounter_common.h"
#include "PowerTree.h"
#include "rand.h"
#include "ResourceManager.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "TextParserInheritance.h"
#include "wlGroupPropertyStructs.h"
#include "InteriorCommon.h"
#include "encounter_common.h"
#include "CharacterAttribute.h"
#include "Character.h"
#include "mission_common.h"
#include "PowerHelpers.h"
#include "PowerAnimFX.h"


#include "nemesis_common.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "AutoGen/Entity_h_ast.h"
#endif

#if GAMESERVER
	#include "aiConfig.h"
	#include "aiPowers.h"
	#include "gslCritter.h"
	#include "gslEntity.h"
	#include "gslPowerTransactions.h"
	#include "PowersMovement.h"
#endif

#if APPSERVER
	#include "utilitiesLib.h"
#endif

#include "entEnums_h_ast.h"
#include "itemEnums_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/rewardCommon_h_ast.h"
#include "AutoGen/CharacterAttribsMinimal_h_ast.h"
#include "AutoGen/Powers_h_ast.h"
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


#define CRITTER_BASE_DIR "defs/critters"
#define CRITTER_EXTENSION "critter"

#define CRITTERGROUP_BASE_DIR "defs/crittergroups"
#define CRITTERGROUP_EXTENSION "crittergroup"

#define CRITTEROVERRIDES_BASE_DIR "defs/critteroverrides"
#define CRITTEROVERRIDES_EXTENSION "critteroverride"

DefineContext *s_pDefineCritterTags = NULL;
StaticDefineInt CritterTagsEnum[] =
{
	DEFINE_INT

	DEFINE_EMBEDDYNAMIC_INT(s_pDefineCritterTags)

	DEFINE_END
};

typedef struct OldEncounter OldEncounter;
typedef struct GameEncounter GameEncounter;

//-----------------------------------------------------------------------------
// Globals & Statics
//-----------------------------------------------------------------------------

DictionaryHandle g_hCritterDefDict;
DictionaryHandle g_hCritterCombatDict;
DictionaryHandle g_hCritterFactionDict;
DictionaryHandle g_hCritterGroupDict;
DictionaryHandle g_hCritterOverrideDict;

CritterRankDef **g_eaCritterRankDefs;
CritterSubRankDef **g_eaCritterSubRankDefs;
const char **g_eaCritterRankNames;
const char **g_eaCritterSubRankNames;
const char *g_pcCritterDefaultRank;
const char *g_pcCritterDefaultSubRank;

CritterTags g_CritterTags;

//--------------------------------------------------------------------------------------
// Critter Rank and SubRank
//--------------------------------------------------------------------------------------

bool critterRankExists(const char *pcRank)
{
	pcRank = allocAddString(pcRank);
	return (eaFind(&g_eaCritterRankNames, pcRank) >= 0);
}


// Assumed passed in ranks are pooled strings
bool critterRankEquals(const char *pcRank1, const char *pcRank2)
{
	if (!pcRank1) {
		pcRank1 = g_pcCritterDefaultRank;
	}
	if (!pcRank2) {
		pcRank2 = g_pcCritterDefaultRank;
	}
	return (pcRank1 == pcRank2);
}


// Assumes passed in rank is a pooled string
bool critterRankIgnoresFallingDamage(const char *pcRank)
{
	int i;

	if (!pcRank) {
		pcRank = g_pcCritterDefaultRank;
	}

	for(i=eaSize(&g_eaCritterRankDefs)-1; i>=0; --i) {
		CritterRankDef *pDef = g_eaCritterRankDefs[i];
		if (pcRank == pDef->pcName) {
			return pDef->bIgnoresFallingDamage;
		}
	}

	return false;
}


// Caller is responsible for freeing the names
void critterRankGetNameCopies(char ***peaNames)
{
	int i;

	for(i=0; i<eaSize(&g_eaCritterRankNames); ++i) {
		eaPush(peaNames, strdup(g_eaCritterRankNames[i]));
	}
}


// Assumes rank and subrank passed in are pooled strings already
float critterRankGetDifficultyValue(const char *pcRank, const char *pcSubRank, int iLevelModifier)
{
	int i,j;

	if (!pcRank) {
		pcRank = g_pcCritterDefaultRank;
	}
	if (!pcSubRank) {
		pcSubRank = g_pcCritterDefaultSubRank;
	}

	for(i=eaSize(&g_eaCritterRankDefs)-1; i>=0; --i) {
		CritterRankDef *pDef = g_eaCritterRankDefs[i];
		if (pDef->pcName == pcRank) {
			for(j=eaSize(&pDef->eaDifficulty)-1; j>=0; --j) {
				if ((gConf.bManualSubRank && !pcSubRank) || pDef->eaDifficulty[j]->pcSubRank == pcSubRank) {
					if (iLevelModifier && pDef->fLevelDifficultyMod)
					{
						return pDef->eaDifficulty[j]->fDifficulty * 
							powf(1.0f + (pDef->fLevelDifficultyMod/pDef->eaDifficulty[j]->fDifficulty) * SIGN(iLevelModifier),ABS(iLevelModifier));
						// Apply the level modifier repeatedly as opposed to linearly
					}
					else
					{
						return pDef->eaDifficulty[j]->fDifficulty;
					}
				}
			}
			return -1.0;
		}
	}

	// This is returned in case of an unexpected situation
	return -1.0;
}


// Assumes the rank and subrank are already pooled strings
int critterRankGetConModifier(const char *pcRank, const char *pcSubRank)
{
	int iModifier = 0;
	int i;

	if (!pcRank) {
		pcRank = g_pcCritterDefaultRank;
	}
	if (!pcSubRank) {
		pcSubRank = g_pcCritterDefaultSubRank;
	}

	for(i=eaSize(&g_eaCritterRankDefs)-1; i>=0; --i) {
		CritterRankDef *pDef = g_eaCritterRankDefs[i];
		if (pDef->pcName == pcRank) {
			iModifier += pDef->iConModifier;
		}
	}
	for(i=eaSize(&g_eaCritterSubRankDefs)-1; i>=0; --i) {
		CritterSubRankDef *pDef = g_eaCritterSubRankDefs[i];
		if (pDef->pcName == pcSubRank) {
			iModifier += pDef->iConModifier;
		}
	}

	return iModifier;
}


// Assumes the rank passed in is a pooled string
int critterRankGetOrder(const char *pcRank)
{
	int i;

	if (!pcRank) {
		pcRank = g_pcCritterDefaultRank;
	}

	for(i=eaSize(&g_eaCritterRankDefs)-1; i>=0; --i) {
		CritterRankDef *pDef = g_eaCritterRankDefs[i];
		if (pDef->pcName == pcRank) {
			return pDef->iOrder;
		}
	}

	return -1;
}


const char *critterRankGetMissionDefault(void)
{
	int i;

	for(i=eaSize(&g_eaCritterRankDefs)-1; i>=0; --i) {
		CritterRankDef *pDef = g_eaCritterRankDefs[i];
		if (pDef->bIsMissionRewardDefault) {
			return pDef->pcName;
		}
	}

	return g_pcCritterDefaultRank;
}


bool critterSubRankExists(const char *pcSubRank)
{
	pcSubRank = allocAddString(pcSubRank);
	return (eaFind(&g_eaCritterSubRankNames, pcSubRank) >= 0);
}


// Works if string is not a pooled string
CritterSubRankDef *critterSubRankGetByName(const char *pcSubRank)
{
	int i;

	for(i=eaSize(&g_eaCritterSubRankDefs)-1; i>=0; --i) {
		CritterSubRankDef *pDef = g_eaCritterSubRankDefs[i];
		if (pcSubRank == pDef->pcName) {
			return pDef;
		}
	}
	for(i=eaSize(&g_eaCritterSubRankDefs)-1; i>=0; --i) {
		CritterSubRankDef *pDef = g_eaCritterSubRankDefs[i];
		if (stricmp(pcSubRank, pDef->pcName) == 0) {
			return pDef;
		}
	}

	return NULL;
}


// Assumes sub-rank is already a pooled string
const char *critterSubRankGetClassInfoType(const char *pcSubRank)
{
	int i;

	for(i=eaSize(&g_eaCritterSubRankDefs)-1; i>=0; --i) {
		CritterSubRankDef *pDef = g_eaCritterSubRankDefs[i];
		if (pcSubRank == pDef->pcName) {
			return pDef->pcClassInfoType;
		}
	}

	return NULL;
}


// Assumes the rank passed in is a pooled string
int critterSubRankGetOrder(const char *pcSubRank)
{
	int i;

	if (!pcSubRank) {
		pcSubRank = g_pcCritterDefaultSubRank;
	}

	for(i=eaSize(&g_eaCritterSubRankDefs)-1; i>=0; --i) {
		CritterSubRankDef *pDef = g_eaCritterSubRankDefs[i];
		if (pDef->pcName == pcSubRank) {
			return pDef->iOrder;
		}
	}

	return -1;
}

// Caller is responsible for freeing the names
void critterSubRankGetNameCopies(char ***peaNames)
{
	int i;

	for(i=0; i<eaSize(&g_eaCritterSubRankNames); ++i) {
		eaPush(peaNames, strdup(g_eaCritterSubRankNames[i]));
	}
}


//--------------------------------------------------------------------------------------
// Util stuff?
//--------------------------------------------------------------------------------------

CritterLore** critter_GetApplicableLore(CritterDef* pDef, AttribType eAttrib, S32 DC)
{
	CritterLore** eaReturnList = 0;
	int i;
	if (!pDef) return NULL;
	for(i = 0; i < eaSize(&pDef->ppCritterLoreEntries); i++)
	{
		if (pDef->ppCritterLoreEntries[i] &&
			eAttrib == pDef->ppCritterLoreEntries[i]->eAttrib &&
			DC >= pDef->ppCritterLoreEntries[i]->DC)
		{
			eaPush(&eaReturnList, pDef->ppCritterLoreEntries[i]);
		}
	}
	return eaReturnList;
}

bool critter_OwnsPower(CritterDef * pDef, char * pchPower)
{
	int i;

	// base

	for(i=eaSize(&pDef->ppPowerConfigs)-1; i>=0; i--)
	{
		if( REF_STRING_FROM_HANDLE(pDef->ppPowerConfigs[i]->hPower) && stricmp(REF_STRING_FROM_HANDLE(pDef->ppPowerConfigs[i]->hPower), pchPower) == 0)
			return 1;
	}

	return 0;
}


void addPowerTag( char * name )
{
	int i;

	for(i=eaSize(&g_CritterTags.critterTags)-1; i>=0; i--)
	{
		if( stricmp(g_CritterTags.critterTags[i], name)==0)
			return;
	}
	eaPush(&g_CritterTags.critterTags, (char*)allocAddString(name));
}

static bool CritterHasValidAttribKeyBlock(CritterDef *pDef)
{
	if(pDef->iKeyBlock)
	{
		if(pDef->pInheritance)
		{
			CritterDef *pDefParent = critter_DefGetByName(pDef->pInheritance->pParentName);
			if(pDefParent && pDef->iKeyBlock!=pDefParent->iKeyBlock)
				return true;
		}
		else
			return true;
	}

	return false;
}

void CritterGenerateNewAttribKeyBlock(CritterDef *pDef)
{
	DictionaryEArrayStruct *pArray = resDictGetEArrayStruct(g_hCritterDefDict);
	CritterDef **ppDefs = (CritterDef**)pArray->ppReferents;
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
				if(ppDefs[i]->iKeyBlock==iKey)
					break;
			}
			bValid = (i < 0);
		}
	}

	// Set the key block
	pDef->iKeyBlock = iKey;

	// Add it to the inheritance structure if this is a child of another critter
	if(pDef->pInheritance)
	{
		StructInherit_CreateFieldOverride(parse_CritterDef, pDef, ".KeyBlock");
	}
}

int critterdef_GetNextPowerConfigKey(CritterDef *pDef, int iPreviousKey)
{
	int iOffset = 0;
	if(!CritterHasValidAttribKeyBlock(pDef))
		CritterGenerateNewAttribKeyBlock(pDef);
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pDef->iKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pDef->iKeyBlock + iOffset;
			for(j=eaSize(&pDef->ppPowerConfigs)-1; j>=0; j--)
			{
				if(pDef->ppPowerConfigs[j]->iKey==iKey)	break; 
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pDef->iKeyBlock + iOffset;
}

int critterdef_GetNextCostumeKey(CritterDef *pDef, int iPreviousKey)
{
	int iOffset = 0;
	if(!CritterHasValidAttribKeyBlock(pDef))
		CritterGenerateNewAttribKeyBlock(pDef);
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pDef->iKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pDef->iKeyBlock + iOffset;
			for(j=eaSize(&pDef->ppCostume)-1; j>=0; j--)
			{
				if(pDef->ppCostume[j]->iKey==iKey)	break; 
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pDef->iKeyBlock + iOffset;
}


int critterdef_GetNextVarKey(CritterDef *pDef, int iPreviousKey)
{
	int iOffset = 0;
	if(!CritterHasValidAttribKeyBlock(pDef))
		CritterGenerateNewAttribKeyBlock(pDef);
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pDef->iKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pDef->iKeyBlock + iOffset;
			for(j=eaSize(&pDef->ppCritterVars)-1; j>=0; j--)
			{
				if(pDef->ppCritterVars[j]->iKey==iKey)	break; 
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pDef->iKeyBlock + iOffset;
}


int critterdef_GetNextLoreKey(CritterDef *pDef, int iPreviousKey)
{
	int iOffset = 0;
	if(!CritterHasValidAttribKeyBlock(pDef))
		CritterGenerateNewAttribKeyBlock(pDef);
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pDef->iKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pDef->iKeyBlock + iOffset;
			for(j=eaSize(&pDef->ppCritterLoreEntries)-1; j>=0; j--)
			{
				if(pDef->ppCritterLoreEntries[j]->iKey==iKey)	break; 
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pDef->iKeyBlock + iOffset;
}


int critterdef_GetNextItemKey(CritterDef *pDef, int iPreviousKey)
{
	int iOffset = 0;
	if(!CritterHasValidAttribKeyBlock(pDef))
		CritterGenerateNewAttribKeyBlock(pDef);
	else
	{
		int j;
		if(iPreviousKey) iOffset = (iPreviousKey - pDef->iKeyBlock) + 1;
		for(; iOffset<ATTRIB_KEY_BLOCK_SIZE; iOffset++)
		{
			int iKey = pDef->iKeyBlock + iOffset;
			for(j=eaSize(&pDef->ppCritterItems)-1; j>=0; j--)
			{
				if(pDef->ppCritterItems[j]->iKey==iKey)	break; 
			}
			if(j<0) break;
		}
	}

	assert(iOffset<ATTRIB_KEY_BLOCK_SIZE);
	return pDef->iKeyBlock + iOffset;
}

bool critterdef_HasOldInteractProps(CritterDef *pDef)
{
	if (pDef->oldInteractProps.interactCond ||
		pDef->oldInteractProps.interactSuccessCond ||
		pDef->oldInteractProps.interactAction ||
		(pDef->oldInteractProps.interactGameActions && eaSize(&pDef->oldInteractProps.interactGameActions->eaActions)) ||
		GET_REF(pDef->oldInteractProps.interactText.hMessage) ||
		GET_REF(pDef->oldInteractProps.interactFailedText.hMessage)
		) {
		return true;
	}
	return false;
}

// Implemented to mirror the way interaction entries are gotten from encounters 
// and to allow for ease of adding multiple entries on a critterdef if we ever want to do that.
int critter_GetNumInteractionEntries(Critter* pCritter)
{
	return pCritter ? critterdef_GetNumInteractionEntries(GET_REF(pCritter->critterDef)) : 0;
}

int critterdef_GetNumInteractionEntries(CritterDef* pDef)
{
	InteractionDef *pIntDef = pDef ? GET_REF(pDef->hInteractionDef) : NULL;
	return pIntDef && pIntDef->pEntry ? 1 : 0;
}

WorldInteractionPropertyEntry *critter_GetInteractionEntry(Critter* pCritter, int iInteractionIndex)
{
	return pCritter ? critterdef_GetInteractionEntry(GET_REF(pCritter->critterDef), iInteractionIndex) : NULL;
}

WorldInteractionPropertyEntry *critterdef_GetInteractionEntry(CritterDef* pDef, int iInteractionIndex)
{
	WorldInteractionPropertyEntry *pEntry = NULL;

	if(pDef && iInteractionIndex >= 0 && iInteractionIndex < critterdef_GetNumInteractionEntries(pDef)) {
		InteractionDef *pIntDef = pDef ? GET_REF(pDef->hInteractionDef) : NULL;
		pEntry = pIntDef ? pIntDef->pEntry : NULL;
	}
	return pEntry;
}

//-----------------------------------------------------------------------------
// Substitute Critter defs
//-----------------------------------------------------------------------------
// Substitute defs are temporary, ref-counted critter defs used to override normal critter defs.
// They need to be added to the dictionary when they're first used, and removed from the dictionary
// when they're no longer being used

// Add a substitute critter def to the dictionary, or increment its refcount it if already exists
/* These defs aren't currently used, but might still be useful at some point
CritterDef* critterdef_AddSubstituteDefToDictionary(const char* defName, SubstituteCritterDef* substDef, Entity* playerEnt)
{
	char* newName = NULL;
	CritterDef* newDef;

	if(playerEnt)
		estrPrintf(&newName, "%s(playerID%d)", defName, playerEnt->myContainerID);
	else
	{
		// This may be dangerous if more than one map is using the same dictionary
		estrPrintf(&newName, "%s(fromMap)", defName);
	}

	// Look up this def in the dictionary.  If it doesn't exist, create it and add it
	if(NULL == (newDef = RefSystem_ReferentFromString(g_hCritterDefDict, newName)))
	{
		newDef = StructAlloc(parse_CritterDef);
		ParserReadText(substDef->parsedDef, parse_CritterDef, newDef, 0);

		if(!newDef)	// StructClone should always return a struct, but the compiler doesn't believe this.
			return NULL;

		newDef->pchName = (char*)allocAddString(newName);
		newDef->pchFileName = (char*)allocAddString(NO_FILE);
		newDef->refCount = 1;

		RefSystem_AddReferent(g_hCritterDefDict, newDef->pchName, newDef);
	}
	else
	{
		newDef->refCount++;
	}

	return newDef;
}

// Given a critter that is being destroyed, if its def is a substitute def,
// decrement the def's ref count and remove it from the dictionary.  Will do nothing if the def
// is a normal def.
void critterdef_TryRemoveSubstituteFromDictionary(Critter* deletedCritter)
{
	CritterDef* critterDef = GET_REF(deletedCritter->critterDef);

	// Critterdefs with nonzero refcounts are substitute defs, and should be deleted when there are no
	// more references to them.
	// Normal (non-substitute) critter defs have zero refcounts all the time
	if(critterDef && critterDef->refCount)
	{
		critterDef->refCount--;
		if(critterDef->refCount <= 0)
		{
			RefSystem_RemoveReferent(critterDef, false);
			StructDestroy(parse_CritterDef, critterDef);
		}
	}
}
*/



//-----------------------------------------------------------------------------
// Validation
//-----------------------------------------------------------------------------


bool critter_FactionVerify(CritterFaction* faction)
{
	int i;

	if( !resIsValidName(faction->pchName) )
	{
		ErrorFilenamef( faction->pchFileName, "Critter faction name is illegal: '%s'", faction->pchName );
		return 0;
	}

	for( i=eaSize(&faction->relationship)-1; i>=0; i-- )
	{
		CritterFaction *pFaction = GET_REF(faction->relationship[i]->hFactionRef);

		if( !pFaction )
		{
			ErrorFilenamef( faction->pchFileName, "Critter Faction %s is trying to use unnamed/undefined relationship", 
				faction->pchName );
			return 0;
		}
	}

	return 1;
}


bool critter_GroupVerify(CritterGroup* group)
{
	if( !resIsValidName(group->pchName) )
	{
		ErrorFilenamef( group->pchFileName, "Critter group name is illegal: '%s'", group->pchName );
		return 0;
	}
//	if (!GET_REF(group->hFaction) && REF_STRING_FROM_HANDLE(group->hFaction))
//	{
//		ErrorFilenamef( group->pchFileName, "Critter group '%s' refers to non-existent faction: '%s'", group->pchName, REF_STRING_FROM_HANDLE(group->hFaction) );
//		return 0;
//	}

	return 1;
}


bool critterOverride_Validate(CritterOverrideDef *def)
{
	const char *pchTempFileName;

	if( !resIsValidName(def->pchName) )
	{
		ErrorFilenamef( def->pchFileName, "Critter override name is illegal: '%s'", def->pchName );
		return 0;
	}

	if( !resIsValidScope(def->pchScope) )
	{
		ErrorFilenamef( def->pchFileName, "Critter override scope is illegal: '%s'", def->pchScope );
		return 0;
	}

	pchTempFileName = def->pchFileName;
	if (resFixPooledFilename(&pchTempFileName, CRITTEROVERRIDES_BASE_DIR, def->pchScope, def->pchName, CRITTEROVERRIDES_EXTENSION)) {
		if (IsServer()) {
			ErrorFilenamef( def->pchFileName, "Critter override filename does not match name '%s' scope '%s'", def->pchName, def->pchScope);
		}
	}



	return 1;
}


//-----------------------------------------------------------------------------
// Fill Ref dicts
//-----------------------------------------------------------------------------

bool killreward_Validate(RewardTable *rw,char const *fn)
{
	RewardPickupType eType = rw ? rw->PickupType : kRewardPickupType_None;
	if(!rw)
		return true;

	if (eType == kRewardPickupType_FromOrigin)
		eType = reward_GetPickupTypeFromRewardOrigin(rw, RewardContextType_EntKill);

	switch (eType)
	{
	case kRewardPickupType_None:
	case kRewardPickupType_Interact:
	case kRewardPickupType_Rollover:
	case kRewardPickupType_Direct:
		// okay
		break;
	case kRewardPickupType_Clickable:
	case kRewardPickupType_Choose:
	default:
		// for missions/contacts only
		ErrorFilenamef(fn, "reward table %s has invalid pickup type %s", rw->pchName, StaticDefineIntRevLookup(RewardPickupTypeEnum, rw->PickupType));
		return false;
	}
	STATIC_INFUNC_ASSERT(kRewardPickupType_Count == 7);
	return true;
}


static void critterdef_Validate(CritterDef *critter)
{
	if(!critter)
		return;

	killreward_Validate(GET_REF(critter->hRewardTable),critter->pchFileName);
	killreward_Validate(GET_REF(critter->hAddRewardTable),critter->pchFileName);	
}

static void critterdef_Process(CritterDef *def)
{
	FOR_EACH_IN_EARRAY(def->ppCritterVars, CritterVar, var)
	{
		if(var->var.eType==WVAR_STRING || var->var.eType==WVAR_ANIMATION)
			worldVariableCountStrings(&var->var);
	}
	FOR_EACH_END
}


static int critterResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CritterDef *pCritter, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pCritter->pchFileName, CRITTER_BASE_DIR, pCritter->pchScope, pCritter->pchName, CRITTER_EXTENSION);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
#if defined(GAMESERVER)
		critter_CritterDefPostTextRead(pCritter);
#elif !defined(APPSERVER)
		assertmsg(0, "Only a gameserver should be loading this");
#endif
	return VALIDATE_HANDLED;
		
	xcase RESVALIDATE_POST_BINNING: // Called on all objects in dictionary after any load/reload of this dictionary
		if(IsServer())
			critterdef_Process(pCritter);
		critter_DefValidate(pCritter,IsServer());
		return VALIDATE_HANDLED;
	
	xcase RESVALIDATE_CHECK_REFERENCES: // Called on all objects in dictionary after any load/reload of this dictionary
		critterdef_Validate(pCritter);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

static int critterOverrideResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CritterOverrideDef *pOverride, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pOverride->pchFileName, CRITTEROVERRIDES_BASE_DIR, pOverride->pchScope, pOverride->pchName, CRITTEROVERRIDES_EXTENSION);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_BINNING: // Called at end of load/reload
		critterOverride_Validate(pOverride);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static int critterFactionResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CritterFaction *pFaction, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pFaction->pchFileName, CRITTER_BASE_DIR, NULL, pFaction->pchName, "faction");
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		critter_FactionVerify(pFaction);
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static void crittergroup_Validate(CritterGroup *grp)
{
	if(!grp)
		return;

	killreward_Validate(GET_REF(grp->hRewardTable),grp->pchFileName);
	killreward_Validate(GET_REF(grp->hAddRewardTable),grp->pchFileName);
}

static void crittergroup_Process(CritterGroup *grp)
{
	FOR_EACH_IN_EARRAY(grp->ppCritterVars, CritterVar, var)
	{
		if(var->var.eType==WVAR_STRING || var->var.eType==WVAR_ANIMATION)
			worldVariableCountStrings(&var->var);
	}
	FOR_EACH_END
}

static int critterGroupResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, CritterGroup *pGroup, U32 userID)
{
	switch (eType)
	{	
	xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
		resFixPooledFilename(&pGroup->pchFileName, CRITTERGROUP_BASE_DIR, pGroup->pchScope, pGroup->pchName, CRITTERGROUP_EXTENSION);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_CHECK_REFERENCES: // Called on all objects in dictionary after any load/reload of this dictionary
		crittergroup_Validate(pGroup);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_TEXT_READING: // Called after load/reload but before binning
		critter_GroupVerify(pGroup);
		return VALIDATE_HANDLED;

	xcase RESVALIDATE_POST_BINNING:
		if(IsServer())
		{
			crittergroup_Process(pGroup);
			return VALIDATE_HANDLED;
		}
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int RegisterCritterDicts(void)
{
	

	// Set up reference dictionaries
	g_hCritterDefDict = RefSystem_RegisterSelfDefiningDictionary("CritterDef",false, parse_CritterDef, true, true, NULL);
	g_hCritterFactionDict = RefSystem_RegisterSelfDefiningDictionary("CritterFaction",false, parse_CritterFaction, true, true, NULL);
	g_hCritterGroupDict = RefSystem_RegisterSelfDefiningDictionary("CritterGroup",false, parse_CritterGroup, true, true, NULL);
	g_hCritterOverrideDict = RefSystem_RegisterSelfDefiningDictionary("CritterOverrideDef",false, parse_CritterOverrideDef, true, true, NULL);

	resDictManageValidation(g_hCritterDefDict, critterResValidateCB);
	resDictManageValidation(g_hCritterFactionDict, critterFactionResValidateCB);
	resDictManageValidation(g_hCritterGroupDict, critterGroupResValidateCB);
	resDictManageValidation(g_hCritterOverrideDict, critterOverrideResValidateCB);

	resDictSetDisplayName(g_hCritterDefDict, "Critter", "Critters", RESCATEGORY_DESIGN);
	resDictSetDisplayName(g_hCritterGroupDict, "CritterGroup", "CritterGroups", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		// He may deny it later, but BenZ told me to pass in "whatever valid ParseTable you want" if I'm making a fake dictionary.
		static DictionaryHandle s_hFakeBubbles;

		if (!s_hFakeBubbles)
			s_hFakeBubbles = RefSystem_RegisterSelfDefiningDictionary("ChatBubbleDef", false, parse_CritterGroup, false, false, NULL);

		resDictProvideMissingResources(g_hCritterDefDict);
		resDictProvideMissingResources(g_hCritterGroupDict);
		resDictProvideMissingResources(g_hCritterFactionDict);
		resDictProvideMissingResources(g_hCritterOverrideDict);

		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hCritterDefDict, ".displayNameMsg.Message", ".Scope", NULL, ".Comment", NULL);
			resDictMaintainInfoIndex(g_hCritterDefDict, ".displaySubNameMsg.Message", ".Scope", NULL, ".Comment", NULL);
			resDictMaintainInfoIndex(g_hCritterGroupDict, ".Name", ".Scope", NULL, ".Notes", NULL);
			resDictMaintainInfoIndex(g_hCritterFactionDict, ".Name", NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hCritterOverrideDict, ".Name", ".Scope", NULL, "Comment", NULL);
		}
	} 
	else
	{
		// Client loading from the server
		resDictRequestMissingResources(g_hCritterDefDict, 8, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCritterGroupDict, 2, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCritterFactionDict, 2, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_hCritterOverrideDict, 8, false, resClientRequestSendReferentCommand);
	}

	// CritterDef and CritterFaction are required in non-edit mode
	resDictProvideMissingRequiresEditMode(g_hCritterGroupDict);	
	resDictProvideMissingRequiresEditMode(g_hCritterOverrideDict);

	// Also register some Enums for static checking
	RegisterNamedStaticDefine(CritterTagsEnum, "CritterTag");
	
	return 1;
}


//-----------------------------------------------------------------------------
// Master Load
//-----------------------------------------------------------------------------

void crittertags_Load(void)
{
	int i, s;
	char * pchTemp = NULL;

	loadstart_printf("Loading crittertags...");

	//do non shared memory load
	if(!ParserLoadFiles(NULL, "defs/critters/crittertags.crittertags", "crittertags.bin", PARSER_OPTIONALFLAG, parse_CritterTags, &g_CritterTags))
	{
		// Error loading, already got a pop-up
	}

	s_pDefineCritterTags = DefineCreate();

	estrStackCreateSize(&pchTemp,20);
	s = eaSize(&g_CritterTags.critterTags);

	for(i=0; i<s; i++)
	{
		estrPrintf(&pchTemp,"%d", i+1);
		DefineAddByHandle(&s_pDefineCritterTags,g_CritterTags.critterTags[i],pchTemp,true);
	}

	estrDestroy(&pchTemp);

	loadend_printf(" done (%d crittertags).",i); 

}

void critterFactionMatrix_SetFactionRelation(CritterFactionMatrix *factionMtx, U32 f1Idx, U32 f2Idx, EntityRelation relation)
{
	if (f1Idx < CFM_MAX_FACTIONS && f2Idx < CFM_MAX_FACTIONS)
	{
		factionMtx->relations[f1Idx][f2Idx] = relation;
	}
}

AUTO_FIXUPFUNC;
TextParserResult fixupCritterFaction(CritterFaction* pCritterFaction, enumTextParserFixupType eType, void *pExtraData)
{
	static S32 s_factionIndex = 0;
	switch (eType)
	{
		case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
			// this is bad, but a temporary work-around for indexing the critter factions loaded since I can't do it after
			// load since it is in shared memory
			pCritterFaction->factionIndex = s_factionIndex++;
			if (s_factionIndex >= CFM_MAX_FACTIONS)
			{	
				s_factionIndex = 0;
			}
	}

	return 1;
}

static void critterFactionProcessFactionMatrix()
{
	S32 i, numFactions;
	DictionaryEArrayStruct *pArrayFaction = resDictGetEArrayStruct(g_hCritterFactionDict);
	CritterFactionMatrix factionMtx = {0};
	
	numFactions = eaSize(&pArrayFaction->ppReferents);
	devassertmsg(numFactions < CFM_MAX_FACTIONS, "Number of CritterFactions is exceeding the limit for the faction matrix. Ask a programmer to increase the limit.");
	/* 
	// I can't do this anymore since the critterFaction dictionary is in shared memory, so we try and get the faction indicies
	// on the fixup of the critterFaction
	for (i = 0; i < numFactions; i++)
	{
		CritterFaction *pFaction = pArrayFaction->ppReferents[i];
		// assign the faction indices
		pFaction->factionIndex = i;
	}
	*/

	for (i = 0; i < numFactions; i++)
	{
		S32 x;
		CritterFaction *pFaction = pArrayFaction->ppReferents[i];

		for (x = 0; x < numFactions; x++)
		{
			CritterFaction *pInnerFaction = pArrayFaction->ppReferents[x];
			EntityRelation relation = faction_GetRelation(pFaction, pInnerFaction);
			critterFactionMatrix_SetFactionRelation(&factionMtx, pFaction->factionIndex, pInnerFaction->factionIndex, relation);
		}		
	}

	#if GAMESERVER || GAMECLIENT
		mmFactionUpdateFactionMatrix(&factionMtx);
	#endif
}

static void critterFactionReferenceEventCB(enumResourceEventType eType, const char *pDictName, const char *pResourceName, CritterFaction *pResource, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		critterFactionProcessFactionMatrix();
	}
}

AUTO_STARTUP(CritterFactions);
void critterFaction_Load(void)
{
	if(IsGameServerBasedType() || IsLoginServer() || IsClient())
	{
		resLoadResourcesFromDisk(g_hCritterFactionDict, CRITTER_BASE_DIR, ".faction", NULL,  PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
		
		resDictRegisterEventCallback(g_hCritterFactionDict, critterFactionReferenceEventCB, NULL);

		critterFactionProcessFactionMatrix();

		if(!FactionDefaults)
		{
			int i;
			DictionaryEArrayStruct *pArrayFaction = resDictGetEArrayStruct(g_hCritterFactionDict);
			FactionDefaults = calloc(sizeof(CritterFactionDefaults),1);

			for (i = 0; i < eaSize(&pArrayFaction->ppReferents); i++)
			{
				CritterFaction *pFaction = eaGet(&pArrayFaction->ppReferents, i);
				if (pFaction->bDefaultEnemyFaction)
				{
					if(IS_HANDLE_ACTIVE(FactionDefaults->hDefaultEnemyFaction))
					{
						CritterFaction* pDefFaction = GET_REF(FactionDefaults->hDefaultEnemyFaction);
						Errorf("Factions %s and %s are both set as the default enemy faction", pFaction->pchName, pDefFaction->pchName);
					}
					else
						SET_HANDLE_FROM_STRING(g_hCritterFactionDict,pFaction->pchName,FactionDefaults->hDefaultEnemyFaction);
				}
				if (pFaction->bDefaultPlayerFaction)
				{
					if(IS_HANDLE_ACTIVE(FactionDefaults->hDefaultPlayerFaction))
					{
						CritterFaction* pDefFaction = GET_REF(FactionDefaults->hDefaultPlayerFaction);
						Errorf("Factions %s and %s are both set as the default player faction", pFaction->pchName, pDefFaction->pchName);
					}
					else
						SET_HANDLE_FROM_STRING(g_hCritterFactionDict,pFaction->pchName,FactionDefaults->hDefaultPlayerFaction);
				}
			}
		} 
	}
}

AUTO_STARTUP(AI);
void fakeAI(void)
{

}

void critter_LoadRankDefs(void)
{
	CritterRankDefs loaderStruct = {0};
	int i;

	loadstart_printf("Loading Critter Rank Defs... ");

	ParserLoadFiles(NULL, "defs/config/critterranks.def", "critterranks.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_CritterRankDefs, &loaderStruct);
	g_eaCritterRankDefs = loaderStruct.eaRanks;

	for(i=0; i<eaSize(&g_eaCritterRankDefs); ++i) {
		eaPush(&g_eaCritterRankNames, g_eaCritterRankDefs[i]->pcName);
		if (g_eaCritterRankDefs[i]->bIsDefault) {
			g_pcCritterDefaultRank = g_eaCritterRankDefs[i]->pcName;
		}
	}

	loadend_printf(" done (%d CritterRankDefs)", eaSize(&g_eaCritterRankDefs));
}


void critter_LoadSubRankDefs(void)
{
	CritterSubRankDefs loaderStruct = {0};
	int i;

	loadstart_printf("Loading Critter Sub Rank Defs... ");

	ParserLoadFiles(NULL, "defs/config/crittersubranks.def", "crittersubranks.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED, parse_CritterSubRankDefs, &loaderStruct);
	g_eaCritterSubRankDefs = loaderStruct.eaRanks;

	for(i=0; i<eaSize(&g_eaCritterSubRankDefs); ++i) {
		eaPush(&g_eaCritterSubRankNames, g_eaCritterSubRankDefs[i]->pcName);
		if (g_eaCritterSubRankDefs[i]->bIsDefault) {
			g_pcCritterDefaultSubRank = g_eaCritterSubRankDefs[i]->pcName;
		}
	}

	loadend_printf(" done (%d CritterSubRankDefs)", eaSize(&g_eaCritterSubRankDefs));
}

AUTO_STARTUP(CritterRanks);
void critter_LoadRanks(void)
{
	critter_LoadRankDefs();
	critter_LoadSubRankDefs();
}

AUTO_STARTUP(Critters) ASTRT_DEPS(RewardsCommon, AS_Messages, Powers, PowerModes, Nemesis, CharacterClasses, EntityCostumes, AI, Items, CritterOverrides, CharacterClassInfos, CritterFactions, CritterGroups, CritterRanks, Species, EntityMovementRequesterDefs, CharacterClassPowers);
void critter_Load(void)
{
	const char *binFile = "critterDef.bin";
#ifdef APPSERVER
	if (isDevelopmentMode() && !gbMakeBinsAndExit)
		binFile = "DevCritterDef.bin";
#endif //APPSERVER

	if(!IsClient())
	{
		resLoadResourcesFromDisk(g_hCritterDefDict, CRITTER_BASE_DIR, ".critter", binFile,  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}
}

AUTO_STARTUP(CritterGroups) ASTRT_DEPS(AS_Messages, CritterFactions, RewardsCommon);
void crittergroup_Load(void)
{
	if(!IsClient())
	{
		resLoadResourcesFromDisk(g_hCritterGroupDict, CRITTERGROUP_BASE_DIR, ".crittergroup", NULL,  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY );
	}
}

AUTO_STARTUP(CritterOverrides) ASTRT_DEPS(Powers);
void CritterOverride_Load(void)
{
	if(!IsClient())
	{
		resLoadResourcesFromDisk(g_hCritterOverrideDict, CRITTEROVERRIDES_BASE_DIR, ".CritterOverride", NULL,  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	}
}


//-----------------------------------------------------------------------------
// Public
//-----------------------------------------------------------------------------

CritterDef * critter_DefGetByName(const char * pchName )
{
	if(pchName && *pchName)
	{
		return (CritterDef*)RefSystem_ReferentFromString(g_hCritterDefDict,pchName);
	}
	return NULL;
}

CritterOverrideDef * critter_OverrideDefGetByName(const char * pchName)
{
	if(pchName && *pchName)
	{
		return (CritterOverrideDef*)RefSystem_ReferentFromString(g_hCritterOverrideDict,pchName);
	}
	return NULL;
}

CritterFaction * critter_FactionGetByName( const char * pchName )
{
	if(pchName && *pchName)
	{
		return (CritterFaction*)RefSystem_ReferentFromString(g_hCritterFactionDict,pchName);
	}
	return NULL;
}

CritterGroup * critter_GroupGetByName( char * pchName )
{
	if(pchName && *pchName)
	{
		return (CritterGroup*)RefSystem_ReferentFromString(g_hCritterGroupDict,pchName);
	}
	return NULL;
}


CritterDef **critter_GetMatchingGroupList( CritterDef *** search_list, CritterGroup * pGroup )
{
	CritterDef **return_list=0;
	int i;

	for(i=eaSize(search_list)-1; i>=0; i--)
	{
		CritterGroup * pSearch = GET_REF((*search_list)[i]->hGroup);
		if((*search_list)[i]->iSpawnLimit == 0 )
			continue;
		if( pGroup == pSearch )
			eaPush(&return_list, (*search_list)[i]);
	}
	return return_list;
}

static CritterDef **critter_GetMatchingLevelList( CritterDef *** search_list, int iLevel )
{
	CritterDef **return_list=0;
	int i;

	for(i=eaSize(search_list)-1; i>=0; i--)
	{
		if((*search_list)[i]->iSpawnLimit == 0 )
			continue;
		// iMaxLevel == -1 means no maximum level
		if((*search_list)[i]->iMinLevel <= iLevel && ((*search_list)[i]->iMaxLevel == -1 || (*search_list)[i]->iMaxLevel >= iLevel ))
		{
			eaPush(&return_list, (*search_list)[i]);
			continue;
		}
		else if(!(*search_list)[i]->noCrossFade) // level cross fading
		{
			F32 fChance = 0;
			S32 minDiff = (*search_list)[i]->iMinLevel - iLevel;
			S32 maxDiff = iLevel - (*search_list)[i]->iMaxLevel;
			if( minDiff > 1 && minDiff < 2 )
				fChance = (-.33*minDiff)+1;
			if( maxDiff > 1 && maxDiff < 2 )
				fChance = (-.33*maxDiff)+1;
			if( randomMersennePositiveF32(NULL) < fChance )
			{
				eaPush(&return_list, (*search_list)[i]);
				continue;
			}
		}

	}
	return return_list;
}

static CritterDef **critter_GetMatchingRankList( CritterDef *** search_list, const char *pcRank )
{
	CritterDef **return_list=0;
	int i;
	for(i=eaSize(search_list)-1; i>=0; i--)
	{
		if((*search_list)[i]->iSpawnLimit == 0 )
			continue;
		if((*search_list)[i]->pcRank == pcRank)
			eaPush(&return_list, (*search_list)[i]);
	}
	return return_list;
}

static CritterDef **critter_GetMatchingSubRankList( CritterDef *** search_list, const char *pcSubRank )
{
	CritterDef **return_list=0;
	int i;
	for(i=eaSize(search_list)-1; i>=0; i--)
	{
		if((*search_list)[i]->iSpawnLimit == 0 )
			continue;
		if((*search_list)[i]->pcSubRank == pcSubRank)
			eaPush(&return_list, (*search_list)[i]);
	}
	return return_list;
}

char* g_DbgCritterPref = NULL;
AUTO_CMD_ESTRING(g_DbgCritterPref, EncCritterPref);

CritterDef * critter_DefFind( CritterGroup * pGroup, const char *pcRank, const char *pcSubRank, int iLevel, int *matchFailure, CritterDef*** excludeDefs)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_hCritterDefDict);
	CritterDef **possible_list = (CritterDef**)pStruct->ppReferents;
	CritterDef ** match_list=0;
	int i, destroy_possible = 0;
	CritterGroup * pCore = RefSystem_ReferentFromString(g_hCritterGroupDict, "Core");
	F32 fTotalWeight = 0, fChoice = 0;

	// assume we don't find match
	if(matchFailure)
		*matchFailure = 0;

	// Look for matching group first, then rank, then level
	// Order matters a lot here, we may want more functions with different priorities
	if( pGroup )
	{
		match_list = critter_GetMatchingGroupList(&possible_list, pGroup);
		if(eaSize(&match_list))
		{
			possible_list = match_list;
			destroy_possible = 1;
		}
		else
		{
			// Using the right CritterGroup is vital; return NULL
			eaDestroy(&match_list);
			if(matchFailure)
				*matchFailure = 1;
			return NULL;
		}
	}
	else
	{
		match_list = critter_GetMatchingGroupList(&possible_list, pCore);
		if(eaSize(&match_list))
		{
			possible_list = match_list;
			destroy_possible = 1;
		}
		else
		{
			// if no matches were found use previous list
			// Errorf( "Could not find any critters belonging to %s faction", pFaction->pchName);
			eaDestroy(&match_list);
			if(matchFailure)
				*matchFailure = 1;
		}
	}

	if( pcRank )
	{
		match_list = critter_GetMatchingRankList(&possible_list, pcRank);
		if(eaSize(&match_list))
		{
			if(destroy_possible)
				eaDestroy(&possible_list);
			else
				destroy_possible = 1;
			possible_list = match_list;
		}
		else
		{
			// if no matches were found use previous list
			// Errorf( "There are no critters belonging to %s faction with rank %i", pFaction ? pFaction->pchName : "Unknown", rank );
			eaDestroy(&match_list);
			if(matchFailure)
				*matchFailure = 1;
		}
	}

	if (gConf.bManualSubRank && pcSubRank)
	{
		match_list = critter_GetMatchingSubRankList(&possible_list, pcSubRank);
		if(eaSize(&match_list))
		{
			if(destroy_possible)
				eaDestroy(&possible_list);
			else
				destroy_possible = 1;
			possible_list = match_list;
		}
		else
		{
			// if no matches were found use previous list
			// Errorf( "There are no critters belonging to %s faction with rank %i", pFaction ? pFaction->pchName : "Unknown", rank );
			eaDestroy(&match_list);
			if(matchFailure)
				*matchFailure = 1;
		}
	}

	if(iLevel>=0)
	{
		match_list = critter_GetMatchingLevelList(&possible_list, iLevel);
		if(eaSize(&match_list))
		{
			if(destroy_possible)
				eaDestroy(&possible_list);
			else
				destroy_possible = 1;
			possible_list = match_list;
		}
		else
		{
			// if no matches were found use previous list
			// Errorf( "There are no critters belong to %s faction matching level %i", pFaction ? pFaction->pchName : "Unknown", iLevel );
			eaDestroy(&match_list);
			if(matchFailure)
				*matchFailure = 1;
		}	
	}

	// Rule out any defs that have exceeded their spawn limit for this encounter
	if(possible_list && excludeDefs)
	{
		int n = eaSize(excludeDefs);
		for(i=0; i<n; i++)
			eaFindAndRemove(&possible_list, (*excludeDefs)[i]);
	}

	if(g_DbgCritterPref && g_DbgCritterPref[0])
	{
		const char *foundStr = allocFindString(g_DbgCritterPref);

		if(1)
		{
			FOR_EACH_IN_EARRAY(possible_list, CritterDef, critter)
			{
				if(strstri(critter->pchName, g_DbgCritterPref))
					return critter;
			}
			FOR_EACH_END
		}
	}

	for( i=eaSize(&possible_list)-1; i>=0; i-- )
		fTotalWeight += possible_list[i]->fSpawnWeight;

	fChoice = fTotalWeight*randomMersennePositiveF32(NULL);
	fTotalWeight = 0;
	for( i=eaSize(&possible_list)-1; i>=0; i-- )
	{
		if( fChoice >= fTotalWeight && fChoice <= fTotalWeight + possible_list[i]->fSpawnWeight )
			return possible_list[i];
		else
			fTotalWeight += possible_list[i]->fSpawnWeight;
	}

	if(eaSize(&possible_list))
		return possible_list[0]; 

	// Get here if exclude list causes last of options to be removed
	if(matchFailure)
		*matchFailure = 1;
	return NULL;
}


void critter_FindAllWithPower( char * pchPower, char ***pppchCritterNames)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_hCritterDefDict);
	CritterDef **ppCritterDefs = (CritterDef**)pStruct->ppReferents;
	int i;

	for(i=eaSize(&ppCritterDefs)-1; i>=0; i--)
	{
		if(critter_OwnsPower(ppCritterDefs[i], pchPower))
		{
			eaPush(pppchCritterNames, ppCritterDefs[i]->pchName);
		}
	}
}


static U64 getLevelRangeBits( int min, int max, int max_level )
{
	U64 val=0;

	if( max < 0 )
		val |= (pow2(max_level)-1);
	else
		val |= (pow2(max)-1);

	if( min > 0)
		val &= ~(pow2(min)-1);

	return val;
}

CritterGroup* entGetCritterGroup(Entity* e)
{
	CritterDef* critterDef;
	Critter* critter = e->pCritter;
	if (critter && (critterDef = GET_REF(critter->critterDef)) && critterDef->pchName)
	{
		CritterGroup *critterGroup = GET_REF(critterDef->hGroup);
		return critterGroup;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Verification
//-----------------------------------------------------------------------------

bool critter_VerifyPrintable( char * str, char * filename )
{
	if(!str)
		return 1; // consider lack of a string ok, we are checking existing strings that suck

	while (*str)
	{
		U8 c = *str;

		if ((c < 'a' || c > 'z')
			&&
			(c < 'A' || c > 'Z')
			&&
			(c < '0' || c > '9')
			&&
			!strchr("!@#$%^&*()-_=+[]{|};:',<.>/?\" ~\r\n\t\\", c))
		{
			ErrorFilenamef(filename, "Bad character '%c' (%i) in \"%s\" ", c, (int)c, str );
			return 0;
		}
		str++;
	}

	return 1;
}

static bool critter_ValidateIdentity( CritterDef * def, bool bOverride, bool bFullData )
{
	if (bFullData && IsServer())
	{	
		if( !def->pchClass || !CharacterClassInfo_FindByName(def->pchClass) )
		{
			ErrorFilenamef( def->pchFileName, "Critter def %s using invalid class: %s.", def->pchName, def->pchClass );
			return 0;
		}
	}
	else
	{
		if ( !def->pchClass || !resGetInfo(g_hCharacterClassInfoDict, def->pchClass)) {
			ErrorFilenamef( def->pchFileName, "Critter def %s using invalid class: %s.", def->pchName, def->pchClass );
			return 0;
		}
	}

	if( IsServer() && !GET_REF(def->hFaction) && REF_STRING_FROM_HANDLE(def->hFaction))
	{	
		ErrorFilenamef( def->pchFileName, "Critter def using invalid faction '%s'", REF_STRING_FROM_HANDLE(def->hFaction) );
		return 0;
	}

	if( IsServer() && !GET_REF(def->hSpecies) && REF_STRING_FROM_HANDLE(def->hSpecies))
	{	
		ErrorFilenamef( def->pchFileName, "Critter def using invalid species '%s'", REF_STRING_FROM_HANDLE(def->hSpecies) );
		return 0;
	}

	if( IsServer() && !GET_REF(def->hGroup) && REF_STRING_FROM_HANDLE(def->hGroup))
	{	
		ErrorFilenamef( def->pchFileName, "Critter def using invalid critter group '%s'", REF_STRING_FROM_HANDLE(def->hGroup) );
		return 0;
	}

	if ( IsServer() && gConf.bManualSubRank && !def->bNonCombat && !critterRankExists(def->pcRank))
	{
		ErrorFilenamef( def->pchFileName, "Critter def using invalid rank '%s'", def->pcRank );
		return 0;
	}

	if ( IsServer() && gConf.bManualSubRank && !def->bNonCombat && !critterSubRankExists(def->pcSubRank))
	{
		ErrorFilenamef( def->pchFileName, "Critter def using invalid sub rank '%s'", def->pcSubRank );
		return 0;
	}

	if( def->iSpawnLimit < -1 || def->iSpawnLimit > 50 )
	{	
		ErrorFilenamef( def->pchFileName, "Critter def %s using spawnlimit (%i) which is out of bounds -1-50.", 
			def->pchName, def->iSpawnLimit );
		return 0;
	}

	if (def->pcRank && !critterRankExists(def->pcRank)) 
	{
		ErrorFilenamef( def->pchFileName, "Critter def %s using critter rank (%s) which does not exist.", 
			def->pchName, def->pcRank );
		return 0;
	}

	return 1;
}

static bool critter_ValidateDisplayInfo( CritterDef * def, bool bOverride )
{	
	if (IsServer() && !GET_REF(def->displayNameMsg.hMessage)) 
	{
		ErrorFilenamef( def->pchFileName, "Critter def mising display name." );
		return 0;
	}

	return 1;
}

static bool critter_ValidateCostume( CritterDef * def, bool bOverride )
{
	int i;
	F32 fTotalWeight = 0;

	for( i = eaSize(&def->ppCostume)-1; i>=0; i-- )
	{
		if( IsServer() && !GET_REF(def->ppCostume[i]->hCostumeRef) )
		{
			if (REF_STRING_FROM_HANDLE(def->ppCostume[i]->hCostumeRef))
				ErrorFilenamef( def->pchFileName, "Critter def %s using non-existent costume '%s'.", def->pchName, REF_STRING_FROM_HANDLE(def->ppCostume[i]->hCostumeRef) );
			else
				ErrorFilenamef( def->pchFileName, "Critter def %s has a costume row with no costume on it.", def->pchName );
			costumeLoad_ValidateCostumeForApply(GET_REF(def->ppCostume[i]->hCostumeRef), def->pchFileName);
			return 0;
		}
		fTotalWeight += def->ppCostume[i]->fWeight;
	}

	if (def->bGenerateRandomCostume)
	{
		if (!REF_STRING_FROM_HANDLE(def->hSpecies))
		{
			ErrorFilenamef( def->pchFileName, "Critter def %s must have a species if GenerateRandomCostume is set.", def->pchName );
			return 0;
		}
		//if (eaSize(&def->ppCostume))
		//{
		//	ErrorFilenamef( def->pchFileName, "Critter def %s can not have any premade costumes if GenerateRandomCostume is set.", def->pchName );
		//	return 0;
		//}
	}
	else if( !def->bTemplate && !bOverride && !fTotalWeight)
	{
		ErrorFilenamef( def->pchFileName, "Critter def %s has no valid costumes (or all weights are 0 and no costume will be picked).", def->pchName);
		return 0;
	}

	return 1;
}

static bool critter_ValidateRewards( CritterDef * def, bool bOverride )
{
	return 1;
}

// Maximum time for any interaction-type task
#define MAX_INTERACT_TIME 600

static char* critter_ValidateProperties(OldInteractionProperties* oldInteractProps)
{
	static char* errorStr = NULL;

	estrSetSize(&errorStr, 0);

	if (oldInteractProps->eInteractType >= (1 << (InteractType_Count - 2)))
		estrConcatf(&errorStr, "\nUndefined interaction type %i.", oldInteractProps->eInteractType);
	
	if (oldInteractProps->uInteractCoolDown > MAX_INTERACT_TIME)
		estrConcatf(&errorStr, "\nInteract cooldown (%u) is out of bounds (0-%d).", oldInteractProps->uInteractCoolDown, MAX_INTERACT_TIME);

	if (oldInteractProps->uInteractTime > MAX_INTERACT_TIME)
		estrConcatf(&errorStr, "\nInteract time (%u) is out of bounds (0-%d).", oldInteractProps->uInteractTime, MAX_INTERACT_TIME);

	if (oldInteractProps->uInteractActiveFor > MAX_INTERACT_TIME)
		estrConcatf(&errorStr, "\nInteract time active (%u) is out of bounds (0-%d).", oldInteractProps->uInteractActiveFor, MAX_INTERACT_TIME);

	return estrLength(&errorStr) ? errorStr : NULL;
}

static bool critter_ValidateInteraction( CritterDef * def, bool bOverride )
{
	static ExprContext* exprContext = NULL;
	char* errorStr;
	InteractionDef* pIntDef = def ? GET_REF(def->hInteractionDef) : NULL;

	if(!def) {
		return false;
	}

	if(!exprContext)
	{
		ExprFuncTable* s_FuncTable = NULL;
		exprContext = exprContextCreate();

		s_FuncTable = encounter_CreateInteractExprFuncTable();

		exprContextSetFuncTable(exprContext, s_FuncTable);

		exprContextSetAllowRuntimePartition(exprContext);
		exprContextSetAllowRuntimeSelfPtr(exprContext);
	}

	//if( !critter_VerifyPrintable( def->interactProps.pchInteractText, def->pchFileName ) )
	//{
	//	ErrorFilenamef( def->pchFileName, "Critter def %s using bad Interact Text", def->pchName );
	//	return 0;
	//}

#if GAMESERVER
	if (gConf.bAllowOldEncounterData) {
		if (def->oldInteractProps.interactCond)
			exprGenerate(def->oldInteractProps.interactCond, exprContext);
		if (def->oldInteractProps.interactSuccessCond)
			exprGenerate(def->oldInteractProps.interactSuccessCond, exprContext);
		if (def->oldInteractProps.interactAction)
			exprGenerate(def->oldInteractProps.interactAction, exprContext);
	}
#endif

	if (gConf.bAllowOldEncounterData && (errorStr = critter_ValidateProperties(&def->oldInteractProps)))
	{
		ErrorFilenamef( def->pchFileName, "Critter def %s has invalid interaction properties:%s", def->pchName, errorStr );
		return 0;
	}

	return 1;
}

static bool critter_ValidateAI( CritterDef * def, bool bOverride )
{
	if (IsGameServerSpecificallly_NotRelatedTypes() && def->pchAIConfig && !RefSystem_ReferentFromString("AIConfig", def->pchAIConfig))
	{
		ErrorFilenamef( def->pchFileName, "Critter def refers to a non-existent AI Config '%s'.", def->pchAIConfig );
		return 0;
	}

	if (IsGameServerSpecificallly_NotRelatedTypes() && !GET_REF(def->hFSM) && REF_STRING_FROM_HANDLE(def->hFSM))
	{
		ErrorFilenamef( def->pchFileName, "Critter def refers to a non-existent FSM '%s'.", REF_STRING_FROM_HANDLE(def->hFSM) );
		return 0;
	}

	if (IsGameServerSpecificallly_NotRelatedTypes() && !GET_REF(def->hCombatFSM) && REF_STRING_FROM_HANDLE(def->hCombatFSM))
	{
		ErrorFilenamef( def->pchFileName, "Critter def refers to a non-existent combat FSM '%s'.", REF_STRING_FROM_HANDLE(def->hCombatFSM) );
		return 0;
	}

	return 1;
}

typedef struct PowerRangeMinMax
{
	Vec2 range;
} PowerRangeMinMax;

static PowerRangeMinMax **s_eaPowerRangeMinMax = NULL;
static PowerRangeMinMax s_initialPowerRangeMinMax = {0,0};

int cmpPowerRangeMinMax(const PowerRangeMinMax **a, const PowerRangeMinMax **b)
{
	return (*a)->range[0] < (*b)->range[0] ? -1 : ((*a)->range[0] > (*b)->range[0] ? 1 : 
		((*a)->range[1] < (*b)->range[1] ? -1 : ((*a)->range[1] > (*b)->range[1] ? 1 : 0)));
}

void powerRangeMinMaxFreeAll()
{
	int i;
	for (i = 0; i < eaSize(&s_eaPowerRangeMinMax); ++i)
	{
		free(s_eaPowerRangeMinMax[i]);
	}
	s_eaPowerRangeMinMax = NULL;
}

void powerRangeMinMaxStart(F32 min, F32 max)
{
	if (s_eaPowerRangeMinMax)
		powerRangeMinMaxFreeAll();
	eaSetSize(&s_eaPowerRangeMinMax, 0);
	s_initialPowerRangeMinMax.range[0] = min;
	s_initialPowerRangeMinMax.range[1] = max;
}

// Add a power range to check
void powerRangeMinMaxAddRange(F32 min, F32 max)
{
	Vec2 range = {min, max};
	int i, rangeIdx = -1;

	if (!s_eaPowerRangeMinMax)
		return;

	if (range[0] < s_initialPowerRangeMinMax.range[0])
		range[0] = s_initialPowerRangeMinMax.range[0];
	else if (range[0] > s_initialPowerRangeMinMax.range[1])
		return;
	if (range[1] > s_initialPowerRangeMinMax.range[1])
		range[1] = s_initialPowerRangeMinMax.range[1];
	else if (range[1] < s_initialPowerRangeMinMax.range[0])
		return;

	for (i = 0; i < eaSize(&s_eaPowerRangeMinMax); ++i)
	{
		// if this range overlaps any existing range in the array, combine them instead of adding it as a new range
		if ((range[0] >= s_eaPowerRangeMinMax[i]->range[0] && range[0] <= s_eaPowerRangeMinMax[i]->range[1]) ||
			(range[1] >= s_eaPowerRangeMinMax[i]->range[0] && range[1] <= s_eaPowerRangeMinMax[i]->range[1]))
		{
			s_eaPowerRangeMinMax[i]->range[0] = MIN(s_eaPowerRangeMinMax[i]->range[0], range[0]);
			s_eaPowerRangeMinMax[i]->range[1] = MAX(s_eaPowerRangeMinMax[i]->range[1], range[1]);
			range[0] = s_eaPowerRangeMinMax[i]->range[0];
			range[1] = s_eaPowerRangeMinMax[i]->range[1];
			if (rangeIdx >= 0 && rangeIdx != i)
				eaRemove(&s_eaPowerRangeMinMax, rangeIdx);
			rangeIdx = i;
		}
	}

	if (rangeIdx == -1)
	{
		PowerRangeMinMax *powerRangeMinMax = malloc(sizeof(PowerRangeMinMax));
		powerRangeMinMax->range[0] = range[0];
		powerRangeMinMax->range[1] = range[1];
		eaPush(&s_eaPowerRangeMinMax, powerRangeMinMax);
	}
}

// Returns true if there is a gap where no power is present in the preferred power range.
// min and max are the start and end values for the first gap.
bool powerRangeMinMaxCheckForGaps(F32 *min, F32 *max)
{
	if (!s_eaPowerRangeMinMax)
		return false;

	eaQSort(s_eaPowerRangeMinMax, cmpPowerRangeMinMax);

	if (eaSize(&s_eaPowerRangeMinMax) > 1)
	{
		*min = s_eaPowerRangeMinMax[0]->range[1];
		*max = s_eaPowerRangeMinMax[1]->range[0];
		return true;
	}
	else if (eaSize(&s_eaPowerRangeMinMax) == 1)
	{
		if (s_eaPowerRangeMinMax[0]->range[0] > s_initialPowerRangeMinMax.range[0])
		{
			*min = s_initialPowerRangeMinMax.range[0];
			*max = s_eaPowerRangeMinMax[0]->range[0];
			return true;
		}
		else if (s_eaPowerRangeMinMax[0]->range[1] < s_initialPowerRangeMinMax.range[1])
		{
			*min = s_eaPowerRangeMinMax[0]->range[1];
			*max = s_initialPowerRangeMinMax.range[1];
			return true;
		}
	}
	else if (eaSize(&s_eaPowerRangeMinMax) == 0)
	{
		*min = s_initialPowerRangeMinMax.range[0];
		*max = s_initialPowerRangeMinMax.range[1];
		return true;
	}
	return false;
}

static bool critter_ValidateVars( CritterDef * def, bool bOverride )
{
	bool bResult = 1;
	int i,j;
	CritterVar *pVar;

	for(i=eaSize(&def->ppCritterVars)-1; i>=0; --i) 
	{
		pVar = def->ppCritterVars[i];
		for(j=i-1; j>=0; --j) 
		{
			if (pVar->var.pcName && def->ppCritterVars[j]->var.pcName && (stricmp(pVar->var.pcName, def->ppCritterVars[j]->var.pcName) == 0)) 
			{
				ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s cannot have two variables named %s", def->pchName, pVar->var.pcName);
				bResult = 0;
			}
		}
	}

	return bResult;
}

static bool critter_ValidateLore( CritterDef * def, bool bOverride )
{
	bool bResult = 1;
	int i,j;
	CritterLore *pLore;

	for(i=eaSize(&def->ppCritterLoreEntries)-1; i>=0; --i) 
	{
		pLore = def->ppCritterLoreEntries[i];
		for(j=i-1; j>=0; --j) 
		{
			if (pLore && def->ppCritterLoreEntries[j] && 
				(pLore->eAttrib == def->ppCritterLoreEntries[j]->eAttrib) &&
				(pLore->DC == def->ppCritterLoreEntries[j]->DC))
			{
				ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s cannot have two lore entries with same Attrib and DC.", def->pchName);
				bResult = 0;
			}
		}
	}

	return bResult;
}

static bool critter_ValidateItems( CritterDef * def, bool bOverride )
{
	int foundPower = 0;
	int i,j, found = 0, chance_pollution = 0;
	DefaultItemDef **chance_list=0;
	CritterItemConfigList **weighted_list=0;
	int iMaxLevel = MAX_LEVELS;
	U64 levels_needed = (pow2(iMaxLevel)-1);
	U64 levels_covered = 0, weighted_levels_covered = levels_needed;
	int weighted_count = 0;
#ifdef GAMESERVER
	AIConfig *pAIConfig = NULL;
	pAIConfig = RefSystem_ReferentFromString("AIConfig", def->pchAIConfig);
	if (pAIConfig && !pAIConfig->movementParams.immobile)
		powerRangeMinMaxStart(pAIConfig->prefMinRange, pAIConfig->prefMaxRange);
	else
		powerRangeMinMaxFreeAll();
#endif	

	if(def->bNonCombat)
		return 1;

	// Make sure all items exist
	for(i=0; i<eaSize(&def->ppCritterItems); i++)
	{
		ItemDef * pDef = GET_REF(def->ppCritterItems[i]->hItem);
		const char *cpchItemName = REF_STRING_FROM_HANDLE(def->ppCritterItems[i]->hItem);

		// PowerDef may not be loaded on the client and this is okay
#ifdef GAMESERVER
		if(!pDef)
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use invalid Item %s", def->pchName, cpchItemName);
			return 0;
		}
#endif
		if( def->ppCritterItems[i]->fChance == 0 )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use invalid item %s with 0 chance", def->pchName, cpchItemName);
			return 0;
		}
		if( def->ppCritterItems[i]->fChance > 1 )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use invalid item %s with chance greater than 1 (setting it to 1)", def->pchName, cpchItemName);
			def->ppPowerConfigs[i]->fChance = 1;
		}
		if( def->ppCritterItems[i]->iMaxLevel >=0 && def->ppCritterItems[i]->iMaxLevel < def->ppCritterItems[i]->iMinLevel )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use item %s with min level greater than max level.", def->pchName, cpchItemName);
			return 0;
		}
	}

	// first gather groups
	for(i=eaSize(&def->ppCritterItems)-1; i>=0; i--)
	{
		for(j=eaSize(&weighted_list)-1; j>=0; j--)
		{
			if( def->ppCritterItems[i]->iGroup == weighted_list[j]->list[0]->iGroup )
			{
				eaPush( &weighted_list[j]->list, def->ppCritterItems[i] );
				continue;
			}
		}
		if( j < 0 ) // new group
		{
			CritterItemConfigList * cl = calloc(1, sizeof(CritterItemConfigList));
			eaPush(&cl->list, def->ppCritterItems[i]);
			eaPush(&weighted_list, cl);
		}
	}

	for(i=eaSize(&weighted_list)-1; i>=0; i--)
	{
		if( !weighted_list[i]->list[0]->fWeight )
		{
			for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
			{
				if( weighted_list[i]->list[j]->fChance == 1 )
					levels_covered |= getLevelRangeBits(weighted_list[i]->list[j]->iMinLevel, weighted_list[i]->list[j]->iMaxLevel, iMaxLevel);

				if( weighted_list[i]->list[0]->fChance != weighted_list[i]->list[j]->fChance)
				{
					ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s has items in the same group (with no weight) using different chances.", def->pchName);
					return 0;
				}
				if( weighted_list[i]->list[0]->fWeight != weighted_list[i]->list[j]->fWeight)
				{
					ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s has items in the same group using different weights.", def->pchName);
					return 0;
				}
			}
		}
		else
		{
			int found_weight = 0;
			weighted_count++;
			for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
			{
				if( weighted_list[i]->list[j]->fChance == 1 )
				{
					found = found_weight = 1;
					weighted_levels_covered &= getLevelRangeBits(weighted_list[i]->list[j]->iMinLevel, weighted_list[i]->list[j]->iMaxLevel, iMaxLevel);
				}

				if( weighted_list[i]->list[0]->fWeight != weighted_list[i]->list[j]->fWeight)
				{
					ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s has items in the same group using different weights.", def->pchName);
					return 0;
				}
			}
			if(!found_weight)
				chance_pollution = 1;
		}
	}

	if( weighted_count == 1)
	{
		ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s only has one weighted group for items, this group has 100 percent chance of selection so you might as well not give it a weighting.", def->pchName);
	}

	if(found && !chance_pollution)
		levels_covered |= weighted_levels_covered;

	levels_covered &= levels_needed;

	for(i=eaSize(&weighted_list)-1; i>=0; i--)
		eaDestroy(&weighted_list[i]->list);
	eaDestroyEx(&weighted_list,0);
	return 1;
}

static bool critter_ValidatePowers( CritterDef * def, bool bOverride )
{
	int foundPower = 0;
	int i,j, found = 0, chance_pollution = 0;
	CritterPowerConfig **chance_list=0;
	CritterConfigList **weighted_list=0;
	int iMaxLevel = MAX_LEVELS;
	U64 levels_needed = (pow2(iMaxLevel)-1);
	U64 levels_covered = 0, weighted_levels_covered = levels_needed;
	int weighted_count = 0;
#ifdef GAMESERVER
	AIConfig *pAIConfig = NULL;
	pAIConfig = RefSystem_ReferentFromString("AIConfig", def->pchAIConfig);
	if (pAIConfig && !pAIConfig->movementParams.immobile)
		powerRangeMinMaxStart(pAIConfig->prefMinRange, pAIConfig->prefMaxRange);
	else
		powerRangeMinMaxFreeAll();
#endif	

	if(def->bNonCombat)
		return 1;

	if( def->lingerDuration < 0 || def->lingerDuration > 500 )
	{
		ErrorFilenameGroupf( def->pchFileName, "Design", 14, "Critter def %s has a linger duration (%f) that is out of bounds (0-500).", def->pchName, def->lingerDuration );
		return 0;
	}

	if (!GET_REF(def->hDefaultStanceDef) && REF_STRING_FROM_HANDLE(def->hDefaultStanceDef))
	{
		ErrorFilenameGroupf( def->pchFileName, "Design", 14, "Critter def %s refers to non-existent power '%s' as a stance power.", def->pchName, REF_STRING_FROM_HANDLE(def->hDefaultStanceDef) );
		return 0;
	}
	if (def->pchRidingPower && !RefSystem_ReferentFromString("PowerDef", def->pchRidingPower))
	{
		ErrorFilenameGroupf( def->pchFileName, "Design", 14, "Critter def %s refers to non-existent power '%s' as a riding power.", def->pchName, def->pchRidingPower );
		return 0;
	}
	if (def->pchRidingItem && !RefSystem_ReferentFromString("ItemDef", def->pchRidingItem))
	{
		ErrorFilenameGroupf( def->pchFileName, "Design", 14, "Critter def %s refers to non-existent item '%s' as a riding item.", def->pchName, def->pchRidingItem );
		return 0;
	}

	// Make sure all powers exist
	for(i=0; i<eaSize(&def->ppPowerConfigs); i++)
	{
		PowerDef * pDef = GET_REF(def->ppPowerConfigs[i]->hPower);
		const char *cpchPowerName = REF_STRING_FROM_HANDLE(def->ppPowerConfigs[i]->hPower);

		if( def->ppPowerConfigs[i]->bDisabled )
			continue;

// PowerDef may not be loaded on the client and this is okay
#ifdef GAMESERVER
		if(!pDef)
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use invalid power %s", def->pchName, cpchPowerName);
			return 0;
		}

		if ((def->ppPowerConfigs[i]->fAIWeight > 0 || def->ppPowerConfigs[i]->pExprAIWeightModifier) && pDef->eType != kPowerType_Passive && pDef->eType != kPowerType_Innate && pDef->eType != kPowerType_Enhancement)
		{
			F32 rangeMax = def->ppPowerConfigs[i]->fAIPreferredMaxRange;
			if (rangeMax == 0.0f)
			{
				rangeMax = pDef->iAIMaxRange;
			}
			foundPower = 1;
			powerRangeMinMaxAddRange(def->ppPowerConfigs[i]->fAIPreferredMinRange, rangeMax);
		}
#endif
		if( def->ppPowerConfigs[i]->fChance == 0 )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use invalid power %s with 0 chance", def->pchName, cpchPowerName);
			return 0;
		}
		if( def->ppPowerConfigs[i]->fChance > 1 )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use invalid power %s with chance greater than 1 (setting it to 1)", def->pchName, cpchPowerName);
			def->ppPowerConfigs[i]->fChance = 1;
		}
		if( def->ppPowerConfigs[i]->fAIPreferredMaxRange < def->ppPowerConfigs[i]->fAIPreferredMinRange )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use power %s with min AI range greater than max AI range.", def->pchName, cpchPowerName);
			return 0;
		}
		if( def->ppPowerConfigs[i]->iMaxLevel >=0 && def->ppPowerConfigs[i]->iMaxLevel < def->ppPowerConfigs[i]->iMinLevel )
		{
			ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s trying to use power %s with min level greater than max level.", def->pchName, cpchPowerName);
			return 0;
		}

		
	}

	for(i=0;i<eaSize(&def->ppCritterItems);i++)
	{
		ItemDef *pItemDef = GET_REF(def->ppCritterItems[i]->hItem);
		const char *cpchItemName = REF_STRING_FROM_HANDLE(def->ppCritterItems[i]->hItem);

#ifdef GAMESERVER
		if(!pItemDef)
		{
			ErrorFilenameGroupf(def->pchFileName,"Design", 14, "Critter %s trying to use invalid item %s", def->pchName, cpchItemName);
			return 0;
		}

		for(j=0;j<eaSize(&pItemDef->ppItemPowerDefRefs);j++)
		{
			ItemPowerDef *pItemPower = GET_REF(pItemDef->ppItemPowerDefRefs[j]->hItemPowerDef);

			if(!pItemPower)
				continue;

			if(pItemPower->pPowerConfig)
			{
				PowerDef *pDef = GET_REF(pItemPower->hPower);
				if (pDef)
				{
					F32 rangeMax = pItemPower->pPowerConfig->fAIPreferredMaxRange;
					if (rangeMax == 0.0f)
					{
						rangeMax = pDef->iAIMaxRange;
					}
					foundPower = 1;
					powerRangeMinMaxAddRange(pItemPower->pPowerConfig->fAIPreferredMinRange,rangeMax);
				}
			}
		}
#endif
	}

	{
		F32 gapMin = 0.0f, gapMax = 0.0f;
		if ( foundPower && !def->bTemplate && powerRangeMinMaxCheckForGaps(&gapMin, &gapMax) )
		{
			//printf("FILE: %s\nWarning: Critter %s has no power within the range %1.1f - %1.1f.\n", def->pchFileName, def->pchName, gapMin, gapMax);

			//Disable Range error for now, because it is NOT doing the correct calculation. It needs to pull data off adam's new range stuff -BZ
			//ErrorFilenameGroupRetroactivef(def->pchFileName, "Design", 14, 2, 6, 2008, "Warning: Critter %s has no power within the range %1.1f - %1.1f.", def->pchName, gapMin, gapMax);
		}
	}

	// first gather groups
	for(i=eaSize(&def->ppPowerConfigs)-1; i>=0; i--)
	{
		if( def->ppPowerConfigs[i]->bDisabled )
			continue;

		for(j=eaSize(&weighted_list)-1; j>=0; j--)
		{
			if( def->ppPowerConfigs[i]->iGroup == weighted_list[j]->list[0]->iGroup )
			{
				eaPush( &weighted_list[j]->list, def->ppPowerConfigs[i] );
				continue;
			}
		}
		if( j < 0 ) // new group
		{
			CritterConfigList * cl = calloc(1, sizeof(CritterConfigList));
			eaPush(&cl->list, def->ppPowerConfigs[i]);
			eaPush(&weighted_list, cl);
		}
	}

	for(i=eaSize(&weighted_list)-1; i>=0; i--)
	{
		if( !weighted_list[i]->list[0]->fWeight )
		{
			for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
			{
				if( weighted_list[i]->list[j]->fChance == 1 && !weighted_list[i]->list[j]->pExprAddPowerRequires )
					levels_covered |= getLevelRangeBits(weighted_list[i]->list[j]->iMinLevel, weighted_list[i]->list[j]->iMaxLevel, iMaxLevel);

				if( weighted_list[i]->list[0]->fChance != weighted_list[i]->list[j]->fChance)
				{
					ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s has powers in the same group (with no weight) using different chances.", def->pchName);
					return 0;
				}
				if( weighted_list[i]->list[0]->fWeight != weighted_list[i]->list[j]->fWeight)
				{
					ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s has powers in the same group using different weights.", def->pchName);
					return 0;
				}
			}
		}
		else
		{
			int found_weight = 0;
			weighted_count++;
			for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
			{
				if( weighted_list[i]->list[j]->fChance == 1 && !weighted_list[i]->list[j]->pExprAddPowerRequires )
				{
					found = found_weight = 1;
					weighted_levels_covered &= getLevelRangeBits(weighted_list[i]->list[j]->iMinLevel, weighted_list[i]->list[j]->iMaxLevel, iMaxLevel);
				}

				if( weighted_list[i]->list[0]->fWeight != weighted_list[i]->list[j]->fWeight)
				{
					ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s has powers in the same group using different weights.", def->pchName);
					return 0;
				}
			}
			if(!found_weight)
				chance_pollution = 1;
		}
	}

	if( weighted_count == 1)
	{
		ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s only has one weighted group, this group has 100 percent chance of selection so you might as well not give it a weighting.", def->pchName);
	}

	if(found && !chance_pollution)
		levels_covered |= weighted_levels_covered;

	levels_covered &= levels_needed;

	if( !def->bTemplate && levels_covered != levels_needed && !def->bNoPowersAllowed ) // all levels covered
	{
		ErrorFilenameGroupf(def->pchFileName, "Design", 14, "Critter %s does not have powers that cover all level ranges", def->pchName);
		return 0;
	}

	for(i=eaSize(&weighted_list)-1; i>=0; i--)
		eaDestroy(&weighted_list[i]->list);
	eaDestroyEx(&weighted_list,0);
	return 1;
}

bool critter_DefValidate( CritterDef *def, bool bFullData)
{
	int retVal = 1;
#ifdef GAMESERVER
	const char *pchTempFileName;

	if( !def )
	{	
		ErrorFilenamef( "No File", "Invalid Critter Def!" );
		return 0;
	}

	if( !def->pchFileName )
	{
		ErrorFilenamef( "No File", "Critter Def missing filename." );
		retVal =  0;
	}

	if( !def->pchName || !def->pchName[0] )
	{
		ErrorFilenamef( def->pchFileName, "Critter def mising name." );
		retVal =  0;
	}

	if( !resIsValidName(def->pchName) )
	{
		ErrorFilenamef( def->pchFileName, "Critter name is illegal: '%s'", def->pchName );
		retVal =  0;
	}

	if( !resIsValidScope(def->pchScope) )
	{
		ErrorFilenamef( def->pchFileName, "Critter scope is illegal: '%s'", def->pchScope );
		retVal =  0;
	}
	if (StructInherit_GetParentName(parse_CritterDef, def) && 
		StructInherit_GetOverrideType(parse_CritterDef, def, ".scope") == OVERRIDE_NONE)
	{
		ErrorFilenamef( def->pchFileName, "Critter scope should not be inherited from parent");
		retVal =  0;
	}

	if ((StructInherit_GetParentName(parse_CritterDef, def) != NULL) &&
		(StructInherit_GetOverrideType(parse_CritterDef, def, ".KeyBlock") == OVERRIDE_NONE))
	{
		ErrorFilenamef( def->pchFileName, "Critter key block should not be inherited from parent");
		retVal =  0;
	}

	pchTempFileName = def->pchFileName;
	if (resFixPooledFilename(&pchTempFileName, CRITTER_BASE_DIR, def->pchScope, def->pchName, CRITTER_EXTENSION)) {
		if (IsServer()) 
		{
			char nameSpace[RESOURCE_NAME_MAX_SIZE];
			char baseObjectName[RESOURCE_NAME_MAX_SIZE];
			if (!resExtractNameSpace(pchTempFileName, nameSpace, baseObjectName) || stricmp(baseObjectName, def->pchFileName) != 0)
			{
				ErrorFilenamef( def->pchFileName, "Critter filename does not match name '%s' scope '%s'", def->pchName, def->pchScope);
				retVal =  0;
			}
		}
	}

	if( !critter_ValidateInteraction( def, 0 ) )
		retVal =  0;

	if( !critter_ValidateIdentity( def, 0, bFullData) ||
		!critter_ValidateDisplayInfo( def, 0 ) ||
		!critter_ValidateCostume( def,  0 ) ||
		!critter_ValidateRewards( def, 0 ) ||
		!critter_ValidateAI( def, 0 ) ||
		!critter_ValidatePowers( def, 0 ) ||
		!critter_ValidateVars( def, 0 ) ||
		!critter_ValidateItems( def, 0))
	{
		retVal = 0;
	}

	if (eaSize(&def->ppchStanceWords) > 2)
	{
		S32 size = eaSize(&def->ppchStanceWords);
		Alertf("%s : CritterDef has %d stances. Are you sure you need this many?", def->pchFileName, size);
	}

#endif
	return retVal;
}

//////////////////////////
// Pet Store Load 
//////////////////////////

DictionaryHandle *g_hPetStoreDict;
DictionaryHandle *g_hPetDiagDict;

#ifdef APPSERVER
PetDef **g_ppAutoGrantPets = NULL;

static void petStore_Scan(PetDef *pDef)
{
	if(pDef->bAutoGrant)
		eaPush(&g_ppAutoGrantPets,pDef);
}
#endif

static void petDiag_Validate(PetDiag *pDiag)
{
	int i;
	if(pDiag)
	{
		for(i=0;i<eaSize(&pDiag->ppNodes);i++)
		{
			if(eaSize(&pDiag->ppNodes[i]->ppReplacements) != pDiag->ppNodes[i]->iCount)
			{
				ErrorFilenamef(pDiag->pchFilename, "Pet Diag %s has an incorrect number of replacements to count %d:%d",pDiag->pchName,pDiag->ppNodes[i]->iCount,eaSize(&pDiag->ppNodes[i]->ppReplacements));
			}
		}
	}
}

static void petStore_Fixup(PetDef *pDef)
{
	CritterDef* pCritterDef = GET_REF(pDef->hCritterDef);
	char* pchClass = pCritterDef ? pCritterDef->pchClass : NULL;
	CharacterClass* pClass = pCritterDef ? characterclass_GetAdjustedClass(pchClass, 0, NULL, pCritterDef) : NULL;

	if (pClass)
	{
		SET_HANDLE_FROM_REFERENT("CharacterClass", pClass, pDef->hClass);
	}
	else
	{
		REMOVE_HANDLE(pDef->hClass);
	}
}

static void petStore_Validate(PetDef *pDef)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	// TomY Workaround for lots of validation spam on ServerBinner
	if (isProductionMode())
		return;

	if (!GET_REF(pDef->hCritterDef)) 
	{
		if (!REF_STRING_FROM_HANDLE(pDef->hCritterDef))
			ErrorFilenamef(pDef->pchFilename, "Pet def '%s' does not specify a critter def and one is required", pDef->pchPetName);
		else
			ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to non-existent critter def '%s'", pDef->pchPetName, REF_STRING_FROM_HANDLE(pDef->hCritterDef));
	}

	if (!GET_REF(pDef->displayNameMsg.hMessage)) 
	{
		if (!REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage))
			ErrorFilenamef(pDef->pchFilename, "Pet def '%s' does not specify a display name message and one is required", pDef->pchPetName);
		else
			ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to non-existent message '%s'", pDef->pchPetName, REF_STRING_FROM_HANDLE(pDef->displayNameMsg.hMessage));
	}

	for(i=eaSize(&pDef->ppAlwaysPropSlot)-1; i>=0; --i)
	{
		if (!GET_REF(pDef->ppAlwaysPropSlot[i]->hPropDef)) 
		{
			ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to non-existent always prop slot '%s'", pDef->pchPetName, REF_STRING_FROM_HANDLE(pDef->ppAlwaysPropSlot[i]->hPropDef));
		}
	}
	for(i=eaSize(&pDef->ppEscrowPowers)-1; i>=0; --i)
	{
		if (!GET_REF(pDef->ppEscrowPowers[i]->hNodeDef)) 
		{
			ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to non-existent power node '%s'", pDef->pchPetName, REF_STRING_FROM_HANDLE(pDef->ppEscrowPowers[i]->hNodeDef));
		}
	}
	if (IS_HANDLE_ACTIVE(pDef->hClass) && !GET_REF(pDef->hClass))
	{
		ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to non-existent character class '%s'", pDef->pchPetName, REF_STRING_FROM_HANDLE(pDef->hClass));
	}

	//if (pDef->bCanBePuppet)
	{
		CritterDef *pCritter = GET_REF(pDef->hCritterDef);
		if (pCritter)
		{
			if (pDef->bCanBePuppet && !eaSize(&pCritter->ppCostume))
				ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to critter def '%s' that has no costumes defined", pDef->pchPetName, pCritter->pchName);

			for(i=eaSize(&pCritter->ppCostume)-1; i>=0; --i)
			{
				if (!GET_REF(pCritter->ppCostume[i]->hCostumeRef))
				{
					if (pDef->bCanBePuppet)
					{
						if (!REF_STRING_FROM_HANDLE(pCritter->ppCostume[i]->hCostumeRef))
							ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to critter def '%s' that has a missing costume", pDef->pchPetName, pCritter->pchName);
						else
							ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to critter def '%s' that refers to non-existent costume '%s'", pDef->pchPetName, pCritter->pchName, REF_STRING_FROM_HANDLE(pCritter->ppCostume[i]->hCostumeRef));
					}
				}
				else if (pDef->bCanBePuppet || GET_REF(pCritter->ppCostume[i]->hCostumeRef)->eCostumeType == kPCCostumeType_Player)
				{
					char *estrReason = NULL;
					if (!costumeValidate_ValidatePlayerCreated(GET_REF(pCritter->ppCostume[i]->hCostumeRef), GET_REF(GET_REF(pCritter->ppCostume[i]->hCostumeRef)->hSpecies), NULL, NULL, NULL, &estrReason, NULL, NULL, false)) 
						ErrorFilenamef(pDef->pchFilename, "Pet def '%s' refers to critter def '%s' that refers costume '%s', but this costume is not legal for use on a player's puppet.  Reason: %s", pDef->pchPetName, pCritter->pchName, REF_STRING_FROM_HANDLE(pCritter->ppCostume[i]->hCostumeRef), estrReason);
					estrDestroy(&estrReason);
				}
			}
		}
	}

	if(IS_HANDLE_ACTIVE(pDef->hTradableItem))
	{
		ItemDef *pItemDef = GET_REF(pDef->hTradableItem);
		if(!pItemDef)
		{
			ErrorFilenamef(pDef->pchFilename, "Pet Def '%s' refers to a tradeable item def that does not exist '%s'",pDef->pchPetName, REF_STRING_FROM_HANDLE(pDef->hTradableItem));
		}
		else if(pItemDef->eType != kItemType_Container)
		{
			ErrorFilenamef(pDef->pchFilename, "Pet Def '%s' refers to a tradeable item that is not of type Container '%s'->type == %s", pDef->pchPetName, pItemDef->pchName, StaticDefineIntRevLookup(ItemTypeEnum,pItemDef->eType));
		}
	}

	if (!pDef->bCanBePuppet)
	{
		if (eaSize(&g_OfficerRankStruct.eaRanks))
		{
			Officer_ValidateSkills(pDef);
		}
		if (pDef->iMinActivePuppetLevel)
		{
			ErrorFilenamef(pDef->pchFilename, "Pet Def '%s' has a MinActivePuppetLevel set to %d, but isn't a puppet", pDef->pchPetName, pDef->iMinActivePuppetLevel);
		}
	}
	PERFINFO_AUTO_STOP();
}

static int petDiagResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PetDiag *pDiag, U32 userID)
{
#ifdef GAMESERVER
	switch (eType)
	{	
		xcase RESVALIDATE_POST_BINNING:
			petDiag_Validate(pDiag);
			return VALIDATE_HANDLED;
	}
#endif
	return VALIDATE_NOT_HANDLED;
}

static int petStoreResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PetDef *pDef, U32 userID)
{
	switch (eType)
	{	

		case RESVALIDATE_POST_BINNING:
		{
#ifdef GAMESERVER
			petStore_Validate(pDef);
			return VALIDATE_HANDLED;
#endif
#if defined(GAMESERVER) || defined(APPSERVER)
			petStore_Fixup(pDef);
			return VALIDATE_HANDLED;
#endif
			break;
		}

#ifdef APPSERVER
		case RESVALIDATE_FINAL_LOCATION:
			petStore_Scan(pDef);
			return VALIDATE_HANDLED;
			break;
#endif
	}

	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void RegisterPetStoreDict(void)
{
	g_hPetStoreDict = RefSystem_RegisterSelfDefiningDictionary("PetDef", false, parse_PetDef, true, true, NULL);
	resDictManageValidation(g_hPetStoreDict, petStoreResValidateCB);

	if(IsServer())
	{
		resDictProvideMissingResources(g_hPetStoreDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hPetStoreDict, NULL, NULL, NULL, NULL, NULL);
		}
	}
	else
	{
		resDictRequestMissingResources(g_hPetStoreDict, 16, false, resClientRequestSendReferentCommand );
	}

	g_hPetDiagDict = RefSystem_RegisterSelfDefiningDictionary("PetDiag", false, parse_PetDiag, true, true, NULL);
	resDictManageValidation(g_hPetDiagDict, petDiagResValidateCB);
}

static void PetStoreReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading PetDefs...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFileToDictionary(pchRelPath,g_hPetStoreDict);

	loadend_printf(" done (%d PetDefs)", RefSystem_GetDictionaryNumberOfReferentInfos(g_hPetStoreDict));
}

AUTO_STARTUP(PetStore) ASTRT_DEPS(PetRestrictions, Critters, EntityCostumes, Powers, CharacterClasses, InteriorDefs, Items, Officers);
void PetStoreLoad(void)
{
	AlwaysPropSlotLoad();
	
	if(IsClient())
	{
		//Do nothing
	}
	else
	{
		resLoadResourcesFromDisk(g_hPetStoreDict, "defs/pets/", ".pet", "petStore.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
		resLoadResourcesFromDisk(g_hPetDiagDict, NULL, "defs/config/petDiag.def", "petDiag.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

		if(isDevelopmentMode())
		{
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/pets/*.pet", PetStoreReload);
		}
	}
}

AUTO_STARTUP(PetStore_AppServer) ASTRT_DEPS(Critter_AppServer);
void PetStoreLoad_AppServer(void)
{	
	PetRestrictionsLoad();
	PetStoreLoad();
}

AUTO_STARTUP(Critter_AppServer) ASTRT_DEPS(CharacterClasses, EntityCostumes, Powers, CharacterClassInfos);
void aslLoadCritter_AppServer(void)
{
	critter_Load();
}

static int CompareCostumes(const CritterCostume **costume1, const CritterCostume **costume2)
{
	if ((*costume1)->fOrder > (*costume2)->fOrder) {
		return 1;
	} else if ((*costume1)->fOrder < (*costume2)->fOrder) {
		return -1;
	}
	return 0;
}

void critterdef_CostumeSort(CritterDef *pDef, CritterCostume ***pppCostumesSorted)
{
	eaCopy(pppCostumesSorted,&pDef->ppCostume);
	eaQSort(*pppCostumesSorted,CompareCostumes);
}

// The index for this function is 1-based (UI display index)
int critterdef_GetCostumeKeyFromIndex(CritterDef *pDef, int iIndex)
{
	CritterCostume **ppCostumes = NULL;
	int s = eaSize(&pDef->ppCostume);
	int iKey = 0;
	
	iIndex--;

	if(!s || iIndex < 0 || iIndex >= s)
		return 0;

	critterdef_CostumeSort(pDef,&ppCostumes);
	iKey = ppCostumes[iIndex]->iKey;
	eaDestroy(&ppCostumes);
	return iKey;
}


CharClassTypes petdef_GetCharacterClassType(PetDef* pPetDef)
{
	if (pPetDef)
	{
		CharacterClass* pClass = GET_REF(pPetDef->hClass);
		if (pClass)
		{
			return pClass->eType;
		}
	}
	return CharClassTypes_None;
}

//gameserver and gameclient ONLY
#if GAMESERVER || GAMECLIENT

static __forceinline void addPowerDefToListMaybe( CritterPowerConfig *** list, CritterPowerConfig * pCpc )
{
	F32 fChance = 0.f;

	if( pCpc->fChance )
		fChance = randomMersennePositiveF32(NULL);

	if( fChance <= pCpc->fChance )
	{
		eaPush(list, pCpc);
	}
}

static __forceinline void addPowersFromPowerConfigList( CritterDef *pDef, CritterPowerConfig ***list )
{
	int i,j;
	static CritterConfigList **weighted_list=0;
	F32 fRand = 0, fWeightTotal = 0, fWeightCumm = 0;

	PERFINFO_AUTO_START_FUNC();

	// first gather groups
	for(i=eaSize(&pDef->ppPowerConfigs)-1; i>=0; i--)
	{
		int added = 0;

		if( pDef->ppPowerConfigs[i]->bDisabled )
			continue;

		for(j=eaSize(&weighted_list)-1; j>=0; j--)
		{
			if( pDef->ppPowerConfigs[i]->iGroup == weighted_list[j]->list[0]->iGroup )
			{
				eaPush( &weighted_list[j]->list, pDef->ppPowerConfigs[i] );
				added = 1;
				break;
			}
		}

		if( !added ) // new group
		{
			CritterConfigList * cl = calloc(1, sizeof(CritterConfigList));
			eaPush(&cl->list, pDef->ppPowerConfigs[i]);
			eaPush(&weighted_list, cl);
			fWeightTotal += pDef->ppPowerConfigs[i]->fWeight;
		}
	}


	// loop over and give everything in non-weighted group
	fRand = randomMersennePositiveF32(NULL)*fWeightTotal;
	for(i=eaSize(&weighted_list)-1; i>=0; i--)
	{
		if( !weighted_list[i]->list[0]->fWeight )
		{
			for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
			{
				addPowerDefToListMaybe(list, weighted_list[i]->list[j]);
			}
		}
		else
		{
			if(fRand >= fWeightCumm && fRand <= fWeightCumm+weighted_list[i]->list[0]->fWeight )
			{
				for(j=eaSize(&weighted_list[i]->list)-1; j>=0; j--)
				{
					addPowerDefToListMaybe(list, weighted_list[i]->list[j]);
				}
			}
			if(weighted_list[i])
				fWeightCumm += weighted_list[i]->list[0]->fWeight;
		}
	}

	for(i=eaSize(&weighted_list)-1; i>=0; i--)
		eaDestroy(&weighted_list[i]->list);
	eaClearEx(&weighted_list,0);
	PERFINFO_AUTO_STOP();
}

//for client-side fake entities
void critter_AddPowerNoTransact(NOCONST(Entity)* ent,
	PowerDef *pdef,
	int iLevel,
	int bAllowDuplicates)
{
	NOCONST(Power) *ppow;

	if(!bAllowDuplicates)
	{
		int i;
		for(i=eaSize(&ent->pChar->ppPowersPersonal)-1; i>=0; i--)
		{
			if(pdef==GET_REF(ent->pChar->ppPowersPersonal[i]->hDef))
			{
				return;
			}
		}
	}
	ppow = entity_CreatePowerHelper(ent,pdef,iLevel);
	eaIndexedAdd(&ent->pChar->ppPowersPersonal,ppow);
}

void critter_AddCombat( Entity *be, CritterDef *pCritter, int iLevel, int iTeamSize, const char *pcSubRank, 
						F32 fRandom, bool bAddPowers, bool bFullReset, CharacterClass* pClass, bool bPowersEntCreatedEnt)
{
	int i, base_overridden=0;
	static CritterPowerConfig **final_power_list=0;
	CritterDef *critter = pCritter;
	NOCONST(Entity) *beNoConst = CONTAINER_NOCONST(Entity, be);
	PowerDef **ppStanceDefs = NULL;
	int iPartitionIdx;

	if (!be->bFakeEntity)
	{
#ifdef GAMESERVER
		gslCacheEntRegion(be,NULL);
#endif
	}

	if( !pCritter || pCritter->bNonCombat )
		return;

	PERFINFO_AUTO_START_FUNC();

	addPowersFromPowerConfigList(pCritter,&final_power_list);

	if( !eaSize(&final_power_list) && !pCritter->bNoPowersAllowed)
	{
		ErrorFilenamef(critter->pchFileName, "Critter %s does not have any powers chosen", critter->pchName);
	}

	if (!be->pChar)
	{
		beNoConst->pChar = StructCreateNoConst(parse_Character);
		beNoConst->pChar->pEntParent = be;
	}
	iPartitionIdx = entGetPartitionIdx(be);

	if (!pClass)
		pClass = characterclass_GetAdjustedClass(critter->pchClass, iTeamSize, pcSubRank, critter);

	SET_HANDLE_FROM_STRING(g_hCharacterClassDict,pClass->pchName,beNoConst->pChar->hClass);

	// Is this correct, how do non persisted things that share a persisted struct write to those fields?
	// TODO: Add max and min levels
	beNoConst->pChar->iLevelCombat = CLAMP(iLevel, 1, MAX_LEVELS);
	entity_SetDirtyBit(be, parse_Entity, be, false);
	
#ifdef GAMESERVER
	if (bPowersEntCreatedEnt)
		be->pChar->uiPowersCreatedEntityTime = pmTimestamp(0.f);
#endif

	if (!be->bFakeEntity)
	{
		// Prepare for use!
		if(bFullReset)
			character_Reset(iPartitionIdx, be->pChar, be, NULL);
		else
		{
			character_RefreshPassives(iPartitionIdx, be->pChar, NULL);
			character_RefreshToggles(iPartitionIdx, be->pChar, NULL);
		}
	}
	else if (bFullReset)
	{
		if (be->pChar->pattrBasic) {
			StructReset(parse_CharacterAttribs, be->pChar->pattrBasic);
		} else {
			be->pChar->pattrBasic = StructCreate(parse_CharacterAttribs);
		}
		character_AutoSpendStatPoints(be->pChar);
	}

	// Default is 0, so don't reset
	if(critter->bUntargetable)
		entSetDataFlagBits(be, ENTITYFLAG_UNTARGETABLE);

	// Default is 0, so don't reset
	if(critter->bUnselectable)
		entSetDataFlagBits(be, ENTITYFLAG_UNSELECTABLE);

	be->pChar->bInvulnerable = critter->bInvulnerable;
	be->pChar->bUnstoppable = critter->bUnstoppable;
	be->pChar->bUnkillable = critter->bUnkillable;
	be->pChar->bLevelAdjusting = critter->bLevelAdjusting;
	be->pChar->bDisableFaceActivate = critter->bDisableFaceActivate;
	be->pChar->bSpecialLargeMonster = critter->bSpecialLargeMonster;

	entity_SetDirtyBit(be, parse_Character, be->pChar, false);

	if(bAddPowers)
	{
		PERFINFO_AUTO_START("Add powers", 1);
		for( i=eaSize(&final_power_list)-1; i>=0; i-- )
		{
			CritterPowerConfig *cpc = final_power_list[i];
			PowerDef * pDef = GET_REF(cpc->hPower);

			if(!pDef)
			{
				ErrorFilenamef(critter->pchFileName, "Power def not found: %s", REF_STRING_FROM_HANDLE(cpc->hPower));
				continue;
			}

			if( (cpc->iMinLevel >= 0 && iLevel < cpc->iMinLevel) ||
				(cpc->iMaxLevel >= 0 && iLevel > cpc->iMaxLevel) )
			{
				continue; // only add powers in level range
			}


#ifdef GAMECLIENT
			//Client-side, this function is used to set up fake entities for UI purposes. Expressions are not respected.
			critter_AddPowerNoTransact(beNoConst, pDef, 0, true);
#else
			if(!be->bFakeEntity && cpc->pExprAddPowerRequires)
			{
				MultiVal mv = {0};
				ExprContext *pContext = critter_GetEncounterExprContext();
				OldEncounter *pEncounter = be->pCritter ? be->pCritter->encounterData.parentEncounter : NULL;
				GameEncounter *pGameEncounter = be->pCritter ? be->pCritter->encounterData.pGameEncounter : NULL;;

				//Setup the encounter expression
				critter_SetupEncounterExprContext(pContext, be, iPartitionIdx, iTeamSize, fRandom, pEncounter, pGameEncounter);

				//Evaluate the AddPowerRequires expression
				exprEvaluate(cpc->pExprAddPowerRequires, pContext, &mv);

				exprContextClearPartition(pContext);

				if(!MultiValGetInt(&mv, NULL))
				{
					continue;
				}
			}
			character_AddPowerPersonal(iPartitionIdx,be->pChar,pDef,0,true,NULL);
			if(!be->bFakeEntity && pCritter->bRandomDefaultStance)
			{
				if(pDef->eType!=kPowerType_Innate && pDef->eType!=kPowerType_Passive)
				{
					PowerAnimFX *pafx = GET_REF(pDef->hFX);
					if(pafx && !poweranimfx_IsEmptyStance(pafx->uiStanceID))
					{
						eaPush(&ppStanceDefs,pDef);
					}
				}
			}
#endif

		}

		entity_FixPowersClassHelper(CONTAINER_NOCONST(Entity, be));
		
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_START("Stances", 1);
	if(pCritter->bRandomDefaultStance && eaSize(&ppStanceDefs))
	{
		U32 seed = PTR_TO_UINT(be);
		int idx = randomIntRangeSeeded(&seed,RandType_LCG,0,eaSize(&ppStanceDefs)-1);
		character_SetDefaultStance(iPartitionIdx, be->pChar,ppStanceDefs[idx]);
		eaDestroy(&ppStanceDefs);
	}
	PERFINFO_AUTO_STOP();
#ifndef GAMECLIENT
	if(be->aibase)
		aiAddPowersFromCritterPowerConfigs(be, be->aibase, final_power_list, pCritter->pchFileName);
#endif
	eaSetSize(&final_power_list, 0);

	PERFINFO_AUTO_STOP();
}

#endif

#include "AutoGen/entCritter_h_ast.c"
