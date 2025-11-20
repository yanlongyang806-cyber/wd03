//////////////////////////////////////////////////////////////////////////
// This file is split into three parts. First, the individual field type
// formatters, which should be added to as needed. Second, the actual
// string formatting function, which should never change. Third, a set
// of expressions to format using these functions.
//
// TODO:
// - Static checking for fields, vars (based on scope)
// - Extend to more types (power tree, power nodes, whatever)
// - Merge with default string formatter (needs support for default/fallback variables)
// - Localized int, float formatting.
// - CharOwnerName -- complicated because they don't have a single fallback.

#include "estring.h"
#include "Expression.h"
#include "file.h"

#include "AccountDataCache.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "EntitySavedData.h"
#include "contact_common.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Player.h"
#include "Powers.h"
#include "entCritter.h"
#include "Guild.h"
#include "mission_common.h"
#include "contact_common.h"
#include "nemesis_common.h"
#include "OfficerCommon.h"
#include "objPath.h"
#include "SavedPetCommon.h"
#include "Species_Common.h"
#include "GameStringFormat.h"
#include "GameAccountDataCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonTailor.h"
#include "PowersAutoDesc.h"
#include "MicroTransactions.h"
#include "Money.h"
#include "stdtypes.h"
#include "stashtable.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#ifdef GAMESERVER
#include "gslMechanics.h"
#include "gslMapVariable.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslWorldVariable.h"
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//////////////////////////////////////////////////////////////////////////
// Individual field type formatters / conditions

typedef bool (*FieldFormat)(unsigned char **ppchResult, StrFmtContainer *pContainer, void *pData, const unsigned char *pchField, StrFmtContext *pContext);
typedef bool (*FieldCondition)(StrFmtContainer *pContainer, void *pData, const unsigned char *pchField, StrFmtContext *pContext);
typedef bool (*FieldFallback)(unsigned char **ppchResult, StrFmtContainer *pContainer, const unsigned char *pchToken, StrFmtContext *pContext);
static FieldFormat s_Formatters[128];
static FieldCondition s_Conditions[128];

static void FromListGameFormat(unsigned char **ppchResult, const unsigned char *pchToken, StrFmtContext *pContext);
static bool FromListGameCondition(unsigned char **ppchResult, const unsigned char *pchToken, StrFmtContext *pContext);

static bool EntityFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext);
static void GameStrFmtDefaults(StrFmtContext *pContext, StrFmtContainer *pContainer);

static bool ItemDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, ItemDef *pDef, const unsigned char *pchField, StrFmtContext *pContext);
static bool ItemDefConditionField(StrFmtContainer *pContainer, ItemDef *pItemDef, const unsigned char *pchField, StrFmtContext *pContext);
static bool EntItemDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext);

static bool MissionDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, MissionDef *pDef, const unsigned char *pchField, StrFmtContext *pContext);

void gamestringformat_Init(void);

// Not really threadsafe, but oh well.
static const char *s_pchFilename;

static bool g_Initialized = false;

//////////////////////////////////////////////////////////////////////////

#define STRFMT_DAYS_KEY "Days"
#define STRFMT_HOURS_KEY "Hours"
#define STRFMT_HOURS12_KEY "Hours12"
#define STRFMT_HOURS24_KEY "Hours24"
#define STRFMT_MINUTES_KEY "Minutes"
#define STRFMT_TOTAL_HOURS_KEY "TotalHours"
#define STRFMT_TOTAL_MINUTES_KEY "TotalMinutes"
#define STRFMT_TOTAL_SECONDS_KEY "TotalSeconds"
#define STRFMT_SECONDS_KEY "Seconds"
#define STRFMT_MONTH_NAME_KEY "MonthName"
#define STRFMT_MONTH_ABBR_NAME_KEY "MonthNameAbbr"
#define STRFMT_MONTH_NUMBER_KEY "MonthNumber"
#define STRFMT_DAY_KEY "Day"
#define STRFMT_YEAR_KEY "Year"
#define STRFMT_WEEKDAY_NAME_KEY "WeekdayName"

// Reserve these, since they are filled in as needed.
static const char *s_apchReserved[] = {
	STRFMT_DAYS_KEY,
	STRFMT_HOURS_KEY,
	STRFMT_HOURS12_KEY,
	STRFMT_HOURS24_KEY,
	STRFMT_MINUTES_KEY,
	STRFMT_SECONDS_KEY,
	STRFMT_MONTH_NAME_KEY,
	STRFMT_MONTH_ABBR_NAME_KEY,
	STRFMT_MONTH_NUMBER_KEY,
	STRFMT_DAY_KEY,
	STRFMT_YEAR_KEY,
	STRFMT_WEEKDAY_NAME_KEY,
	STRFMT_TOTAL_HOURS_KEY,
	STRFMT_TOTAL_MINUTES_KEY,
	STRFMT_TOTAL_SECONDS_KEY,
};

//////////////////////////////////////////////////////////////////////////
// Some bareword variables may be translated into a var/field pair, for
// legacy reasons.
static struct 
{
	const char *pchKey;
	const char *pchVar;
	const char *pchField;
} s_aSimpleFallbacks[] = {
	{ "CharName", "Entity", "Name" },
	{ "TheCharName", "Entity", "TheName" },
	{ "NemesisName", "Nemesis", "Name" },
	{ "OwnerCharName", "MapOwner", "Name" },
	{ "ClassName", "Entity", "Class" },
	{ "ItemName", "Item", "Name" },
	{ "ItemIcon", "Item", "Icon" },
	{ "ItemValue", "Item", "Value" },
	{ "Level", "Entity", "CombatLevel" },
	{ "PuppetOfType", "Entity", "Puppet" },
	{ "FormalName", "Entity", "FormalName" },
	{ "FormalFirstName", "Entity", "FormalFirstName" },
	{ "FormalLastName", "Entity", "FormalLastName" },
//////// STO specific additions /////////////

	// NOTE: Per Jira bug STO-21560 we really should NOT be adding any new entries to this section as this
	//   was supposed to be used for legacy purposes. I likely made a mistake in adding CharacterRankAlt to
	//   this section. mblattel [3Oct2011].

	// Ensign, Lt. Commander, Commander, Etc.
	{ "CharacterRank", "Entity", "StarfleetRank" },
	
	// Used for any alternate rank display name. Such as "Admiral" instead of the long form "Rear Admiral Upper Half".
	{ "CharacterRankAlt", "Entity", "StarfleetRankAlt" },

	// Informal ship name.  i.e. Enterprise
	{ "CurrentShipName", "Entity", "Puppet.Space.InformalShipName" },

	// Formal ship name. i.e. U.S.S. Enterprise
	{ "CurrentShipFormalName", "Entity", "Puppet.Space.Name" },

	// Ship registry number.  i.e. NCC-1701-D
	{ "CurrentShipRegistry", "Entity", "Puppet.Space.SubName" },

	// Ship type.  i.e. Galaxy Class Cruiser
	{ "CurrentShipType", "Entity", "Puppet.Space.Class" },

////////////////////////////////////////////
};

// List messages will format each entry in the earray with the specified message
struct {
	REF_TO(Message) hDefaultListEntry;
	REF_TO(Message) hClassListEntry;
	REF_TO(Message) hItemAlgoDescListEntry;
} s_ListMessages;
#define GetListEntryFormat(pchResult, langId, pchField, hSpecificListEntry) { \
		(pchResult) = strchr((pchField), ':'); \
		if ((pchResult)) \
			(pchResult) = langTranslateMessageKey((langId), (pchResult) + 1); \
		if (!(pchResult)) \
			(pchResult) = langTranslateMessageRef((langId), (hSpecificListEntry)); \
		if (!(pchResult)) \
			(pchResult) = langTranslateMessageRef((langId), s_ListMessages.hDefaultListEntry); \
}

static void FormatEArray(unsigned char **ppchResult, const char *pchFormat, char chType, const char *pchKey, void **ea, S32 iSelected, Language eLang, const char *pchFilename)
{
	static StrFmtContext s_context = {0};
	static S32 iDepth;
	StrFmtContainer Selected = {STRFMT_CODE_INT, 0};
	StrFmtContainer First = {STRFMT_CODE_INT, 0};
	StrFmtContainer Last = {STRFMT_CODE_INT, 0};
	StrFmtContainer Pointer = {chType, 0};
	int i;

	// Quick validation
	if (iDepth != 0)
	{
		ErrorFilenamef(pchFilename, "List formatting is not reentrant.");
		return;
	}
	iDepth++;

	if (!s_context.stArgs)
		s_context.stArgs = stashTableCreateWithStringKeys(3, StashDefault);
	s_context.langID = eLang;
	s_context.bTranslate = true;

	stashAddPointer(s_context.stArgs, "Selected", &Selected, true);
	stashAddPointer(s_context.stArgs, "First", &First, true);
	stashAddPointer(s_context.stArgs, "Last", &Last, true);
	stashAddPointer(s_context.stArgs, "ListEntry", &Pointer, true);
	stashAddPointer(s_context.stArgs, pchKey, &Pointer, true);
	GameStrFmtDefaults(&s_context, &Pointer);

	for (i = 0; i < eaSize(&ea); i++)
	{
		Selected.iValue = (i == iSelected ? 1 : 0);
		First.iValue = (i == 0 ? 1 : 0);
		Last.iValue = (i + 1 == eaSize(&ea) ? 1 : 0);
		Pointer.pValue = ea[i];
		strfmt_Format(ppchResult, pchFormat, FromListGameFormat, &s_context, FromListGameCondition, &s_context);
	}

	stashRemovePointer(s_context.stArgs, pchKey, NULL);
	iDepth--;
}

__forceinline static const unsigned char *PrettyPrintInt(S32 i, S32 iZeroPadding)
{
	static unsigned char buf[128];
	itoa_with_grouping(i, buf, 10, 3, 0, ',', '.', iZeroPadding);
	return buf;
}

#define STRIP_LEADING_DOTS(pch) while (*(pch) == '.') (pch)++

static bool IntFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, void *pDummy, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (strStartsWith(pchField, "OfficerRankAndGrade"))
	{
		char *pchFieldCopy, *pchAllegiance, *pchFormat;
		AllegianceDef *pAllegiance;
		S32 iRank, iGrade;
		strdup_alloca(pchFieldCopy, pchField + 19);
		STRIP_LEADING_DOTS(pchFieldCopy);
		pchAllegiance = strsep(&pchFieldCopy, ".");
		pchFormat = strsep(&pchFieldCopy, ".");
		if (pchAllegiance && *pchAllegiance && pchFormat && *pchFormat)
		{
			pAllegiance = allegiance_FindByName(pchAllegiance);
			if (Officer_GetRankAndGradeFromLevel(pContainer->iValue, pAllegiance, &iRank, &iGrade))
			{
				// HACK: Star Trek has the ensign rank which has no grades.
				OfficerRankDef *pPrevRankDef = iRank > 0 ? Officer_GetRankDef(iRank-1, pAllegiance, NULL) : NULL;
				OfficerRankDef *pRankDef = !pPrevRankDef || pPrevRankDef->iGradeCount != 0 || iGrade > 1 ? Officer_GetRankDef(iRank, pAllegiance, NULL) : pPrevRankDef;
				Message *pRankName = pRankDef && pRankDef->pDisplayMessage ? GET_REF(pRankDef->pDisplayMessage->hMessage) : NULL;
				if (pRankName)
				{
					langFormatGameMessageKey(pContext->langID, ppchResult, pchFormat,
						STRFMT_MESSAGE("Rank", pRankName),
						STRFMT_INT("Grade", iGrade),
						STRFMT_INT("Level", pContainer->iValue),
						STRFMT_INT("ZeroLevel", MAX(pContainer->iValue - 1, 0)),
						STRFMT_END);
					return true;
				}
			}
		}
	}

	estrAppend2(ppchResult, PrettyPrintInt(pContainer->iValue, *pchField ? atoi(pchField) : 0));
	return true;
}

__forceinline static const unsigned char *PrettyPrintFloat(F32 f, int iPlaces, bool bTruncate)
{
	static unsigned char buf[128];
	double d = pow(10, iPlaces);
	S32 i = 0;

	// round() has an FPE (overflow) if f*d is too high.
	i = (S32)round64(f * d);

	// strip trailing zeroes
	while (bTruncate && i % 10 == 0 && iPlaces > 0)
	{
		iPlaces--;
		i /= 10;
	}
	return itoa_with_grouping(i, buf, 10, 3, iPlaces, ',', '.', 0);
}

#define STRIP_LEADING_DOTS(pch) while (*(pch) == '.') (pch)++

static bool FloatFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, void *pDummy, const unsigned char *pchField, StrFmtContext *pContext)
{
	bool bTruncate = true;
	STRIP_LEADING_DOTS(pchField);
	if (strStartsWith(pchField, "Fixed"))
	{
		STRIP_LEADING_DOTS(pchField);
		pchField += 5;
		bTruncate = false;
	}
	estrAppend2(ppchResult, PrettyPrintFloat(pContainer->fValue, *pchField ? atoi(pchField) : 2, bTruncate));
	return true;
}

static bool CritterGroupFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, CritterGroup *pCritterGroup, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pCritterGroup || !*pchField)
		return false;
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pCritterGroup->descriptionMsg.hMessage));
	else
		StringFormatErrorReturn(pchField, "CritterGroup");
	return true;
}

static bool CritterDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, CritterDef *pCritterDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pCritterDef || !*pchField)
		return false;
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pCritterDef->descriptionMsg.hMessage));
	else if (strStartsWith(pchField, "Group") && GET_REF(pCritterDef->hGroup))
		return CritterGroupFormatField(ppchResult, pContainer, GET_REF(pCritterDef->hGroup), pchField + 5, pContext);
	else if (pCritterDef->pParent)
		return CritterDefFormatField(ppchResult, pContainer, pCritterDef->pParent, pchField, pContext);
	else
		StringFormatErrorReturn(pchField, "CritterDef");
	return true;
}

static bool CritterFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Critter *pCritter, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pCritter || !*pchField)
		return false;
	else
		return CritterDefFormatField(ppchResult, pContainer, GET_REF(pCritter->critterDef), pchField, pContext);
	return true;
}

static bool PuppetFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	char* pupContext = NULL;
	char* pupType;
	char* pchToken;
	Entity* puppet = NULL;
	size_t len;

	STRIP_LEADING_DOTS(pchField);

	if (!pEnt || !*pchField) {
		return false;
	}

	pupType = strdup(pchField);
	pchToken = strtok_s(pupType, ".", &pupContext);
	len = strlen(pupType);
	if(len) {
		puppet = Entity_FindCurrentOrPreferredPuppet(pEnt, (CharClassTypes)StaticDefineIntGetInt(CharClassTypesEnum, pupType));
	}
	free(pupType);
	return puppet && EntityFormatField(ppchResult, pContainer, puppet, pchField+len, pContext);
}

static bool PetContactFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	char* petContext = NULL;
	char* pchList = NULL;
	char* pchCopy = NULL;
	PetContactList* pList = NULL;
	Entity* pPet = NULL;
	CritterDef *pDef = NULL;
	CritterCostume* pCostume = NULL;
	size_t len = 0;
	REF_TO(PetContactList) hList;

	STRIP_LEADING_DOTS(pchField);

	if (!pEnt || !*pchField) {
		return false;
	}

	// Find the list
	pchCopy = strdup(pchField);
	pchList = strtok_s(pchCopy, ".", &petContext);
	len = strlen(pchList);
	if(!len) {
		free(pchCopy);
		return false;
	}

	SET_HANDLE_FROM_STRING("PetContactList", pchList, hList);


	if(!IS_HANDLE_ACTIVE(hList)) {
		REMOVE_HANDLE(hList);
		free(pchCopy);
		return false;
	}

	// Get the pet for the player
	pList = GET_REF(hList);

	PetContactList_GetPetOrCostume(pEnt, pList, NULL, &pPet, &pDef, &pCostume);

	// If no pet found, use default information
	if (!pPet)
	{
		if(!pDef) {
			REMOVE_HANDLE(hList);
			free(pchCopy);
			return true;
		}

		pchList = strtok_s(NULL, ".", &petContext);
		if(!pchList || strStartsWith(pchList, "Name")) {
			if(pCostume) {
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pCostume->displayNameMsg));
			} else if(pDef) {
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pDef->displayNameMsg));
			}

		} else if(strStartsWith(pchList, "Class")) {
			REF_TO(CharacterClass) hClass;
			CharacterClass* pClass = NULL;
			SET_HANDLE_FROM_STRING("CharacterClass", pDef->pchClass, hClass);
			pClass = GET_REF(hClass);
			REMOVE_HANDLE(hClass);

			if(pClass)
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pClass->msgDisplayName));
		}
		if(pchCopy)
			free(pchCopy);
		REMOVE_HANDLE(hList);
		return true;
	}

	if(pchCopy)
		free(pchCopy);
	REMOVE_HANDLE(hList);
	return EntityFormatField(ppchResult, pContainer, pPet, pchField+len, pContext);
}

static bool MapVarsFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, WorldVariable** ppMapVars, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!*pchField)
		return false;


	if (!ppMapVars || !eaSize(&ppMapVars))
		return true;
	else 
	{
		char* pchCopy = strdup(pchField);
		char* pchName;
		char* varContext = NULL;
		int i;

		pchName = strtok_s(pchCopy, ".", &varContext);

		for(i = eaSize(&ppMapVars)-1; i >= 0; i--) {
			if(ppMapVars[i] && !stricmp(ppMapVars[i]->pcName, pchName)) {
				switch (ppMapVars[i]->eType)
				{
					xcase WVAR_CRITTER_DEF:
					{
						CritterDef* cDef = GET_REF(ppMapVars[i]->hCritterDef);
						if(cDef) {
							estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, cDef->displayNameMsg));
						}
					}
					xcase WVAR_CRITTER_GROUP:
					{
						CritterGroup* cGroup = GET_REF(ppMapVars[i]->hCritterGroup);
						if(cGroup) {
							estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, cGroup->displayNameMsg));
						}
					}
					xcase WVAR_MESSAGE:
					{
						// Temporarily remove the current map var from the list (to avoid circular dependencies)
						WorldVariable* pMapVar = eaRemove(&ppMapVars, i);
						Message *pMessage = pMapVar ? GET_REF(pMapVar->messageVal.hMessage) : NULL;
						const char *pchFormat = pMessage ? langTranslateMessage(pContext->langID, pMessage) : NULL;

						if(pchFormat) 
						{
							strfmt_Format(ppchResult, pchFormat, FromListGameFormat, pContext, FromListGameCondition, pContext);
						}

						eaInsert(&ppMapVars, pMapVar, i);
					}
					xcase WVAR_INT:
					{
						STRIP_LEADING_DOTS(varContext);
						estrAppend2(ppchResult, PrettyPrintInt(ppMapVars[i]->iIntVal, atoi(varContext)));
					}
					xcase WVAR_FLOAT:
					{
						STRIP_LEADING_DOTS(varContext);
						estrAppend2(ppchResult, PrettyPrintFloat(ppMapVars[i]->fFloatVal, atoi(varContext), true));
					}
					xcase WVAR_STRING:
					{
						estrAppend2(ppchResult, ppMapVars[i]->pcStringVal);
					}
				}
				free(pchCopy);
				return true;
			}
		}
		free(pchCopy);
	}
	return true;

}

static bool MissionVarsFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, WorldVariableContainer** ppMissionVars, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);

	if (!*pchField)
		return false;

	if (!ppMissionVars|| !eaSize(&ppMissionVars))
		return true;
	else 
	{
		char* pchCopy = strdup(pchField);
		char* pchName;
		char* varContext = NULL;
		int i;

		pchName = strtok_s(pchCopy, ".", &varContext);

		for(i = eaSize(&ppMissionVars)-1; i >= 0; i--) {
			if(ppMissionVars[i] && !stricmp(ppMissionVars[i]->pcName, pchName)) {
				switch (ppMissionVars[i]->eType)
				{
					xcase WVAR_CRITTER_DEF:
					{
						CritterDef* cDef = GET_REF(ppMissionVars[i]->hCritterDef);
						if(cDef) {
							estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, cDef->displayNameMsg));
						}
					}
					xcase WVAR_CRITTER_GROUP:
					{
						CritterGroup* cGroup = GET_REF(ppMissionVars[i]->hCritterGroup);
						if(cGroup) {
							estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, cGroup->displayNameMsg));
						}
					}
					xcase WVAR_MESSAGE:
					{
						estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, ppMissionVars[i]->hMessage));
					}
					xcase WVAR_STRING:
					{
						estrAppend2(ppchResult, ppMissionVars[i]->pcStringVal);
					}
					xcase WVAR_INT:
					{
						STRIP_LEADING_DOTS(varContext);
						estrAppend2(ppchResult, PrettyPrintInt(ppMissionVars[i]->iIntVal, atoi(varContext)));
					}
					xcase WVAR_FLOAT:
					{
						STRIP_LEADING_DOTS(varContext);
						estrAppend2(ppchResult, PrettyPrintFloat(ppMissionVars[i]->fFloatVal, atoi(varContext), true));
					}
				}
				free(pchCopy);
				return true;
			}
		}
		free(pchCopy);
	}
	return true;
}

static bool PowerTagsFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, S32 *eaiPowerTags, const unsigned char *pchField, StrFmtContext *pContext)
{
	char* estrTagKey = NULL;
	char* pchFieldDup = NULL;
	char* pchName = NULL;
	char* varContext = NULL;
	bool bStartsWith = false;
	int i;

	STRIP_LEADING_DOTS(pchField);
	if (!eaiPowerTags || !eaiSize(&eaiPowerTags) || !*pchField)
		return true;

	pchFieldDup = strdup(pchField);
	pchName = strtok_s(pchFieldDup, ".", &varContext);

	if(strEndsWith(pchName, "*")) {
		bStartsWith = true;
		pchName[strlen(pchName)-1] = '\0';
	}

	for(i = eaiSize(&eaiPowerTags)-1; i >= 0; i--) {
		const char* pchTagName = StaticDefineIntRevLookup(PowerTagsEnum, eaiPowerTags[i]);
		bool bFound = false;
		if(!stricmp(pchTagName,"")) {
			free(pchFieldDup);
			return true;
		}
		if(bStartsWith) {
			if(strStartsWith(pchTagName, pchName))
				bFound = true;
		} else if(!stricmp(pchTagName, pchName)) {
			bFound = true;
		}

		if(bFound) {
			estrCreate(&estrTagKey);
			estrPrintf(&estrTagKey, "Powertag.Desc.%s", pchTagName);
			estrAppend2(ppchResult, langTranslateMessageKey(pContext->langID, estrTagKey));
			free(pchFieldDup);
			estrDestroy(&estrTagKey);
			return true;
		}
		estrDestroy(&estrTagKey);
	}

	free(pchFieldDup);
	return true;

}


static bool EntityFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pEnt)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, entGetLangName(pEnt, pContext->langID));
	else if (!stricmp(pchField, "TheName"))
		estrAppend2(ppchResult, entGetLangName(pEnt, pContext->langID));
	else if (!stricmp(pchField, "FormalName"))
		estrAppend2(ppchResult, FormalName_GetFullName(pEnt));
	else if (!stricmp(pchField, "FormalFirstName"))
		estrAppend2(ppchResult, FormalName_GetFirstName(pEnt));
	else if (!stricmp(pchField, "FormalLastName"))
		estrAppend2(ppchResult, FormalName_GetLastName(pEnt));
	else if (!stricmp(pchField, "AccountName"))
		estrAppend2(ppchResult, entGetAccountOrLangName(pEnt, pContext->langID));
	else if (!stricmp(pchField, "SubName"))
		estrAppend2(ppchResult, entGetLangSubName(pEnt, pContext->langID));
	else if (!stricmp(pchField, "Build.Number") && pEnt->pSaved)
		estrAppend2(ppchResult, PrettyPrintInt(pEnt->pSaved->uiIndexBuild + 1, 0));
	else if (!stricmp(pchField, "Build.Name") && pEnt->pSaved)
	{
		EntityBuild *pBuild = eaGet(&pEnt->pSaved->ppBuilds, pEnt->pSaved->uiIndexBuild);
		estrAppend2(ppchResult, pBuild ? pBuild->achName : "(null)");
	}
	else if (!stricmp(pchField, "Class"))
	{
		Message *pMessage = entGetClassNameMsg(pEnt);
		if (pMessage)
			estrAppend2(ppchResult, langTranslateMessage(pContext->langID, pMessage));
	}
	else if (!stricmp(pchField, "CombatLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(pEnt->pChar ? pEnt->pChar->iLevelCombat : 0, 0));
#ifdef GAMESERVER
	else if (strStartsWith(pchField, "Owner") && pEnt->pCritter 
		&& (pEnt->pCritter->spawningPlayer || (pEnt->pCritter->encounterData.parentEncounter && GET_REF(pEnt->pCritter->encounterData.parentEncounter->hOwner))))
	{
		Entity *pOwner = GET_REF(pEnt->pCritter->encounterData.parentEncounter->hOwner);
		if (!pOwner)
			pOwner = entFromEntityRefAnyPartition(pEnt->pCritter->spawningPlayer);
		return EntityFormatField(ppchResult, pContainer, pOwner, pchField + 5, pContext);
	} 
#endif	
	else if (strStartsWith(pchField, "Pet") && pEnt->pSaved && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER) {
		return PetContactFormatField(ppchResult, pContainer, pEnt, pchField + 3, pContext);
	}

	else if (strStartsWith(pchField, "Critter") && pEnt->pCritter)
	{
		return CritterDefFormatField(ppchResult, pContainer, GET_REF(pEnt->pCritter->critterDef), pchField + 7, pContext);
	} 
	else if(strStartsWith(pchField, "Puppet") && pEnt->pSaved && pEnt->pSaved->pPuppetMaster && eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets) > 0) 
	{
		return PuppetFormatField(ppchResult, pContainer, pEnt, pchField+6, pContext);
	}
	else if(!stricmp(pchField, "Allegiance"))
	{
		AllegianceDef *pAllegiance = GET_REF(pEnt->hAllegiance);
		if(pAllegiance)
			estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pAllegiance->displayNameMsg));
	}
	else if(!stricmp(pchField, "SubAllegiance"))
	{
		AllegianceDef *pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
		if(pSubAllegiance)
			estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pSubAllegiance->displayNameMsg));
	}
	else if(!stricmp(pchField, "Title"))
	{
		if(pEnt->pPlayer && GET_REF(pEnt->pPlayer->pTitleMsgKey))
		{
			// The title should be translated using the entity's gender
			langFormatGameString(pContext->langID, ppchResult, langTranslateMessageRef(pContext->langID, pEnt->pPlayer->pTitleMsgKey), STRFMT_PLAYER(pEnt), STRFMT_INT("ItemPowerFactor", 0), STRFMT_END);
		}
	}
	else if(!stricmp(pchField, "Species") || !stricmp(pchField, "Race"))
	{
		if (pEnt && pEnt->pChar)
		{		
			SpeciesDef *pSpecies = GET_REF(pEnt->pChar->hSpecies);
			if(pSpecies)
			{
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pSpecies->displayNameMsg));
			}
		}
	}
	else if(!stricmp(pchField, "SpeciesGender"))
	{
		if (pEnt && pEnt->pChar)
		{		
			SpeciesDef *pSpecies = GET_REF(pEnt->pChar->hSpecies);
			if(pSpecies)
			{
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pSpecies->genderNameMsg));
			}
		}
	}
	else if (!stricmp(pchField, "Path"))
	{
		if (pEnt && pEnt->pChar)
		{
			int i;
			CharacterPath** eaPaths = NULL;
			eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
			entity_GetChosenCharacterPaths(pEnt, &eaPaths);
			
			for(i = 0; i < eaSize(&pEnt->pChar->ppSecondaryPaths); i++)
			{
				if (i < 0)
					estrAppend2(ppchResult, ", ");
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, eaPaths[i]->pDisplayName));
			}
		}
	}

/////// STO specific additions /////////
	else if(!stricmp(pchField, "StarfleetRank"))
	{
		AllegianceDef *pAllegiance = GET_REF(pEnt->hAllegiance);
		AllegianceDef *pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
		OfficerRankDef* pRank = Officer_GetRankDef(Officer_GetRank(pEnt), pAllegiance, pSubAllegiance);
		if(pRank && pRank->pDisplayMessage) {
			estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, *(pRank->pDisplayMessage)));
		}
	}
	else if(!stricmp(pchField, "StarfleetRankAlt"))
	{
		AllegianceDef *pAllegiance = GET_REF(pEnt->hAllegiance);
		AllegianceDef *pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
		OfficerRankDef* pRank = Officer_GetRankDef(Officer_GetRank(pEnt), pAllegiance, pSubAllegiance);
		if (pRank)
		{
			if(pRank->pDisplayMsgAlt)
			{
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, *(pRank->pDisplayMsgAlt)));
			}
			else if(pRank->pDisplayMessage)
			{
				estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, *(pRank->pDisplayMessage)));
			}
		}
	}
	else if(!stricmp(pchField, "InformalShipName"))
	{
		const char* shipName = entGetLangName(pEnt, pContext->langID);
		if(strStartsWith(shipName, "U.S.S. "))
		{
			estrAppend2(ppchResult, shipName+7);
		}
		else if(strStartsWith(shipName, "I.K.S. "))
		{
			estrAppend2(ppchResult, shipName+7);
		}
		else
		{
			estrAppend2(ppchResult, shipName);
		}
	} 
	else if(!stricmp(pchField, "DiplomacyRank"))
	{
		AllegianceDef *pAllegiance = GET_REF(pEnt->hAllegiance);
		AllegianceDef *pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
		OfficerRankDef* pRank = Officer_GetRankDefUsingNumeric(Officer_GetRankUsingNumeric(pEnt, "Diplomacy_Rank"), pAllegiance, pSubAllegiance, "Diplomacy_Rank");
		if(pRank && pRank->pDisplayMessage) {
			estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, *(pRank->pDisplayMessage)));
		}
	}
	else if (strStartsWith(pchField, "Mission["))
	{
		const char* pchStart = pchField+8;
		const char* pchEndBrace = strchr(pchStart, ']');
		if (pchEndBrace)
		{
			MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pEnt);
			if (pMissionInfo)
			{
				Mission* pMission;
				char pchFindName[MAX_PATH];
				strncpy_s(pchFindName, pchEndBrace-pchStart+1, pchStart, pchEndBrace-pchStart);

				pMission = mission_GetMissionByName(pMissionInfo, pchFindName);
				if (pMission)
				{
					if (strStartsWith(pchEndBrace+1, ".Var["))
					{
						pchStart = pchEndBrace+6;
						pchEndBrace = strchr(pchStart, ']');
						if (pchEndBrace)
						{
							WorldVariable* pVariable;
							strncpy_s(pchFindName, pchEndBrace-pchStart+1, pchStart, pchEndBrace-pchStart);
							pVariable = eaIndexedGetUsingString(&pMission->eaMissionVariables, pchFindName);
							if (pVariable)
							{
								switch (pVariable->eType)
								{
									xcase WVAR_STRING:
									{
										estrAppend2(ppchResult, pVariable->pcStringVal);
									}
									xcase WVAR_MESSAGE:
									{
										estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pVariable->messageVal));
									}
									xcase WVAR_INT:
									{
										int iZeroPadding = 0;
										const char* pchCheckZeroPadding = pchEndBrace+1;
										if (*pchCheckZeroPadding == '.')
											iZeroPadding = atoi(pchCheckZeroPadding+1);
										estrAppend2(ppchResult, PrettyPrintInt(pVariable->iIntVal, iZeroPadding));
									}
									xcase WVAR_FLOAT:
									{
										int iPlaces = 2;
										const char* pchCheckPlaces = pchEndBrace+1;
										if (*pchCheckPlaces == '.')
											iPlaces = atoi(pchCheckPlaces+1);
										estrAppend2(ppchResult, PrettyPrintFloat(pVariable->fFloatVal, iPlaces, true));
									}
								}
							}
						}
					}
				}
			}
		}
	}
///////////////////////////////////////
	else
		StringFormatErrorReturn(pchField, "Entity");
	return true;
}

static bool IntConditionField(StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	return strfmt_NumericCondition(pContainer->iValue, pchField, s_pchFilename);
}

static bool FloatConditionField(StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	return strfmt_NumericCondition(pContainer->fValue, pchField, s_pchFilename);
}

static bool StringConditionField(StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	// Super-simple String conditional - if it isn't null or empty the result is true
	return (pContainer->pchValue && *pContainer->pchValue);
}

static bool StructConditionField(StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	if(!pchField || !pchField[0])
	{
		// Super-simple Struct conditional - if it isn't null the result is true
		return !!pContainer->pValue;
	}
	else
	{
		void *pInnerStruct;
		ParseTable *pInnerTable;
		S32 iIndex;
		S32 iColumn;
		char *pchPath;
		estrStackCreate(&pchPath);
		estrConcatChar(&pchPath,'.');
		estrAppend2(&pchPath,pchField);
		if(objPathResolveField(pchPath, pContainer->pTable, pContainer->pValue, &pInnerTable, &iColumn, &pInnerStruct, &iIndex, OBJPATHFLAG_TRAVERSEUNOWNED))
		{
			// If we managed to xpath to the field, we consider that success, regardless of the value of the field
			//  This is used to test for the existence of elements of an indexed earray, such as
			//  {a.b['c']?C exists|C does not exist}
			estrDestroy(&pchPath);
			return true;
		}
		else
		{
			estrDestroy(&pchPath);
			return false;
		}
	}
}

static bool StaticDefineIntConditionField(StrFmtContainer *pContainer, int iDefine, StaticDefineInt *pEnum, const unsigned char *pchField, StrFmtContext *pContext)
{
	int iLowerBound = INT_MAX;
	int iUpperBound = INT_MIN;
	bool bAnd = true;
	int iValue = 0;

	while (*pchField && isspace(*pchField)) pchField++;

	switch (*pchField)
	{
	case '!':
		pchField++;
		if (*(pchField+1) != '=')
			return false;
		iUpperBound = iDefine - 1;
		iLowerBound = iDefine + 1;
		bAnd = false;
		break;
	case '>':
		pchField++;
		iUpperBound = INT_MAX;
		if (*(pchField+1) == '=')
			iLowerBound = iDefine;
		else
			iLowerBound = iDefine + 1;
		break;
	case '<':
		pchField++;
		iLowerBound = INT_MIN;
		if (*(pchField+1) == '=')
			iUpperBound = iDefine;
		else
			iUpperBound = iDefine - 1;
		break;
	case '=':
		pchField++;
	default:
		iUpperBound = iDefine;
		iLowerBound = iDefine;
		break;
	}

	if (*pchField == '=')
		pchField++;

	while (*pchField && isspace(*pchField)) pchField++;

	iValue = StaticDefineInt_FastStringToInt(pEnum, pchField, INT_MIN);
	if (iValue == INT_MIN)
		iValue = atoi(pchField);

	return bAnd && (iValue <= iUpperBound && iValue >= iLowerBound) || !bAnd && (iValue <= iUpperBound || iValue >= iLowerBound);
}

static bool EntityConditionField(StrFmtContainer *pContainer, Entity *pEnt, const unsigned char *pchField, StrFmtContext *pContext)
{
	// FIXME: for speed reasons, this is the lamest parser ever -- it accepts e.g. "entity.gender male !=".
	STRIP_LEADING_DOTS(pchField);
	if (!pEnt)
		return false;
	else if (strStartsWith(pchField, "gender"))
	{
		if (strstr(pchField, "!="))
		{
			if (strstri(pchField, "female"))
				return pEnt->eGender != Gender_Female;
			else if (strstri(pchField, "male"))
				return pEnt->eGender != Gender_Male;
			else
				return pEnt->eGender != Gender_Neuter;
		}
		else if (strchr(pchField, '='))
		{
			if (strstri(pchField, "female"))
				return pEnt->eGender == Gender_Female;
			else if (strstri(pchField, "male"))
				return pEnt->eGender == Gender_Male;
			else
				return pEnt->eGender == Gender_Neuter;
		}
	}

	StringConditionErrorReturn(pchField, "Entity");
}

static bool UsageRestrictionFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, UsageRestriction *pRestriction, const unsigned char *pchField, StrFmtContext *pContext)
{
	int i;
	STRIP_LEADING_DOTS(pchField);
	if (!pRestriction)
		return false;
	else if (!stricmp(pchField, "MinLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(pRestriction->iMinLevel, 0));
	else if (!stricmp(pchField, "MaxLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(pRestriction->iMaxLevel, 0));
	else if (!stricmp(pchField, "SkillType") && StaticDefineGetMessage(SkillTypeEnum, pRestriction->eSkillType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(SkillTypeEnum, pRestriction->eSkillType)));
	else if (!stricmp(pchField, "SkillType.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(SkillTypeEnum, pRestriction->eSkillType));
	else if (!stricmp(pchField, "SkillLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(pRestriction->iSkillLevel, 0));
	else if (strStartsWith(pchField, "Classes"))
	{
		static CharacterClass **s_eaClasses;
		const char *pchElementMessage = NULL;
		Entity *pEntity = (pContainer->chType == STRFMT_CODE_ENTITEM || pContainer->chType == STRFMT_CODE_ENTITEMDEF) ? pContainer->pValue : NULL;
		GetListEntryFormat(pchElementMessage, pContext->langID, pchField, s_ListMessages.hClassListEntry);
		if (pchElementMessage)
		{
			eaClear(&s_eaClasses);
			for (i = 0; i < eaSize(&pRestriction->ppCharacterClassesAllowed); i++) {
				if (GET_REF(pRestriction->ppCharacterClassesAllowed[i]->hClass))
					eaPush(&s_eaClasses, GET_REF(pRestriction->ppCharacterClassesAllowed[i]->hClass));
			}
			FormatEArray(ppchResult,
				pchElementMessage,
				STRFMT_CODE_CHARACTERCLASS, "Class", s_eaClasses,
				pEntity && pEntity->pChar ? eaFind(&s_eaClasses, GET_REF(pEntity->pChar->hClass)) : -1,
				pContext->langID, s_pchFilename);
		}
	}
	else if (!stricmp(pchField, "UICategory") && StaticDefineGetMessage(UsageRestrictionCategoryEnum, pRestriction->eUICategory))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(UsageRestrictionCategoryEnum, pRestriction->eUICategory)));
	else if (!stricmp(pchField, "UICategory.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(UsageRestrictionCategoryEnum, pRestriction->eUICategory));
	//TODO: Add "ClassCategory"
	else
		StringFormatErrorReturn(pchField, "UsageRestriction");
	return true;
}

static bool UsageRestrictionConditionField(StrFmtContainer *pContainer, UsageRestriction *pRestriction, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pRestriction)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "MinLevel"))
		return strfmt_NumericCondition(pRestriction->iMinLevel, pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "MaxLevel"))
		return strfmt_NumericCondition(pRestriction->iMaxLevel, pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "SkillType"))
		return StaticDefineIntConditionField(pContainer, pRestriction->eSkillType, SkillTypeEnum, pchField + 9, pContext);
	else if (strStartsWith(pchField, "SkillLevel"))
		return strfmt_NumericCondition(pRestriction->iSkillLevel, pchField + 10, s_pchFilename);
	else if (strStartsWith(pchField, "Classes"))
		return strfmt_NumericCondition(eaSize(&pRestriction->ppCharacterClassesAllowed), pchField + 7, s_pchFilename);
	else if (strStartsWith(pchField, "ClassCategories"))
		return strfmt_NumericCondition(eaiSize(&pRestriction->peClassCategoriesAllowed), pchField + 15, s_pchFilename);
	StringConditionErrorReturn(pchField, "UsageRestriction");
}

static bool EntUsageRestrictionConditionField(StrFmtContainer *pContainer, Entity *pEntity, Item *pItem, ItemDef *pItemDef, UsageRestriction *pRestriction, const unsigned char *pchField, StrFmtContext *pContext)
{
	MultiVal mv = {0};
	int i;
	STRIP_LEADING_DOTS(pchField);

	if (!stricmp(pchField, "Entity.LevelInRange"))
	{
		int iEntLevel = entity_GetSavedExpLevel(pEntity);
		if (pItem)
		{
			return (pItemDef->flags & kItemDefFlag_NoMinLevel) ||
				!(pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought)) ||
				!item_GetMinLevel(pItem) ||
				item_GetMinLevel(pItem) <= iEntLevel;
		}
		if (pRestriction)
		{
			S32 iMinLevel = pRestriction->iMinLevel;
			if (pItem && item_GetMinLevel(pItem) > iMinLevel && pItemDef->flags & (kItemDefFlag_LevelFromSource | kItemDefFlag_ScaleWhenBought))
				iMinLevel = item_GetMinLevel(pItem);
			if (pItemDef->flags & kItemDefFlag_NoMinLevel)
				iMinLevel = -1;
			if (iMinLevel > 0)
				return iEntLevel >= iMinLevel && (pRestriction->iMaxLevel <= iMinLevel || iEntLevel <= pRestriction->iMaxLevel);
			else if (pRestriction->iMaxLevel > 0)
				return iEntLevel <= pRestriction->iMaxLevel;
		}
		return true;
	}
	else if (!pRestriction)
		// need to have a restriction beyond this point, if no restriction is
		// set, then it's assumed true
		return true;
	else if (!stricmp(pchField, "Entity.SkillInRange"))
	{
		SkillType eEntSkill = entity_GetSkill(pEntity);
		return pRestriction->eSkillType != kSkillType_None && pRestriction->eSkillType == eEntSkill && pRestriction->iSkillLevel <= (U32)inv_GetNumericItemValue(pEntity, "SkillLevel");
	}
	else if (!stricmp(pchField, "Entity.ClassInSet"))
	{
		if (!pEntity || !pEntity->pChar)
			return false;
		for (i = eaSize(&pRestriction->ppCharacterClassesAllowed) - 1; i >= 0; i--)
			if (REF_COMPARE_HANDLES(pRestriction->ppCharacterClassesAllowed[i]->hClass, pEntity->pChar->hClass))
				return true;
		return eaSize(&pRestriction->ppCharacterClassesAllowed) == 0;
	}
	else if (!stricmp(pchField, "Entity.ClassCategoryInSet"))
	{
		CharacterClass* pClass;
		if (!pEntity || !pEntity->pChar)
			return false;
		if (pClass = GET_REF(pEntity->pChar->hClass))
		{
			for (i = eaiSize(&pRestriction->peClassCategoriesAllowed) - 1; i >= 0; i--)
				if (pClass->eCategory == pRestriction->peClassCategoriesAllowed[i])
					return true;
		}
		return eaiSize(&pRestriction->peClassCategoriesAllowed) == 0;
	}
	else if (!stricmp(pchField, "Entity.Usable"))
	{
		if (!pRestriction->pRequires || !pEntity)
			return true;
		itemeval_Eval(entGetPartitionIdx(pEntity), pRestriction->pRequires, pItemDef, NULL, pItem, pEntity, item_GetLevel(pItem), item_GetQuality(pItem), 0, pItemDef->pchFileName, -1, &mv);
		return itemeval_GetIntResult(&mv,pItemDef->pchFileName,pRestriction->pRequires);
	}
	else
		return UsageRestrictionConditionField(pContainer, pRestriction, pchField, pContext);
}

static bool ItemCraftingTableFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, ItemCraftingTable *pCraft, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pCraft)
		return false;
	else if (strStartsWith(pchField, "Result"))
	{
		ItemDef *pResultDef = GET_REF(pCraft->hItemResult);
		if (pResultDef)
			return ItemDefFormatField(ppchResult, pContainer, pResultDef, pchField + 6, pContext);
		else
			return true;
	}
	else
		StringFormatErrorReturn(pchField, "ItemCraftingTable");
}

static bool ItemCraftingTableConditionField(StrFmtContainer *pContainer, ItemCraftingTable *pCraft, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pCraft)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "Result"))
		return ItemDefConditionField(pContainer, GET_REF(pCraft->hItemResult), pchField + 6, pContext);
	else if (!stricmp(pchField, "Algorithmic") && GET_REF(pCraft->hItemResult))
		return GET_REF(pCraft->hItemResult)->Group != 0;
	else
		StringConditionErrorReturn(pchField, "ItemCraftingTable");
}

static bool ItemDefFlagsConditionField(StrFmtContainer *pContainer, ItemDef *pItemDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	int flag;
	STRIP_LEADING_DOTS(pchField);

	// FIXME(jm): This breaks the original purpose of this file
	flag = StaticDefineIntGetInt(ItemDefFlagEnum, pchField);
	if (flag == -1)
		return false;
	return (pItemDef->flags & flag) == flag;
}

static bool ItemValueFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, S32 iValue, const unsigned char *pchField, StrFmtContext *pContext)
{
	int iUnit1 = iValue % 100;
	int iUnit100 = (iValue / 100) % 100;
	int iUnit10000 = iValue / 100 / 100;

	if (strStartsWith(pchField, "Numeric3"))
	{
		pchField += 8;
		STRIP_LEADING_DOTS(pchField);
	}
	else if (strStartsWith(pchField, "Numeric2"))
	{
		pchField += 8;
		STRIP_LEADING_DOTS(pchField);
		iUnit100 = iValue / 100;
		iUnit10000 = 0;
	}

	if (strStartsWith(pchField, "Unit10000"))
	{
		pchField += 9;
		STRIP_LEADING_DOTS(pchField);
		estrAppend2(ppchResult, PrettyPrintInt(iUnit10000, atoi(pchField)));
	}
	else if (strStartsWith(pchField, "Unit100"))
	{
		pchField += 7;
		STRIP_LEADING_DOTS(pchField);
		estrAppend2(ppchResult, PrettyPrintInt(iUnit100, atoi(pchField)));
	}
	else if (strStartsWith(pchField, "Unit1"))
	{
		pchField += 5;
		STRIP_LEADING_DOTS(pchField);
		estrAppend2(ppchResult, PrettyPrintInt(iUnit1, atoi(pchField)));
	}
	else
	{
		estrAppend2(ppchResult, PrettyPrintInt(iValue, atoi(pchField)));
	}

	return true;
}

static bool ItemValueConditionField(StrFmtContainer *pContainer, S32 iValue, const unsigned char *pchField, StrFmtContext *pContext)
{
	int iUnit1 = iValue % 100;
	int iUnit100 = (iValue / 100) % 100;
	int iUnit10000 = iValue / 100 / 100;

	if (strStartsWith(pchField, "Numeric3"))
	{
		pchField += 8;
		STRIP_LEADING_DOTS(pchField);
	}
	else if (strStartsWith(pchField, "Numeric2"))
	{
		pchField += 8;
		STRIP_LEADING_DOTS(pchField);
		iUnit100 = iValue / 100;
		iUnit10000 = 0;
	}

	if (strStartsWith(pchField, "Unit10000"))
		return strfmt_NumericCondition(iUnit10000, pchField + 9, s_pchFilename);
	else if (strStartsWith(pchField, "Unit100"))
		return strfmt_NumericCondition(iUnit100, pchField + 7, s_pchFilename);
	else if (strStartsWith(pchField, "Unit1"))
		return strfmt_NumericCondition(iUnit1, pchField + 5, s_pchFilename);
	else
		return strfmt_NumericCondition(iValue, pchField, s_pchFilename);
}

static bool EntItemValueFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEnt, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	char *pchResources = "Resources";
	S32 iValue = 0;
	int iPartitionIdx;
	STRIP_LEADING_DOTS(pchField);

	if (*pchField && !strStartsWith(pchField, "Numeric3") && !strStartsWith(pchField, "Numeric2"))
	{
		char *pchPeriod = strchr(pchField, '.');
		char *pchSpace = strchr(pchField, ' ');
		S32 len = 0;
		if (pchPeriod != NULL && pchSpace != NULL)
			len = (S32)(MIN(pchPeriod, pchSpace) - pchField);
		else if (pchPeriod != NULL || pchSpace != NULL)
			len = (S32)(MAX(pchPeriod, pchSpace) - pchField);
		else
			len = (S32)strlen(pchField);
		pchResources = alloca(len + 1);
		strncpy_s(pchResources, len + 1, pchField, len);

		// Skip resource name
		pchField += len;
		STRIP_LEADING_DOTS(pchField);
	}

	iPartitionIdx = (pEnt ? entGetPartitionIdx(pEnt) : PARTITION_UNINITIALIZED);
	iValue = item_GetResourceValue(iPartitionIdx, pEnt, pItem, pchResources);
	return ItemValueFormatField(ppchResult, pContainer, iValue, pchField, pContext);
}

static bool EntItemDefValueFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEnt, ItemDef *pItemDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	char *pchResources = "Resources";
	ItemDef *pResources = NULL;
	S32 iResourcesValue = 0;
	S32 iValue = 0;
	int iPartitionIdx;
	STRIP_LEADING_DOTS(pchField);

	if (*pchField && !strStartsWith(pchField, "Numeric3") && !strStartsWith(pchField, "Numeric2"))
	{
		char *pchPeriod = strchr(pchField, '.');
		char *pchSpace = strchr(pchField, ' ');
		S32 len = 0;
		if (pchPeriod != NULL && pchSpace != NULL)
			len = (S32)(MIN(pchPeriod, pchSpace) - pchField);
		else if (pchPeriod != NULL || pchSpace != NULL)
			len = (S32)(MAX(pchPeriod, pchSpace) - pchField);
		else
			len = (S32)strlen(pchField);
		pchResources = alloca(len + 1);
		strncpy_s(pchResources, len + 1, pchField, len);

		// Skip resource name
		pchField += len;
		STRIP_LEADING_DOTS(pchField);
	}

	if (pchResources && *pchResources)
		pResources = item_DefFromName(pchResources);

	iPartitionIdx = (pEnt ? entGetPartitionIdx(pEnt) : PARTITION_UNINITIALIZED);

	if (pResources)
		iResourcesValue = item_GetDefEPValue(iPartitionIdx, pEnt, pResources, pResources->iLevel, pResources->Quality);

	iValue = item_GetDefEPValue(iPartitionIdx, pEnt, pItemDef, pItemDef->iLevel, pItemDef->Quality);
	if (iResourcesValue)
		iValue /= iResourcesValue;

	return ItemValueFormatField(ppchResult, pContainer, iValue, pchField, pContext);
}

static bool EntItemValueConditionField(StrFmtContainer *pContainer, Entity *pEnt, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	char *pchResources = "Resources";
	S32 iValue = 0;
	int iPartitionIdx;
	STRIP_LEADING_DOTS(pchField);

	if (*pchField && !strStartsWith(pchField, "Numeric3") && !strStartsWith(pchField, "Numeric2"))
	{
		char *pchPeriod = strchr(pchField, '.');
		char *pchSpace = strchr(pchField, ' ');
		S32 len = 0;
		if (pchPeriod != NULL && pchSpace != NULL)
			len = (S32)(MIN(pchPeriod, pchSpace) - pchField);
		else if (pchPeriod != NULL || pchSpace != NULL)
			len = (S32)(MAX(pchPeriod, pchSpace) - pchField);
		else
			len = (S32)strlen(pchField);
		pchResources = alloca(len + 1);
		strncpy_s(pchResources, len + 1, pchField, len);

		// Skip resource name
		pchField += len;
		STRIP_LEADING_DOTS(pchField);
	}

	iPartitionIdx = (pEnt ? entGetPartitionIdx(pEnt) : PARTITION_UNINITIALIZED);
	iValue = item_GetResourceValue(iPartitionIdx, pEnt, pItem, pchResources);
	return ItemValueConditionField(pContainer, iValue, pchField, pContext);
}

static bool EntItemDefValueConditionField(StrFmtContainer *pContainer, Entity *pEnt, ItemDef *pItemDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	char *pchResources = "Resources";
	ItemDef *pResources = NULL;
	S32 iResourcesValue = 0;
	S32 iValue = 0;
	int iPartitionIdx;
	STRIP_LEADING_DOTS(pchField);

	if (*pchField && !strStartsWith(pchField, "Numeric3") && !strStartsWith(pchField, "Numeric2"))
	{
		char *pchPeriod = strchr(pchField, '.');
		char *pchSpace = strchr(pchField, ' ');
		S32 len = 0;
		if (pchPeriod != NULL && pchSpace != NULL)
			len = (S32)(MIN(pchPeriod, pchSpace) - pchField);
		else if (pchPeriod != NULL || pchSpace != NULL)
			len = (S32)(MAX(pchPeriod, pchSpace) - pchField);
		else
			len = (S32)strlen(pchField);
		pchResources = alloca(len + 1);
		strncpy_s(pchResources, len + 1, pchField, len);

		// Skip resource name
		pchField += len;
		STRIP_LEADING_DOTS(pchField);
	}

	if (pchResources && *pchResources)
		pResources = item_DefFromName(pchResources);

	iPartitionIdx = (pEnt ? entGetPartitionIdx(pEnt) : PARTITION_UNINITIALIZED);

	if (pResources)
		iResourcesValue = item_GetDefEPValue(iPartitionIdx, pEnt, pResources, pResources->iLevel, pResources->Quality);

	iValue = item_GetDefEPValue(iPartitionIdx, pEnt, pItemDef, pItemDef->iLevel, pItemDef->Quality);
	if (iResourcesValue)
		iValue /= iResourcesValue;

	return ItemValueConditionField(pContainer, iValue, pchField, pContext);
}

static bool ItemDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, ItemDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, itemdef_GetNameLang(NULL, pDef, pContext->langID, pContainer->chType == STRFMT_CODE_ENTITEM || pContainer->chType == STRFMT_CODE_ENTITEMDEF ? pContainer->pValue : NULL));
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->descriptionMsg.hMessage));
	else if (!stricmp(pchField, "ShortDescription"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->descShortMsg.hMessage));
	else if (!stricmp(pchField, "Icon"))
		estrAppend2(ppchResult, pDef->pchIconName);
	else if (!stricmp(pchField, "Tag") && StaticDefineGetMessage(ItemTagEnum, pDef->eTag))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemTagEnum, pDef->eTag)));
	else if (!stricmp(pchField, "Tag.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemTagEnum, pDef->eTag));
	else if (!stricmp(pchField, "Type") && StaticDefineGetMessage(ItemTypeEnum, pDef->eType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemTypeEnum, pDef->eType)));
	else if (!stricmp(pchField, "Type.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemTypeEnum, pDef->eType));
	else if (!stricmp(pchField, "BagSlots"))
		estrAppend2(ppchResult, PrettyPrintInt(pDef->iNumBagSlots, 0));
	else if (!stricmp(pchField, "Quality") && StaticDefineGetMessage(ItemQualityEnum, pDef->Quality))
		// FIXME(jm): The fake item quality message will never return a non-null message.
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemQualityEnum, pDef->Quality)));
	else if (!stricmp(pchField, "Quality.Name") && StaticDefineGetMessage(ItemQualityEnum, pDef->Quality))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemQualityEnum, pDef->Quality));
	else if (!stricmp(pchField, "MinLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(SAFE_MEMBER2(pDef, pRestriction, iMinLevel), 0));
	else if (!stricmp(pchField, "MaxLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(SAFE_MEMBER2(pDef, pRestriction, iMaxLevel), 0));
	else if (!stricmp(pchField, "StackLimit"))
		estrAppend2(ppchResult, PrettyPrintInt(pDef->iStackLimit, 0));
	else if (!stricmp(pchField, "SkillType") && StaticDefineGetMessage(SkillTypeEnum, pDef->kSkillType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(SkillTypeEnum, pDef->kSkillType)));
	else if (!stricmp(pchField, "SkillType.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(SkillTypeEnum, pDef->kSkillType));
	else if (!stricmp(pchField, "Bag1") && StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,0)))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,0))));
	else if (!stricmp(pchField, "Bag1.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,0)));
	else if (!stricmp(pchField, "Bag2") && StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,1)))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,1))));
	else if (!stricmp(pchField, "Bag2.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(InvBagIDsEnum, eaiGet(&pDef->peRestrictBagIDs,1)));
	else if (strStartsWith(pchField, "Bag["))
	{
		int len = (int)strcspn(pchField+4, "]");
		char* pchBuffer = alloca(len+1);
		strncpy_s(pchBuffer, len+1, pchField+4, len);
		if (strIsNumeric(pchBuffer))
		{
			InvBagIDs eBagID = eaiGet(&pDef->peRestrictBagIDs, atoi(pchBuffer));
			if (strEndsWith(pchField, ".Name"))
				estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(InvBagIDsEnum, eBagID));
			else
				estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(InvBagIDsEnum, eBagID)));
		}
	}
	else if (!stricmp(pchField, "Slot") && StaticDefineGetMessage(SlotTypeEnum, pDef->eRestrictSlotType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(SlotTypeEnum, pDef->eRestrictSlotType)));
	else if (!stricmp(pchField, "Slot.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(SlotTypeEnum, pDef->eRestrictSlotType));
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueFormatField(ppchResult, pContainer, NULL, pDef, pchField + 5, pContext);
	else if (strStartsWith(pchField, "Recipe") || strStartsWith(pchField, "Craft"))
	{
		ItemCraftingTable *pCraft = pDef->pCraft;
		if (pCraft && strStartsWith(pchField, "Recipe"))
			return ItemCraftingTableFormatField(ppchResult, pContainer, pCraft, pchField + 6, pContext);
		else if (pCraft)
		{
			// TODO(jm): deprecate the 2 uses in Champions.
			ErrorFilenamef(s_pchFilename, "Uses old {Item.Craft...}, it should get updated to {Item.Recipe...}.");
			return ItemCraftingTableFormatField(ppchResult, pContainer, pCraft, pchField + 5, pContext);
		}
		else
			return true;
	}
	else if (strStartsWith(pchField, "Restriction"))
	{
		UsageRestriction *pRestriction = pDef->pRestriction;
		if (pRestriction)
			return UsageRestrictionFormatField(ppchResult, pContainer, pRestriction, pchField + 11, pContext);
		else
			return true;
	}
	else if (strStartsWith(pchField, "Mission"))
	{
		MissionDef *pMissionDef = GET_REF(pDef->hMission);
		if (pMissionDef)
			return MissionDefFormatField(ppchResult, pContainer, pMissionDef, pchField + 7, pContext);
		else
			return true;
	}
	else
		StringFormatErrorReturn(pchField, "ItemDef");
	return true;
}

static bool ItemDefConditionField(StrFmtContainer *pContainer, ItemDef *pItemDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	const char *pch;
	STRIP_LEADING_DOTS(pchField);
	if (!pItemDef)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "Name"))
		return (pch = langTranslateDisplayMessage(pContext->langID, pItemDef->displayNameMsgUnidentified)) != NULL && *pch != '\0';
	else if (strStartsWith(pchField, "Description"))
		return (pch = langTranslateDisplayMessage(pContext->langID, pItemDef->descriptionMsg)) != NULL && *pch != '\0';
	else if (strStartsWith(pchField, "ShortDescription"))
		return (pch = langTranslateDisplayMessage(pContext->langID, pItemDef->descShortMsg)) != NULL && *pch != '\0';
	else if (strStartsWith(pchField, "Type"))
		return StaticDefineIntConditionField(pContainer, pItemDef->eType, ItemTypeEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Quality"))
		return StaticDefineIntConditionField(pContainer, pItemDef->Quality, ItemQualityEnum, pchField + 7, pContext);
	else if (strStartsWith(pchField, "Flags"))
		return ItemDefFlagsConditionField(pContainer, pItemDef, pchField + 5, pContext);
	else if (strStartsWith(pchField, "SkillType"))
		return StaticDefineIntConditionField(pContainer, pItemDef->kSkillType, SkillTypeEnum, pchField + 9, pContext);
	else if (!stricmp(pchField, "MissionGrant"))
		return item_IsMissionGrant(pItemDef);
	else if (!stricmp(pchField, "MissionItem"))
		return item_IsMission(pItemDef);
	else if (strStartsWith(pchField, "MinLevel"))
		return strfmt_NumericCondition(SAFE_MEMBER2(pItemDef, pRestriction, iMinLevel), pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "MaxLevel"))
		return strfmt_NumericCondition(SAFE_MEMBER2(pItemDef, pRestriction, iMaxLevel), pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "StackLimit"))
		return strfmt_NumericCondition(pItemDef->iStackLimit, pchField + 10, s_pchFilename);
	else if (!stricmp(pchField, "CostumeUnlock"))
		return pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock && eaSize(&pItemDef->ppCostumes) > 0;
	else if (!stricmp(pchField, "Recipe"))
		return item_IsRecipe(pItemDef);
	else if (strStartsWith(pchField, "Tag"))
		return StaticDefineIntConditionField(pContainer, pItemDef->eTag, ItemTagEnum, pchField + 3, pContext);
	else if (strStartsWith(pchField, "Recipe"))
		return ItemCraftingTableConditionField(pContainer, pItemDef->pCraft, pchField + 6, pContext);
	else if (strStartsWith(pchField, "Restriction"))
		return UsageRestrictionConditionField(pContainer, pItemDef->pRestriction, pchField + 11, pContext);
	else if (strStartsWith(pchField, "BagSlots"))
		return strfmt_NumericCondition(pItemDef->iNumBagSlots, pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "Bag1")) 
		return StaticDefineIntConditionField(pContainer, eaiGet(&pItemDef->peRestrictBagIDs,0), InvBagIDsEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Bag2"))
		return StaticDefineIntConditionField(pContainer, eaiGet(&pItemDef->peRestrictBagIDs,1), InvBagIDsEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Bag["))
	{
		int len = (int)strcspn(pchField+4, "]");
		char* pchBuffer = alloca(len+1);
		strncpy_s(pchBuffer, len+1, pchField+4, len);
		if (strIsNumeric(pchBuffer))
		{
			InvBagIDs eBagID = eaiGet(&pItemDef->peRestrictBagIDs, atoi(pchBuffer));
			return StaticDefineIntConditionField(pContainer, eBagID, InvBagIDsEnum, pchField + len + 5, pContext);
		}
	}
	else if (strStartsWith(pchField, "Slot"))
		return StaticDefineIntConditionField(pContainer, pItemDef->eRestrictSlotType, SlotTypeEnum, pchField + 4, pContext);
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueConditionField(pContainer, NULL, pItemDef, pchField + 5, pContext);
	StringConditionErrorReturn(pchField, "ItemDef");
}

static bool EntItemDefConditionField(StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	Item *pItem = pContainer->chType == STRFMT_CODE_ENTITEM ? pContainer->pValue2 : NULL;
	ItemDef *pItemDef = pContainer->chType == STRFMT_CODE_ENTITEMDEF ? pContainer->pValue2 : pItem ? GET_REF(pItem->hItem) : NULL;
	const char *pchUsage = pContainer->pchValue;
	int i;

	STRIP_LEADING_DOTS(pchField);
	if (!pItemDef)
		return false;
	else if (!*pchField)
		return true;
	else if (!pEntity)
		return ItemDefConditionField(pContainer, pItemDef, pchField, pContext);
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueConditionField(pContainer, pEntity, pItemDef, pchField + 5, pContext);
	else if (!stricmp(pchField, "Recipe.New"))
	{
		GameAccountDataExtract *pExtract;
		bool bResult;

		if (!item_IsRecipe(pItemDef))
			return false;

		pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		bResult = inv_ent_CountItems(pEntity, InvBagIDs_Recipe, pItemDef->pchName, pExtract) == 0;
		return bResult;
	}
	else if (!stricmp(pchField, "MissionGrant.New"))
	{
		if (!item_IsMissionGrant(pItemDef))
			return false;
		return true;
	}
	else if (!stricmp(pchField, "CostumeUnlock.New"))
	{
		SavedEntityData *pSaved = SAFE_MEMBER(pEntity, pSaved);
		GameAccountData *pAccountData = entity_GetGameAccount(pEntity);
		if (pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock)
			return false;
		if (!pEntity->pSaved)
			return true;
		if (pItem && (pItem->flags & kItemFlag_Algo) && pItem->pSpecialProps)
		{
			return !costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEntity, pEntity, REF_STRING_FROM_HANDLE(pItem->pSpecialProps->hCostumeRef));
		}
		else
		{
			for (i = eaSize(&pItemDef->ppCostumes) - 1; i >= 0; i--)
				if (!costumeEntity_IsUnlockedCostumeRef(pSaved->costumeData.eaUnlockedCostumeRefs, pAccountData, pEntity, pEntity, REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef)))
					return true;
		}
		return false;
	}
	else if (strStartsWith(pchField, "Restriction"))
		return EntUsageRestrictionConditionField(pContainer, pEntity, pItem, pItemDef, pItemDef->pRestriction, pchField + 11, pContext);
	else
		return ItemDefConditionField(pContainer, pItemDef, pchField, pContext);
	StringConditionErrorReturn(pchField, "EntItemDef");
}

static bool ItemDescriptionFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	int i;
	ItemDef *pDef = GET_REF(pItem->hItem);
	bool bShowItemDesc = true;
	bool bShowPowerDesc = (pItem->flags & kItemFlag_Algo) && pDef && item_IsUnidentified(pItem) == 0;
	STRIP_LEADING_DOTS(pchField);

	if (!stricmp(pchField, "Base"))
		bShowPowerDesc = false;
	else if (!stricmp(pchField, "Algo"))
		bShowItemDesc = false;
	else if (*pchField)
		return false;

	if (bShowItemDesc && pDef)
	{
		const char *pchDesc = NULL;

		pchDesc = item_GetTranslatedDescription(pItem, pContext->langID);

		if (pchDesc && !stricmp(pchDesc, "."))
			pchDesc = NULL;
		if (pchDesc)
			estrAppend2(ppchResult, pchDesc);
	}

	if (bShowPowerDesc)
	{
		static const char **s_eaPowerDesc;
		const char *pchAlgoFormat = NULL;
		GetListEntryFormat(pchAlgoFormat, pContext->langID, pchField, s_ListMessages.hItemAlgoDescListEntry);
		if (pchAlgoFormat && pItem->pAlgoProps)
		{
			eaClear(&s_eaPowerDesc);
			for (i = 0; i < eaSize(&pItem->pAlgoProps->ppItemPowerDefRefs); i++)
			{
				if (GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef))
				{
					ItemPowerDef *pItemPowerDef = GET_REF(pItem->pAlgoProps->ppItemPowerDefRefs[i]->hItemPowerDef);
					const char *pchDescription = pItemPowerDef ? langTranslateDisplayMessage(pContext->langID, pItemPowerDef->descriptionMsg) : NULL;
					if (pchDescription && *pchDescription)
						eaPush(&s_eaPowerDesc, pchDescription);
				}
			}
			FormatEArray(ppchResult, pchAlgoFormat, STRFMT_CODE_STRING, "Description", (void **)s_eaPowerDesc, -1, pContext->langID, s_pchFilename);
		}
	}

	return true;
}

static bool ItemFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pItem)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, item_GetNameLang(pItem, pContext->langID, pContainer->chType == STRFMT_CODE_ENTITEM ? pContainer->pValue : NULL));
	else if (strStartsWith(pchField, "Description") && !item_IsUnidentified(pItem))
		return ItemDescriptionFormatField(ppchResult, pContainer, pItem, pchField + 11, pContext);
	else if (!stricmp(pchField, "Icon"))
		estrAppend2(ppchResult, item_GetIconName(pItem, GET_REF(pItem->hItem)));
	else if (strStartsWith(pchField, "Value"))
		return EntItemValueFormatField(ppchResult, pContainer, pContainer->chType == STRFMT_CODE_ENTITEM ? pContainer->pValue : NULL, pItem, pchField + 5, pContext);
	else if (!stricmp(pchField, "NumericValue"))
		estrAppend2(ppchResult, PrettyPrintInt(pItem->count, 0));
	else if (!stricmp(pchField, "MinLevel"))
		estrAppend2(ppchResult, PrettyPrintInt(item_GetMinLevel(pItem), 0));
	else if (!stricmp(pchField, "Level"))
		estrAppend2(ppchResult, PrettyPrintInt(item_GetLevel(pItem), 0));
	else if (!stricmp(pchField, "Quality") && StaticDefineGetMessage(ItemQualityEnum, item_GetQuality(pItem)))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(ItemQualityEnum, item_GetQuality(pItem))));
	else if (!stricmp(pchField, "Quality.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(ItemQualityEnum, item_GetQuality(pItem)));
	else if (pContainer->chType == STRFMT_CODE_ENTITEM)
		return EntItemDefFormatField(ppchResult, pContainer, pContainer->pValue, pchField, pContext);
	else
		return ItemDefFormatField(ppchResult, pContainer, GET_REF(pItem->hItem), pchField, pContext);
	return true;
}

static bool ItemFlagsConditionField(StrFmtContainer *pContainer, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	int flag;
	ItemDef *pDef;
	STRIP_LEADING_DOTS(pchField);

	// FIXME(jm): This breaks the original purpose of this file
	flag = StaticDefineIntGetInt(ItemFlagEnum, pchField);
	if (flag != -1 && (pItem->flags & flag) == flag)
		return true;

	pDef = GET_REF(pItem->hItem);
	if (pDef)
		return ItemDefFlagsConditionField(pContainer, pDef, pchField, pContext);

	StringConditionErrorReturn(pchField, "ItemFlags");
}

static bool ItemConditionField(StrFmtContainer *pContainer, Item *pItem, const unsigned char *pchField, StrFmtContext *pContext)
{
	const char *pch;
	STRIP_LEADING_DOTS(pchField);
	if (!pItem)
		return false;
	else if (!*pchField)
		return true;
	else if (!stricmp(pchField, "Description"))
	{
		ItemDef *pDef = GET_REF(pItem->hItem);
		if (pDef)
		{
			pch = item_GetTranslatedDescription(pItem, pContext->langID);
			return pch && *pch;
		}
	}
	else if (strStartsWith(pchField, "Value"))
		return EntItemValueConditionField(pContainer, NULL, pItem, pchField + 5, pContext);
	else if (strStartsWith(pchField, "MinLevel"))
		return strfmt_NumericCondition(item_GetMinLevel(pItem), pchField + 8, s_pchFilename);
	else if (strStartsWith(pchField, "Level"))
		return strfmt_NumericCondition(item_GetLevel(pItem), pchField + 5, s_pchFilename);
	else if (strStartsWith(pchField, "Flags"))
		return ItemFlagsConditionField(pContainer, pItem, pchField + 5, pContext);
	else if (strStartsWith(pchField, "Quality"))
		return StaticDefineIntConditionField(pContainer, item_GetQuality(pItem), ItemQualityEnum, pchField + 7, pContext);
	else if (pContainer->chType == STRFMT_CODE_ENTITEM)
		return EntItemDefConditionField(pContainer, pContainer->pValue, pchField, pContext);
	else
		return ItemDefConditionField(pContainer, GET_REF(pItem->hItem), pchField, pContext);

	StringConditionErrorReturn(pchField, "Item");
}

static bool EntItemConditionField(StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	Item *pItem = pContainer->pValue2;
	const char *pchUsage = pContainer->pchValue;

	STRIP_LEADING_DOTS(pchField);
	if (!pItem)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "Value"))
		return EntItemValueConditionField(pContainer, pEntity, pItem, pchField + 5, pContext);
	else if (!pEntity)
		return ItemConditionField(pContainer, pItem, pchField, pContext);
	// Insert any item related conditions that require an entity here
	else
		return ItemConditionField(pContainer, pItem, pchField, pContext);
}

static bool PowerDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, PowerDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->msgDisplayName.hMessage));
	else if (!stricmp(pchField, "ShortDescription"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->msgDescription.hMessage));
	else if (!stricmp(pchField, "LongDescription"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->msgDescriptionLong.hMessage));
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->msgDescriptionLong.hMessage));
	else if (strStartsWith(pchField, "PowerTags"))
		PowerTagsFormatField(ppchResult, pContainer, pDef->tags.piTags, pchField+9, pContext);
	else
		StringFormatErrorReturn(pchField, "PowerDef");
	return true;
}

static bool PowerDefConditionField(StrFmtContainer *pContainer, PowerDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		return langTranslateMessageRef(pContext->langID, pDef->msgDisplayName.hMessage) != NULL;
	else if (!stricmp(pchField, "ShortDescription"))
		return langTranslateMessageRef(pContext->langID, pDef->msgDescription.hMessage) != NULL;
	else if (!stricmp(pchField, "LongDescription"))
		return langTranslateMessageRef(pContext->langID, pDef->msgDescriptionLong.hMessage) != NULL;
	else if (!stricmp(pchField, "Description"))
		return langTranslateMessageRef(pContext->langID, pDef->msgDescriptionLong.hMessage) != NULL;
	else
		StringConditionErrorReturn(pchField, "PowerDef");
}

static bool PowerFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Power *pPower, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pPower)
		return false;
	else
		return PowerDefFormatField(ppchResult, pContainer, GET_REF(pPower->hDef), pchField, pContext);
}

static bool MissionDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, MissionDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->displayNameMsg.hMessage));
	else if (!stricmp(pchField, "Detail"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->detailStringMsg.hMessage));
	else if (!stricmp(pchField, "Summary"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->summaryMsg.hMessage));
	else if (!stricmp(pchField, "UIString"))
		estrAppend2(ppchResult, langTranslateMessageRef(pContext->langID, pDef->uiStringMsg.hMessage));
	else
		return MissionDefFormatField(ppchResult, pContainer, GET_REF(pDef->parentDef), pchField, pContext);
	return true;
}

static bool MissionFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Mission *pMission, const unsigned char *pchField, StrFmtContext *pContext)
{
	MissionDef *pDef;
	STRIP_LEADING_DOTS(pchField);
	if (!pMission)
		return false;
	else if ((pDef = mission_GetDef(pMission)) && MissionDefFormatField(ppchResult, pContainer, pDef, pchField, pContext))
		return true;
	else if (MissionFormatField(ppchResult, pContainer, pMission->parent, pchField, pContext))
		return true;
	StringFormatErrorReturn(pchField, "Mission");
}

static bool ContactDialogBlockFormatField(unsigned char **ppchResult, DialogBlock **eaBlock, const unsigned char *pchField, StrFmtContext *pContext)
{
	DialogBlock *pBlock;
	STRIP_LEADING_DOTS(pchField);
	if (!eaBlock)
		return false;
	else if (pBlock = eaRandChoice(&eaBlock))
	{
		const char *pch = langTranslateMessageRef(pContext->langID, pBlock->displayTextMesg.hMessage);
		if (pch)
			strfmt_Format(ppchResult, pch, FromListGameFormat, pContext, FromListGameCondition, pContext);
		return !!pch;
	}
	else
	{
		estrConcatf(ppchResult, "{Missing Text: %s}", pchField);
		return true;
	}
	StringFormatErrorReturn(pchField, "ContactDialogBlock");
}

static bool ContactDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, ContactDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (!stricmp(pchField, "InfoDialog"))
		return ContactDialogBlockFormatField(ppchResult, pDef->infoDialog, pchField, pContext);
	else if (!stricmp(pchField, "Greeting"))
		return ContactDialogBlockFormatField(ppchResult, pDef->greetingDialog, pchField, pContext);
	else if (!stricmp(pchField, "GeneralCallout"))
		return ContactDialogBlockFormatField(ppchResult, pDef->generalCallout, pchField, pContext);
	else if (!stricmp(pchField, "MissionCallout"))
		return ContactDialogBlockFormatField(ppchResult, pDef->missionCallout, pchField, pContext);
	else if (!stricmp(pchField, "RangeCallout"))
		return ContactDialogBlockFormatField(ppchResult, pDef->rangeCallout, pchField, pContext);
	else if (!stricmp(pchField, "DefaultDialog"))
		return ContactDialogBlockFormatField(ppchResult, pDef->defaultDialog, pchField, pContext);
	else if (!stricmp(pchField, "NoMissions"))
		return ContactDialogBlockFormatField(ppchResult, pDef->noMissionsDialog, pchField, pContext);
	else if (!stricmp(pchField, "Farewell"))
		return ContactDialogBlockFormatField(ppchResult, pDef->exitDialog, pchField, pContext);
	else if (!stricmp(pchField, "NoStoreItems"))
		return ContactDialogBlockFormatField(ppchResult, pDef->noStoreItemsDialog, pchField, pContext);
	else
		StringFormatErrorReturn(pchField, "ContactDialog");
}

static bool ContactInfoFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, ContactInfo *pInfo, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (pInfo)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pInfo->entRef);
		ContactDef *pDef = pInfo->pchContactDef ? contact_DefFromName(pInfo->pchContactDef) : NULL;
		return (
			ContactDefFormatField(ppchResult, pContainer, pDef, pchField, pContext)
			|| EntityFormatField(ppchResult, pContainer, pEnt, pchField, pContext)
			);
	}
	else
		return false;
}

static bool ContactInfoConditionField(StrFmtContainer *pContainer, ContactInfo *pInfo, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pInfo)
		return false;
	else if (!*pchField)
		return true;
	else
		return EntityConditionField(pContainer, entFromEntityRefAnyPartition(pInfo->entRef), pchField, pContext);
}

static bool EntPowerFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	Power *pPower = pContainer->pValue2;
	const char *pchUsage = pContainer->pchValue;
	STRIP_LEADING_DOTS(pchField);
	if (!pPower)
		return false;
	else if (PowerFormatField(ppchResult, pContainer, pPower, pchField, pContext))
		return true;
	else
		return false;
}

static bool EntPowerDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	PowerDef *pPowerDef = pContainer->pValue2;
	const char *pchUsage = pContainer->pchValue;
	STRIP_LEADING_DOTS(pchField);
	if (!pPowerDef)
		return false;
	else if (PowerDefFormatField(ppchResult, pContainer, pPowerDef, pchField, pContext))
		return true;
	else
		StringFormatErrorReturn(pchField, "EntPowerDef");
}

static bool EntItemFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	Item *pItem = pContainer->pValue2;
	const char *pchUsage = pContainer->pchValue;
	STRIP_LEADING_DOTS(pchField);
	if (!pItem)
		return false;
	else if (strStartsWith(pchField, "Value"))
		return EntItemValueFormatField(ppchResult, pContainer, pEntity, pItem, pchField + 5, pContext);
	else if (ItemFormatField(ppchResult, pContainer, pItem, pchField, pContext))
		return true;
	else
		StringFormatErrorReturn(pchField, "EntItem");
}

static bool EntItemDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Entity *pEntity, const unsigned char *pchField, StrFmtContext *pContext)
{
	ItemDef *pItemDef = pContainer->pValue2;
	const char *pchUsage = pContainer->pchValue;
	STRIP_LEADING_DOTS(pchField);
	if (!pItemDef || !pEntity)
		return false;
	else if (strStartsWith(pchField, "Value"))
		return EntItemDefValueFormatField(ppchResult, pContainer, pEntity, pItemDef, pchField + 5, pContext);
	else if (ItemDefFormatField(ppchResult, pContainer, pItemDef, pchField, pContext))
		return true;
	else
		StringFormatErrorReturn(pchField, "EntItemDef");
}

static bool GuildFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, Guild *pGuild, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pGuild)
		return false;
	else if (!stricmp(pchField, "name"))
		estrAppend2(ppchResult, pGuild->pcName);
	else if (!stricmp(pchField, "motd"))
		estrAppend2(ppchResult, pGuild->pcMotD);
	else if (!stricmp(pchField, "description"))
		estrAppend2(ppchResult, pGuild->pcDescription);
	else
		StringFormatErrorReturn(pchField, "Guild");
	return true;
}

static bool GuildMemberFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, GuildMember *pMember, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pMember)
		return false;
	else if (!stricmp(pchField, "name"))
		estrAppend2(ppchResult, pMember->pcName);
	else if (!stricmp(pchField, "account"))
		estrAppend2(ppchResult, pMember->pcAccount);
	else if (!stricmp(pchField, "level"))
		estrConcatf(ppchResult, "%d", pMember->iLevel);
	else if (!stricmp(pchField, "rank"))
		estrConcatf(ppchResult, "%d", pMember->iRank);
	else if (!stricmp(pchField, "status"))
		estrAppend2(ppchResult, pMember->pcStatus);
	else
		StringFormatErrorReturn(pchField, "GuildMember");
	return true;
	
}

static bool CharClassFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, CharacterClass *pClass, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pClass)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pClass->msgDisplayName));
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pClass->msgDescription));
	else if (!stricmp(pchField, "LongDescription"))
		estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pClass->msgDescriptionLong));
	else if (!stricmp(pchField, "Icon"))
		estrAppend2(ppchResult, pClass->pchIconName);
	else if (!stricmp(pchField, "Portrait"))
		estrAppend2(ppchResult, pClass->pchPortraitName);
	else if (!stricmp(pchField, "Category") && StaticDefineGetMessage(CharClassCategoryEnum, pClass->eCategory))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(CharClassCategoryEnum, pClass->eCategory)));
	else if (!stricmp(pchField, "Category.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(CharClassCategoryEnum, pClass->eCategory));
	else if (!stricmp(pchField, "Type") && StaticDefineGetMessage(CharClassTypesEnum, pClass->eType))
		estrAppend2(ppchResult, langTranslateMessage(pContext->langID, StaticDefineGetMessage(CharClassTypesEnum, pClass->eType)));
	else if (!stricmp(pchField, "Type.Name"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(CharClassTypesEnum, pClass->eType));
	else
		StringFormatErrorReturn(pchField, "CharClass");
	return true;
}

static bool MicroTransDefFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, MicroTransactionDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pDef->displayNameMesg));
	else if (!stricmp(pchField, "Description") && GET_REF(pDef->descriptionLongMesg.hMessage))
		estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pDef->descriptionLongMesg));
	else if (!stricmp(pchField, "ShortDescription") || !stricmp(pchField, "Description"))
		estrAppend2(ppchResult, langTranslateDisplayMessage(pContext->langID, pDef->descriptionShortMesg));
	else if (!stricmp(pchField, "Icon"))
		estrAppend2(ppchResult, pDef->pchIconSmall);
	else if (!stricmp(pchField, "IconLarge[0]"))
		estrAppend2(ppchResult, pDef->pchIconLarge);
	else if (!stricmp(pchField, "IconLarge[1]"))
		estrAppend2(ppchResult, pDef->pchIconLargeSecond);
	else if (!stricmp(pchField, "IconLarge[2]"))
		estrAppend2(ppchResult, pDef->pchIconLargeThird);
	else if (!stricmp(pchField, "Price"))
		estrAppend2(ppchResult, PrettyPrintInt(pDef->uiPrice, 0));
	else
		StringFormatErrorReturn(pchField, "MicroTransDef");
	return true;
}

static bool MicroTransDefConditionField(StrFmtContainer *pContainer, MicroTransactionDef *pDef, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pDef)
		return false;
	else if (!*pchField)
		return true;
	else if (!stricmp(pchField, "Description"))
		return !!GET_REF(pDef->descriptionLongMesg.hMessage);
	else if (!stricmp(pchField, "IconLarge[0]"))
		return pDef->pchIconLarge && *pDef->pchIconLarge;
	else if (!stricmp(pchField, "IconLarge[1]"))
		return pDef->pchIconLargeSecond && *pDef->pchIconLargeSecond;
	else if (!stricmp(pchField, "IconLarge[2]"))
		return pDef->pchIconLargeThird && *pDef->pchIconLargeThird;
	else if (strStartsWith(pchField, "Price"))
		return strfmt_NumericCondition(pDef->uiPrice, pchField + 5, s_pchFilename);
	else
		StringConditionErrorReturn(pchField, "MicroTransDef");
	return true;
}

static bool MicroTransProductFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, MicroTransactionProduct *pProduct, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pProduct)
		return false;
	else if (!stricmp(pchField, "ID") || !stricmp(pchField, "UID"))
		estrAppend2(ppchResult, PrettyPrintInt(pProduct->uID, 0));
	else if (strStartsWith(pchField, "Price"))
	{
		S64 iPrice = -1;
		pchField += 5;
		STRIP_LEADING_DOTS(pchField);
		if (pProduct->pProduct)
		{
			S32 i;
			char achCurrency[128];
			strcpy(achCurrency, pchField);
			if (!*achCurrency)
				sprintf(achCurrency, "_%s", microtrans_GetShardCurrency());
			for (i = eaSize(&pProduct->pProduct->ppMoneyPrices)-1; i >= 0; i--)
			{
				Money *pMoney = pProduct->pProduct->ppMoneyPrices[i];
				if (pMoney && !stricmp(moneyCurrency(pMoney), achCurrency))
				{
					iPrice = moneyCountPoints(pMoney);
				}
			}
		}
		estrAppend2(ppchResult, PrettyPrintInt((S32) iPrice, 0));
	}
	else
		return MicroTransDefFormatField(ppchResult, pContainer, GET_REF(pProduct->hDef), pchField, pContext);
	return true;
}

static bool MicroTransProductConditionField(StrFmtContainer *pContainer, MicroTransactionProduct *pProduct, const unsigned char *pchField, StrFmtContext *pContext)
{
	STRIP_LEADING_DOTS(pchField);
	if (!pProduct)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "ID") || strStartsWith(pchField, "UID"))
		return strfmt_NumericCondition(pProduct->uID, *pchField == 'U' || *pchField == 'u' ? pchField + 3 : pchField + 2, s_pchFilename);
	else if (strStartsWith(pchField, "Price"))
	{
		S64 iPrice = -1;
		pchField += 5;
		if (pProduct->pProduct)
		{
			S32 i;
			char achCurrency[128];
			if (*pchField == '.')
			{
				const unsigned char *pchEnd;
				STRIP_LEADING_DOTS(pchField);
				for (pchEnd = pchField; *pchEnd && !isspace(*pchEnd); )
					pchEnd++;
				strncpy(achCurrency, pchField, pchEnd - pchField);
				pchField = pchEnd;
			}
			if (!*achCurrency)
				sprintf(achCurrency, "_%s", microtrans_GetShardCurrency());
			for (i = eaSize(&pProduct->pProduct->ppMoneyPrices)-1; i >= 0; i--)
			{
				Money *pMoney = pProduct->pProduct->ppMoneyPrices[i];
				if (pMoney && !stricmp(moneyCurrency(pMoney), achCurrency))
				{
					iPrice = moneyCountPoints(pMoney);
				}
			}
		}
		return strfmt_NumericCondition(iPrice, pchField, s_pchFilename);
	}
	else
		return MicroTransDefConditionField(pContainer, GET_REF(pProduct->hDef), pchField, pContext);
	StringConditionErrorReturn(pchField, "MicroTransProduct");
}

//////////////////////////////////////////////////////////////////////////
// Date/Time Related Formatters

static struct  
{
	REF_TO(Message) hTimer;
	REF_TO(Message) hTimerText;
	REF_TO(Message) hTimerTextFull;
	REF_TO(Message) hClockTime;
	REF_TO(Message) hDateShort;
	REF_TO(Message) hDateMonthAbbrAndDay;
	REF_TO(Message) hDateLong;
	REF_TO(Message) hDateShortAndTime;
	REF_TO(Message) hDateLongAndTime;

	REF_TO(Message) ahDaysOfWeek[7];
	REF_TO(Message) ahMonths[12];
	REF_TO(Message) ahMonthsAbbr[12];
} s_DateTimeMessage;

__forceinline static const unsigned char *UglyPrintInt(S32 i, S32 iZeroPadding)
{
	static unsigned char buf[128];
	itoa_with_grouping(i, buf, 10, 1000000000, 0, ',', '.', iZeroPadding);
	return buf;
}

static bool UglyIntFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, void *pDummy, const unsigned char *pchField, StrFmtContext *pContext)
{
	S32 iPadding = *pchField ? atoi(pchField) : 0;
	estrAppend2(ppchResult, UglyPrintInt(pContainer->iValue, iPadding));
	return true;
}

static bool TimerFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, void *pDummy, const unsigned char *pchField, StrFmtContext *pContext)
{
	S32 iTime = abs(pContainer->iValue);
	StrFmtContainer Days = {STRFMT_CODE_UGLYINT, iTime / (60 * 60 * 24)};
	StrFmtContainer Hours = {STRFMT_CODE_UGLYINT, (iTime / (60 * 60)) % 24};
	StrFmtContainer TotalHours = {STRFMT_CODE_UGLYINT, iTime / (60 * 60)};
	StrFmtContainer Minutes = {STRFMT_CODE_UGLYINT, (iTime / 60) % 60};
	StrFmtContainer TotalMinutes = {STRFMT_CODE_UGLYINT, (iTime / 60)};
	StrFmtContainer Seconds = {STRFMT_CODE_UGLYINT, (iTime % 60)};
	StrFmtContainer TotalSeconds = {STRFMT_CODE_UGLYINT, iTime};
	const char *pchMessage = NULL;
	const char *pchSubfield = strchr(pchField, '.');
	if (pchSubfield)
		pchSubfield++;
	else
		pchSubfield = "";
	if (pContainer->iValue < 0)
		estrConcatChar(ppchResult, '-');
	if (!*pchField || strStartsWith(pchField, "Timestamp"))
		pchMessage = langTranslateMessageRefDefault(pContext->langID, s_DateTimeMessage.hTimer, "{Days > 0 ? {Days}:}{Hours > 0 ? {Hours}:{Minutes.2}:{Seconds.2} | {Minutes}:{Seconds.2}}");
	else if (strStartsWith(pchField, STRFMT_DAYS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Days.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_HOURS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Hours.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_TOTAL_HOURS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(TotalHours.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_MINUTES_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Minutes.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_TOTAL_MINUTES_KEY))
		estrAppend2(ppchResult, UglyPrintInt(TotalMinutes.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_SECONDS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Seconds.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_TOTAL_SECONDS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(TotalSeconds.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, "Text"))
		pchMessage = langTranslateMessageRefDefault(pContext->langID, s_DateTimeMessage.hTimerText, "{Value.Days > 0 ? {Value.Days}d {Value.Hours > 0 ? {Value.Hours}h {Value.Minutes > 0 ? {Value.Minutes}m } | {Value.Minutes > 0 ? {Value.Minutes}m }} | {Value.Hours > 0 ? {Value.Hours}h {Value.Minutes > 0 ? {Value.Minutes}m } | {Value.Minutes > 0 ? {Value.Minutes}m | {Value.Seconds}s }}}");
	else if (strStartsWith(pchField, "FullText"))
		pchMessage = langTranslateMessageRefDefault(pContext->langID, s_DateTimeMessage.hTimerTextFull, "{Value.Days > 0 ? {Value.Days}d {Value.Hours > 0 ? {Value.Hours}h {Value.Minutes > 0 ? {Value.Minutes}m {Value.Seconds > 0 ? {Value.Seconds}s } | {Value.Seconds > 0 ? {Value.Seconds}s }} | {Value.Minutes > 0 ? {Value.Minutes}m {Value.Seconds > 0 ? {Value.Seconds}s } | {Value.Seconds > 0 ? {Value.Seconds}s }}} | {Value.Hours > 0 ? {Value.Hours}h {Value.Minutes > 0 ? {Value.Minutes}m {Value.Seconds > 0 ? {Value.Seconds}s } | {Value.Seconds > 0 ? {Value.Seconds}s }} | {Value.Minutes > 0 ? {Value.Minutes}m {Value.Seconds > 0 ? {Value.Seconds}s } | {Value.Seconds}s }}}");
	else if (strStartsWith(pchField, "Stardate"))
		estrConcatf(ppchResult, "%.2f", timerStardateFromSecondsSince2000(iTime));
	else
		StringFormatErrorReturn(pchField, "Time");

	if (pchMessage)
	{
		stashAddPointer(pContext->stArgs, STRFMT_DAYS_KEY, &Days, true);
		stashAddPointer(pContext->stArgs, STRFMT_HOURS_KEY, &Hours, true);
		stashAddPointer(pContext->stArgs, STRFMT_TOTAL_HOURS_KEY, &TotalHours, true);
		stashAddPointer(pContext->stArgs, STRFMT_MINUTES_KEY, &Minutes, true);
		stashAddPointer(pContext->stArgs, STRFMT_TOTAL_MINUTES_KEY, &TotalMinutes, true);
		stashAddPointer(pContext->stArgs, STRFMT_SECONDS_KEY, &Seconds, true);
		stashAddPointer(pContext->stArgs, STRFMT_TOTAL_SECONDS_KEY, &TotalSeconds, true);
		strfmt_Format(ppchResult, pchMessage, FromListGameFormat, pContext, FromListGameCondition, pContext);
	}
	return true;
}

static bool TimerConditionField(StrFmtContainer *pContainer, void *pDummy, const unsigned char *pchField, StrFmtContext *pContext)
{
	U32 uiTime = abs(pContainer->iValue);
	F64 fSign = 1;

	STRIP_LEADING_DOTS(pchField);

	if (pContainer->iValue < 0 && strStartsWith(pchField, "Signed"))
	{
		fSign = -1;
		pchField += 6;
		STRIP_LEADING_DOTS(pchField);
	}

	if (strStartsWith(pchField, STRFMT_DAYS_KEY))
		uiTime = uiTime / (60 * 60 * 24);
	else if (strStartsWith(pchField, STRFMT_HOURS_KEY))
		uiTime = (uiTime / (60 * 60)) % 24;
	else if (strStartsWith(pchField, STRFMT_TOTAL_HOURS_KEY))
		uiTime = uiTime / (60 * 60);
	else if (strStartsWith(pchField, STRFMT_MINUTES_KEY))
		uiTime = (uiTime / 60) % 60;
	else if (strStartsWith(pchField, STRFMT_TOTAL_MINUTES_KEY))
		uiTime = (uiTime / 60);
	else if (strStartsWith(pchField, STRFMT_SECONDS_KEY))
		uiTime = (uiTime % 60);
	else if (strStartsWith(pchField, "Stardate"))
		return strfmt_NumericCondition(timerStardateFromSecondsSince2000(uiTime) * fSign, pchField, s_pchFilename);

	return strfmt_NumericCondition(uiTime * fSign, pchField, s_pchFilename);
}


static bool DateTimeFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, struct tm *pTime, const unsigned char *pchField, StrFmtContext *pContext)
{
	const char *pchSubfield;
	S32 iHours12 = (pTime->tm_hour == 0 || pTime->tm_hour == 12) ? 12 : (pTime->tm_hour % 12);
	const char *pchMessage = NULL;
	STRIP_LEADING_DOTS(pchField);
	if (!*pchField)
		pchField = "DateShortAndTime";
	pchSubfield = strchr(pchField, '.');
	if (pchSubfield)
		pchSubfield++;
	else
		pchSubfield = "";
	if (strStartsWith(pchField, STRFMT_HOURS12_KEY))
		estrAppend2(ppchResult, UglyPrintInt(iHours12, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_HOURS24_KEY) || strStartsWith(pchField, STRFMT_HOURS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(pTime->tm_hour, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_MINUTES_KEY))
		estrAppend2(ppchResult, UglyPrintInt(pTime->tm_min, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_SECONDS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(pTime->tm_sec, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_DAY_KEY))
		estrAppend2(ppchResult, UglyPrintInt(pTime->tm_mday, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_MONTH_NUMBER_KEY))
		estrAppend2(ppchResult, UglyPrintInt(pTime->tm_mon + 1, atoi(pchSubfield)));
	else if (!stricmp(pchField, STRFMT_MONTH_NAME_KEY))
		estrAppend2(ppchResult, TranslateMessageRef(s_DateTimeMessage.ahMonths[pTime->tm_mon]));
	else if (!stricmp(pchField, STRFMT_MONTH_ABBR_NAME_KEY))
		estrAppend2(ppchResult, TranslateMessageRef(s_DateTimeMessage.ahMonthsAbbr[pTime->tm_mon]));
	else if (!stricmp(pchField, STRFMT_WEEKDAY_NAME_KEY))
		estrAppend2(ppchResult, TranslateMessageRef(s_DateTimeMessage.ahDaysOfWeek[pTime->tm_wday]));
	else if (strStartsWith(pchField, STRFMT_YEAR_KEY))
		estrAppend2(ppchResult, UglyPrintInt(pTime->tm_year + 1900, atoi(pchSubfield)));
	else if (!stricmp(pchField, "ClockTime"))
		pchMessage = TranslateMessageRef(s_DateTimeMessage.hClockTime);
	else if (!stricmp(pchField, "DateLong"))
		pchMessage = TranslateMessageRef(s_DateTimeMessage.hDateLong);
	else if (!stricmp(pchField, "DateShort"))
		pchMessage = TranslateMessageRef(s_DateTimeMessage.hDateShort);
	else if (!stricmp(pchField, "DateMonthAbbrAndDay"))
		pchMessage = TranslateMessageRef(s_DateTimeMessage.hDateMonthAbbrAndDay);
	else if (!stricmp(pchField, "DateLongAndTime"))
		pchMessage = TranslateMessageRef(s_DateTimeMessage.hDateLongAndTime);
	else if (!stricmp(pchField, "DateShortAndTime"))
		pchMessage = TranslateMessageRef(s_DateTimeMessage.hDateShortAndTime);
	else
		StringFormatErrorReturn(pchField, "DateTime");

	if (pchMessage)
	{
		StrFmtContainer Hours12 = {STRFMT_CODE_UGLYINT, iHours12};
		StrFmtContainer Hours24 = {STRFMT_CODE_UGLYINT, pTime->tm_hour};
		StrFmtContainer Minutes = {STRFMT_CODE_UGLYINT, pTime->tm_min};
		StrFmtContainer Seconds = {STRFMT_CODE_UGLYINT, pTime->tm_sec};
		StrFmtContainer Day = {STRFMT_CODE_UGLYINT, pTime->tm_mday};
		StrFmtContainer MonthNumber = {STRFMT_CODE_UGLYINT, pTime->tm_mon + 1};
		StrFmtContainer MonthName = {STRFMT_CODE_STRING};
		StrFmtContainer MonthAbbrName = {STRFMT_CODE_STRING};
		StrFmtContainer WeekdayName = {STRFMT_CODE_STRING};
		StrFmtContainer Year = {STRFMT_CODE_UGLYINT, pTime->tm_year + 1900};
		MonthName.pchValue = TranslateMessageRefDefault(s_DateTimeMessage.ahMonths[pTime->tm_mon], "");
		MonthAbbrName.pchValue = TranslateMessageRefDefault(s_DateTimeMessage.ahMonthsAbbr[pTime->tm_mon], "");
		WeekdayName.pchValue = TranslateMessageRefDefault(s_DateTimeMessage.ahDaysOfWeek[pTime->tm_wday], "");
		stashAddPointer(pContext->stArgs, STRFMT_HOURS_KEY, &Hours12, true);
		stashAddPointer(pContext->stArgs, STRFMT_HOURS24_KEY, &Hours24, true);
		stashAddPointer(pContext->stArgs, STRFMT_HOURS12_KEY, &Hours12, true);
		stashAddPointer(pContext->stArgs, STRFMT_MINUTES_KEY, &Minutes, true);
		stashAddPointer(pContext->stArgs, STRFMT_SECONDS_KEY, &Seconds, true);
		stashAddPointer(pContext->stArgs, STRFMT_DAY_KEY, &Day, true);
		stashAddPointer(pContext->stArgs, STRFMT_MONTH_NAME_KEY, &MonthName, true);
		stashAddPointer(pContext->stArgs, STRFMT_MONTH_ABBR_NAME_KEY, &MonthAbbrName, true);
		stashAddPointer(pContext->stArgs, STRFMT_MONTH_NUMBER_KEY, &MonthNumber, true);
		stashAddPointer(pContext->stArgs, STRFMT_WEEKDAY_NAME_KEY, &WeekdayName, true);
		stashAddPointer(pContext->stArgs, STRFMT_YEAR_KEY, &Year, true);
		strfmt_Format(ppchResult, pchMessage, FromListGameFormat, pContext, FromListGameCondition, pContext);
	}

	return true;
}

static bool DateTimeConditionField(StrFmtContainer *pContainer, struct tm *pTime, const unsigned char *pchField, StrFmtContext *pContext)
{
	U32 uiValue = 0;
	if (strStartsWith(pchField, STRFMT_HOURS_KEY))
		uiValue = pTime->tm_hour;
	else if (strStartsWith(pchField, STRFMT_MINUTES_KEY))
		uiValue = pTime->tm_min;
	else if (strStartsWith(pchField, STRFMT_SECONDS_KEY))
		uiValue = pTime->tm_sec;
	else if (strStartsWith(pchField, STRFMT_DAY_KEY))
		uiValue = pTime->tm_mday;
	else if (strStartsWith(pchField, STRFMT_MONTH_NUMBER_KEY))
		uiValue = pTime->tm_mon;
	else if (strStartsWith(pchField, STRFMT_YEAR_KEY))
		uiValue = pTime->tm_year;
	else
		StringConditionErrorReturn(pchField, "DateTime");
	return strfmt_NumericCondition(uiValue, pchField, s_pchFilename);
}

//////////////////////////////////////////////////////////////////////////
// AutoDescInnateModDetails
static bool InnateModFormatField(unsigned char **ppchResult, StrFmtContainer *pContainer, AutoDescInnateModDetails *pBase, const unsigned char *pchField, StrFmtContext *pContext)
{
	AutoDescInnateModDetails *pNew = pContainer->pValue2;
	STRIP_LEADING_DOTS(pchField);
	if (!pBase)
		return false;
	else if (!*pchField || !stricmp(pchField, "Name"))
		estrAppend2(ppchResult, pBase->pchAttribName);
	else if (!stricmp(pchField, "LongDescription"))
		estrAppend2(ppchResult, pBase->pchDescLong);
	else if (!stricmp(pchField, "Description"))
		estrAppend2(ppchResult, pBase->pchDesc);
	else if (strStartsWith(pchField, "Magnitude"))
	{
		StrFmtContainer flt = {0};
		flt.fValue = pBase->fMagnitude;
		pchField += 9;
		return FloatFormatField(ppchResult, &flt, NULL, pchField, pContext);
	}
	else if (!stricmp(pchField, "Attrib"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(AttribTypeEnum, pBase->offAttrib));
	else if (!stricmp(pchField, "Aspect"))
		estrAppend2(ppchResult, StaticDefineIntRevLookupNonNull(AttribAspectEnum, pBase->offAspect));
	else if (strStartsWith(pchField, "Difference"))
	{
		StrFmtContainer flt = {0};
		flt.fValue = pNew ? pNew->fMagnitude - pBase->fMagnitude : 0;
		pchField += 10;
		return FloatFormatField(ppchResult, &flt, NULL, pchField, pContext);
	}
	else if (strStartsWith(pchField, "Power"))
		return PowerDefFormatField(ppchResult, pContainer, powerdef_Find(pBase->pchPowerDef), pchField + 5, pContext);
	else if (strStartsWith(pchField, "RequiredGemSlot"))
		estrAppend2(ppchResult, pBase->pchRequiredGemSlot);
	else
		StringFormatErrorReturn(pchField, "InnateMod");
	return true;
}

static bool InnateModConditionField(StrFmtContainer *pContainer, AutoDescInnateModDetails *pBase, const unsigned char *pchField, StrFmtContext *pContext)
{
	AutoDescInnateModDetails *pNew = pContainer->pValue2;
	STRIP_LEADING_DOTS(pchField);
	if (!pBase)
		return false;
	else if (!*pchField)
		return true;
	else if (strStartsWith(pchField, "Magnitude"))
		return strfmt_NumericCondition(pBase->fMagnitude, pchField + 9, s_pchFilename);
	else if (strStartsWith(pchField, "Attrib"))
		return StaticDefineIntConditionField(pContainer, pBase->offAttrib, AttribTypeEnum, pchField + 6, pContext);
	else if (strStartsWith(pchField, "Aspect"))
		return StaticDefineIntConditionField(pContainer, pBase->offAspect, AttribAspectEnum, pchField + 6, pContext);
	else if (strStartsWith(pchField, "Difference"))
		return strfmt_NumericCondition(pNew ? pNew->fMagnitude - pBase->fMagnitude : 0, pchField + 10, s_pchFilename);
	else if (strStartsWith(pchField, "NegativeDifference"))
		return strfmt_NumericCondition(pNew ? pBase->fMagnitude - pNew->fMagnitude : 0, pchField + 10, s_pchFilename);
	else if (!stricmp(pchField, "Flags.Boolean"))
		return pBase->bAttribBoolean;
	else if (!stricmp(pchField, "Flags.Damage"))
		return pBase->bDamageAttribAspect;
	else if (!stricmp(pchField, "Flags.Percentage"))
		return pBase->bPercent;
	else if (strStartsWith(pchField, "Power"))
		return PowerDefConditionField(pContainer, powerdef_Find(pBase->pchPowerDef), pchField + 5, pContext);
	else if (strStartsWith(pchField,"RequiredGemSlot"))
		return pBase->pchRequiredGemSlot!=NULL;
	StringConditionErrorReturn(pchField, "InnateMod");
}

//////////////////////////////////////////////////////////////////////////
// Whole-string formatter, should not need to be changed.

// Split "a.b" into "a" and "b", destructively, and find a StrFmtContainer for it.
static StrFmtContainer *GameFormatSplitToken(const unsigned char *pchToken, StrFmtContext *pContext, const unsigned char **ppchVar, const unsigned char **ppchField)
{
	StrFmtContainer *pContainer = NULL;
	unsigned char *pchSep;
	if (stashFindPointer(pContext->stArgs, pchToken, &pContainer))
	{
		*ppchVar = pchToken;
		*ppchField = "";
	}
	else if ((pchSep = strchr(pchToken, '.')) || (pchSep = strchr(pchToken, ' ')))
	{
		*pchSep = '\0';
		*ppchVar = pchToken;
		*ppchField = pchSep + 1;
		stashFindPointer(pContext->stArgs, pchToken, &pContainer);
	}
	else
	{
		S32 i;
		*ppchVar = NULL;
		*ppchField = NULL;
		for (i = 0; !pContainer && i < ARRAY_SIZE_CHECKED(s_aSimpleFallbacks); i++)
		{
			if (!stricmp(s_aSimpleFallbacks[i].pchKey, pchToken))
			{
				*ppchVar = s_aSimpleFallbacks[i].pchVar;
				*ppchField = s_aSimpleFallbacks[i].pchField;
				stashFindPointer(pContext->stArgs, *ppchVar, &pContainer);
			}
		}
	}
	return pContainer;
}

// Run the condition function appropriate for the given token.
static bool FromListGameCondition(unsigned char **ppchResult, const unsigned char *pchToken, StrFmtContext *pContext)
{
	const unsigned char *pchVar = NULL;
	const unsigned char *pchField = NULL;
	const unsigned char *pchFieldEnd = NULL;
	const unsigned char *pchAppend = NULL;
	StrFmtContainer *pContainer = GameFormatSplitToken(pchToken, pContext, &pchVar, &pchField);

	// Trim trailing whitespace, just to prevent breaking of stuff
	if (pchField && *pchField)
	{
		pchFieldEnd = pchField + strlen(pchField) - 1;
		while (isspace(*pchFieldEnd))
			pchFieldEnd--;
		// Not ideal, but GameFormatSplitToken is already destructive
		*(unsigned char *)(pchFieldEnd+1) = '\0';
	}

	if (pContainer && pchField && s_Conditions[pContainer->chType])
		return s_Conditions[pContainer->chType](pContainer, pContainer->pValue, pchField, pContext);
	ErrorFilenamef(s_pchFilename, "Unable to find a condition for (Var: %s / Field: %s / Token: %s) (string is %s so far)", pchVar, pchField, pchToken, ppchResult ? *ppchResult : NULL);
	estrConcatf(ppchResult, "{Unknown Condition (Var: %s / Field: %s / Condition: %s)}", pchVar, pchField, pchToken);
	return false;
}

// Run the formatting function appropriate for the given token.
static void FromListGameFormat(unsigned char **ppchResult, const unsigned char *pchToken, StrFmtContext *pContext)
{
	const unsigned char *pchVar = NULL;
	const unsigned char *pchField = NULL;
	StrFmtContainer *pContainer = GameFormatSplitToken(pchToken, pContext, &pchVar, &pchField);
	bool bSuccess = false;

	if (pchToken[0] == 'k' && pchToken[1] == ':')
	{
		bSuccess = strfmt_AppendMessageKey(ppchResult, pchToken + 2, pContext->langID);
	}
	else if (pContainer && pchField)
	{
		if (s_Formatters[pContainer->chType])
			bSuccess = s_Formatters[pContainer->chType](ppchResult, pContainer, pContainer->pValue, pchField, pContext);
		else
		{
			if (pchField && pchField[0]) {
				char *pchPath = NULL;
				estrConcatf(&pchPath, ".%s", pchField);
				strfmt_AppendContainer(ppchResult, pContainer, pchPath, pContext);
				estrDestroy(&pchPath);
			} else {
				strfmt_AppendContainer(ppchResult, pContainer, NULL, pContext);
			}
			return;
		}
	}
	else
	{
		/* 
		// BF - ComplexFallbacks are disabled since there aren't any of them anymore
		for (i = 0; !bSuccess && i < ARRAY_SIZE_CHECKED(s_aComplexFallbacks); i++)
		{
			if (!stricmp(s_aComplexFallbacks[i].pchKey, pchToken))
			{
				bSuccess = s_aComplexFallbacks[i].cbFallback(ppchResult, pContainer, pchToken, pContext);
			}
		}
		*/

	}

	if (!bSuccess && isDevelopmentMode())
	{
		ErrorFilenamef(s_pchFilename, "Unable to find a replacement for (Var: %s / Field: %s / Token: %s) (string is %s so far)", pchVar, pchField, pchToken, *ppchResult);
		estrConcatf(ppchResult, "{Unknown Token (Var: %s / Field: %s / Token: %s)}", pchVar, pchField, pchToken);
	}
}

// Fill in all default variable values.
__forceinline static void GameStrFmtDefaults(StrFmtContext *pContext, StrFmtContainer *pContainer)
{
#define FMTCASE(TYPE) \
		case STRFMT_CODE_##TYPE: \
			stashAddPointer(pContext->stArgs, STRFMT_##TYPE##_DEFAULT, pContainer, false); \
			break

	switch (pContainer->chType)
	{
	FMTCASE(ENTITY);
	FMTCASE(MISSION);
	FMTCASE(MISSIONDEF);
	FMTCASE(ITEM);
	FMTCASE(ITEMDEF);
	FMTCASE(ENTITEM);
	FMTCASE(ENTITEMDEF);
	FMTCASE(POWER);
	FMTCASE(POWERDEF);
	FMTCASE(POWERTAGS);
	FMTCASE(ENTPOWER);
	FMTCASE(ENTPOWERDEF);
	FMTCASE(CONTACTINFO);
	FMTCASE(CONTACTDEF);
	FMTCASE(INNATEMOD);
	FMTCASE(MAPVARS);
	FMTCASE(MISSIONVARS);
	FMTCASE(CHARACTERCLASS);
	FMTCASE(DATETIME);
	FMTCASE(MICROTRANSACTIONDEF);
	FMTCASE(MICROTRANSACTION);
	}
#undef FMTCASE

	if ((pContainer->chType == STRFMT_CODE_ENTITY
		|| pContainer->chType == STRFMT_CODE_ENTPOWER
		|| pContainer->chType == STRFMT_CODE_ENTPOWERDEF
		|| pContainer->chType == STRFMT_CODE_ENTITEM
		|| pContainer->chType == STRFMT_CODE_ENTITEMDEF)
		&& pContainer->pValue)
	{
		if (((Entity *)(pContainer->pValue))->pCritter)
			stashAddPointer(pContext->stArgs, STRFMT_CRITTER_DEFAULT, pContainer, false);
		if (((Entity *)(pContainer->pValue))->pPlayer)
			stashAddPointer(pContext->stArgs, STRFMT_PLAYER_DEFAULT, pContainer, false);
		stashAddPointer(pContext->stArgs, STRFMT_ENTITY_DEFAULT, pContainer, false);
	}
}

// Actual whole-string formatter.
void langFormatGameStringv(Language eLang, unsigned char **ppchResult, const unsigned char *pchFormat, va_list va)
{
	static bool s_bRun = false;
	static bool s_bDevelopment = false;

	if (!s_bRun)
	{
		s_bRun = true;
		s_bDevelopment = isDevelopmentMode();

		if (!g_Initialized) {
			gamestringformat_Init();
		}
	}

	if (pchFormat)
	{
		// Only have to worry about one recursion for now (in Item.Name)
		static StashTable s_apStashTables[2];
		StrFmtContext s_context = {0};
		const unsigned char *pchKey;
		S32 iContainer;

		if (s_apStashTables[0])
		{
			s_context.stArgs = s_apStashTables[0];
			s_apStashTables[0] = NULL;
		}
		else if (s_apStashTables[1])
		{
			s_context.stArgs = s_apStashTables[1];
			s_apStashTables[1] = NULL;
		}
		else
		{
			s_context.stArgs = stashTableCreateWithStringKeys(16, StashDefault);
		}

		s_context.langID = eLang;
		s_context.bTranslate = true;

		for (iContainer = 0; pchKey = va_arg(va, const unsigned char *); iContainer++)
		{
			StrFmtContainer *pContainer = alloca(sizeof(*pContainer));
			pContainer->chType = va_arg(va, int);

			if (s_bDevelopment)
			{
				S32 i;
				for (i = 0; i < ARRAY_SIZE_CHECKED(s_apchReserved); i++)
					devassertmsgf(stricmp(s_apchReserved[i], pchKey), "Reserved key %s passed to string formatter.", pchKey);
			}

			switch (pContainer->chType)
			{
			case STRFMT_CODE_INT:
			case STRFMT_CODE_TIMER:
				pContainer->iValue = va_arg(va, S32);
				break;
			case STRFMT_CODE_DATETIME:
				pContainer->pValue = alloca(sizeof(struct tm));
#ifdef GAMECLIENT
				timeMakeDaylightLocalTimeStructFromSecondsSince2000(va_arg(va, U32), pContainer->pValue);
#else
				timeMakeTimeStructFromSecondsSince2000(va_arg(va, U32), pContainer->pValue);
#endif
				break;
			case STRFMT_CODE_FLOAT:
				pContainer->fValue = va_arg(va, F64);
				break;
			case STRFMT_CODE_STRING:
			case STRFMT_CODE_MESSAGEKEY:
				pContainer->pchValue = va_arg(va, unsigned char *);
				if (!pContainer->pchValue)
					pContainer->pchValue = "";
				break;
			case STRFMT_CODE_MESSAGE:
				pContainer->pValue = va_arg(va, Message *);
				break;
			case STRFMT_CODE_STRUCT:
				pContainer->pValue = va_arg(va, void *);
				pContainer->pTable = va_arg(va, ParseTable *);
				break;
			case STRFMT_CODE_ENTPOWERDEF:
			case STRFMT_CODE_ENTPOWER:
			case STRFMT_CODE_ENTITEMDEF:
			case STRFMT_CODE_ENTITEM:
				pContainer->pValue2 = va_arg(va, void *);
				pContainer->pValue = va_arg(va, Entity *);
				pContainer->pchUsage = va_arg(va, const char *);
				break;
			case STRFMT_CODE_INNATEMOD:
				pContainer->pValue = va_arg(va, AutoDescInnateModDetails *);
				pContainer->pValue2 = va_arg(va, AutoDescInnateModDetails *);
				break;
			case STRFMT_CODE_POWERTAGS:
				pContainer->pValue = va_arg(va, S32 *);
				break;
			case STRFMT_CODE_STASHEDINTS:
				{
					StashTable pStash = va_arg(va, StashTable);
					StaticDefineInt* pStaticDefine = va_arg(va, StaticDefineInt*);
					StashTableIterator iter;
					StashElement elem;
					S32 iAttr = 0;
					int i = 0;
					pContainer->pValue = 0;

					stashGetIterator(pStash, &iter);
					while(stashGetNextElement(&iter, &elem))
					{
						F32 value = 0;
						StrFmtContainer *pStashContainer = alloca(sizeof(*pContainer));
						const char* pchStashKey = StaticDefineIntRevLookup(pStaticDefine, stashElementGetIntKey(elem));
						pStashContainer->chType = STRFMT_CODE_INT;
						pStashContainer->iValue = stashElementGetFloat(elem);//yeah, I know this says "float"... it's to fix a formatting issue. I'll swing by and fix this later. (CM)
						stashAddPointer(s_context.stArgs, pchStashKey, pStashContainer, true);
						iContainer++;
					}

				}
				break;
			case STRFMT_CODE_MULTIVAL:
				{
					MultiVal *pmv = va_arg(va, MultiVal *);
					switch (MULTI_GET_TYPE(pmv->type))
					{
					case MMT_INT32:
					case MMT_INT64:
						pContainer->iValue = MultiValGetInt(pmv, NULL);
						pContainer->chType = STRFMT_CODE_INT;
						break;
					case MMT_FLOAT32:
					case MMT_FLOAT64:
						pContainer->fValue = MultiValGetFloat(pmv, NULL);
						pContainer->chType = STRFMT_CODE_FLOAT;
						break;
					case MMT_STRING:
						pContainer->pchValue = MultiValGetString(pmv, NULL);
						pContainer->chType = STRFMT_CODE_STRING;
						break;
					default:
						devassertmsgf(0, "Un-stringable MultiVal type %s", MultiValTypeToReadableString(pmv->type));
						pContainer->iValue = 0;
						pContainer->chType = STRFMT_CODE_INT;
					}
				}
				break;
			default:
				pContainer->pValue = va_arg(va, void *);
				if (!devassertmsgf(s_Formatters[pContainer->chType], "Invalid type code %c, passed to %s, did you forget STRFMT_END?", pContainer->chType, __FUNCTION__))
					pContainer->chType = STRFMT_CODE_INT;
				break;
			}
			stashAddPointer(s_context.stArgs, pchKey, pContainer, true);
			GameStrFmtDefaults(&s_context, pContainer);

		}
		strfmt_Format(ppchResult, pchFormat, FromListGameFormat, &s_context, FromListGameCondition, &s_context);
		stashTableClear(s_context.stArgs);

		if (!s_apStashTables[0])
			s_apStashTables[0] = s_context.stArgs;
		else if (!s_apStashTables[1])
			s_apStashTables[1] = s_context.stArgs;
		else
			stashTableDestroy(s_context.stArgs);
		s_context.stArgs = NULL;
	}
	else if (!*ppchResult)
	{
		estrCopy2(ppchResult, "");
	}
}

void langFormatGameString(Language eLang, unsigned char **ppchResult, const unsigned char *pchFormat, ...)
{
	va_list va;
	va_start(va, pchFormat);
	s_pchFilename = NULL;
	langFormatGameStringv(eLang, ppchResult, pchFormat, va);
	va_end(va);
}

void langFormatGameDisplayMessage(Language eLang, unsigned char **ppchResult, DisplayMessage *pDisplayMessage, ...)
{
	Message *pMessage = pDisplayMessage ? GET_REF(pDisplayMessage->hMessage) : NULL;
	const char *pchFormat = pMessage ? langTranslateMessage(eLang, pMessage) : NULL;
	s_pchFilename = msgGetFilename(pMessage);
	if (pchFormat)
	{
		va_list va;
		va_start(va, pDisplayMessage);
		langFormatGameStringv(eLang, ppchResult, pchFormat, va);
		va_end(va);
	}
	else
	{
		estrConcatf(ppchResult, "{UNTRANSLATED: %s}", pDisplayMessage ? REF_STRING_FROM_HANDLE(pDisplayMessage->hMessage) : "Null Display Message");
	}
}

void langFormatGameMessage(Language eLang, unsigned char **ppchResult, SA_PARAM_OP_VALID Message *pMessage, ...)
{
	const char *pchFormat = pMessage ? langTranslateMessage(eLang, pMessage) : NULL;
	s_pchFilename = msgGetFilename(pMessage);
	if (pchFormat)
	{
		va_list va;
		va_start(va, pMessage);
		langFormatGameStringv(eLang, ppchResult, pchFormat, va);
		va_end(va);
	}
	else
	{
		estrConcatf(ppchResult, "{UNTRANSLATED: %s}", pMessage ? pMessage->pcMessageKey : NULL);
	}
}

void langFormatGameMessageKey(Language eLang, unsigned char **ppchResult, const char *pchMessageKey, ...)
{
	va_list va;
	va_start(va, pchMessageKey);
	langFormatGameMessageKeyV(eLang, ppchResult, pchMessageKey, va);
	va_end(va);
}

void langFormatGameMessageKeyV(Language eLang, unsigned char **ppchResult, const char *pchMessageKey, va_list va)
{
	const char *pchFormat = pchMessageKey ? langTranslateMessageKey(eLang, pchMessageKey) : NULL;
	if (isDevelopmentMode())
	{
		Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pchMessageKey);
		s_pchFilename = msgGetFilename(pMessage);
	}
	else
		s_pchFilename = NULL;
	if (pchFormat)
	{
		langFormatGameStringv(eLang, ppchResult, pchFormat, va);
	}
	else
	{
		estrConcatf(ppchResult, "{UNTRANSLATED: %s}", pchMessageKey);
	}
}

//////////////////////////////////////////////////////////////////////////
// Expressions for string formatting.

// Format a text string using the given entity as the "Entity" and "Player" variables.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatPlayer);
const char *MessageExprFormatPlayer(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, SA_PARAM_NN_VALID Entity *pEntity)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation && pEntity)
	{
		estrClear(&s_pch);
		langFormatGameString(eLang, &s_pch, pchTranslation,
			STRFMT_PLAYER(pEntity), 
			STRFMT_ENTITY_KEY("Value", pEntity),
			STRFMT_ENTITY_KEY("Value1", pEntity),
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format an integer named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatInt);
const char *MessageExprFormatInt(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, S32 iVal)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_INT("Value", iVal),
			STRFMT_INT("Value1", iVal),
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format integers named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatInt2);
const char *MessageExprFormatInt2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, S32 iVal1, S32 iVal2)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_INT("Value", iVal1), 
			STRFMT_INT("Value1", iVal1), 
			STRFMT_INT("Value2", iVal2), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format integers named "Value1", "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatInt3);
const char *MessageExprFormatInt3(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, S32 iVal1, S32 iVal2, S32 iVal3)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_INT("Value", iVal1), 
			STRFMT_INT("Value1", iVal1), 
			STRFMT_INT("Value2", iVal2), 
			STRFMT_INT("Value3", iVal3), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a floating point number named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatFloat);
const char *MessageExprFormatFloat(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, F32 fVal)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_FLOAT("Value", fVal), 
			STRFMT_FLOAT("Value1", fVal), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format floating point numbers named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatFloat2);
const char *MessageExprFormatFloat2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, F32 fVal1, F32 fVal2)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_FLOAT("Value", fVal1), 
			STRFMT_FLOAT("Value1", fVal1), 
			STRFMT_FLOAT("Value2", fVal2),
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format floating point numbers named "Value1", "Value2", "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatFloat3);
const char *MessageExprFormatFloat3(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, F32 fVal1, F32 fVal2, F32 fVal3)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_FLOAT("Value", fVal1), 
			STRFMT_FLOAT("Value1", fVal1), 
			STRFMT_FLOAT("Value2", fVal2), 
			STRFMT_FLOAT("Value3", fVal3), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}


// Format a string named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatString);
const char *MessageExprFormatString(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char* pchString)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_STRING("Value", pchString), 
			STRFMT_STRING("Value1", pchString), 
			STRFMT_STRING("String", pchString), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format strings named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatString2);
const char *MessageExprFormatString2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchVal1, const char *pchVal2)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_STRING("Value", pchVal1),
			STRFMT_STRING("Value1", pchVal1), 
			STRFMT_STRING("Value2", pchVal2), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format strings named "Value1", "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatString3);
const char *MessageExprFormatString3(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char *pchVal1, const char *pchVal2, const char *pchVal3)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_STRING("Value", pchVal1),
			STRFMT_STRING("Value1", pchVal1), 
			STRFMT_STRING("Value2", pchVal2), 
			STRFMT_STRING("Value3", pchVal3), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}


// Format a string named "Value1" and an int named "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatStringInt);
const char *MessageExprFormatStringInt(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char* pchVal, S32 iVal2)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_STRING("Value", pchVal), 
			STRFMT_STRING("Value1", pchVal), 
			STRFMT_INT("Value2", iVal2),
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}


// Format a string named "Value1" and ints named "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatStringInt2);
const char *MessageExprFormatStringInt2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char* pchVal, S32 iVal2, S32 iVal3)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_STRING("Value", pchVal), 
			STRFMT_STRING("Value1", pchVal), 
			STRFMT_INT("Value2", iVal2),
			STRFMT_INT("Value3", iVal3),
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}


// Format a string1 named "Value", string2 named "Value1" and ints named "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatString2Int2);
const char *MessageExprFormatString2Int2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, 
											const char* pchVal, const char* pchVal1, S32 iVal2, S32 iVal3)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	s_pchFilename = exprContextGetBlameFile(pContext);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_STRING("Value", pchVal), 
			STRFMT_STRING("Value1", pchVal1), 
			STRFMT_INT("Value2", iVal2),
			STRFMT_INT("Value3", iVal3),
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}


// Format a timestamp named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatDateTime);
const char *MessageExprFormatDateTime(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, U32 uiVal)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_DATETIME("Value", uiVal), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format timestamps named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MessageFormatDateTime2);
const char *MessageExprFormatDateTime2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, U32 uiVal1, U32 uiVal2)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	const char *pchTranslation = langTranslateMessageKey(eLang, pchMessageKey);
	if (pchTranslation)
	{
		estrClear(&s_pch);
		FormatGameString(&s_pch, pchTranslation, 
			STRFMT_DATETIME("Value", uiVal1), 
			STRFMT_DATETIME("Value1", uiVal1),
			STRFMT_DATETIME("Value2", uiVal2), 
			STRFMT_END);
	}
	else
		estrPrintf(&s_pch, "{UNTRANSLATED: %s}", pchMessageKey);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a text string using the given entity as the "Entity" and "Player" variables.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatPlayer);
const char *MessageExprStringFormatPlayer(ExprContext *pContext, const char *pchString, SA_PARAM_NN_VALID Entity *pEntity)
{
	Language eLang = locGetLanguage(getCurrentLocale());
	static unsigned char *s_pch = NULL;
	s_pchFilename = exprContextGetBlameFile(pContext);
	estrClear(&s_pch);
	if (pEntity)
	{
		langFormatGameString(eLang, &s_pch, pchString,
			STRFMT_PLAYER(pEntity), 
			STRFMT_ENTITY_KEY("Value", pEntity),
			STRFMT_ENTITY_KEY("Value1", pEntity),
			STRFMT_END);
	}
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format an integer named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatInt);
const char *MessageExprStringFormatInt(ExprContext *pContext, const char *pchString, S32 iVal)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_INT("Value", iVal), 
		STRFMT_INT("Value1", iVal), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format integers named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatInt2);
const char *MessageExprStringFormatInt2(ExprContext *pContext, const char *pchString, S32 iVal1, S32 iVal2)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_INT("Value", iVal1), 
		STRFMT_INT("Value1", iVal1), 
		STRFMT_INT("Value2", iVal2), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format integers named "Value1", "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatInt3);
	const char *MessageExprStringFormatInt3(ExprContext *pContext, const char *pchString, S32 iVal1, S32 iVal2, S32 iVal3)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_INT("Value", iVal1), 
		STRFMT_INT("Value1", iVal1), 
		STRFMT_INT("Value2", iVal2),
		STRFMT_INT("Value3", iVal3), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a floating point number named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatFloat);
const char *MessageExprStringFormatFloat(ExprContext *pContext, const char *pchString, F32 fVal)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_FLOAT("Value", fVal), 
		STRFMT_FLOAT("Value1", fVal), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format floating point numbers named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatFloat2);
const char *MessageExprStringFormatFloat2(ExprContext *pContext, const char *pchString, F32 fVal1, F32 fVal2)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_FLOAT("Value", fVal1), 
		STRFMT_FLOAT("Value1", fVal1), 
		STRFMT_FLOAT("Value2", fVal2), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format floating point numbers named "Value1", "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatFloat3);
const char *MessageExprStringFormatFloat3(ExprContext *pContext, const char *pchString, F32 fVal1, F32 fVal2, F32 fVal3)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_FLOAT("Value", fVal1), 
		STRFMT_FLOAT("Value1", fVal1), 
		STRFMT_FLOAT("Value2", fVal2), 
		STRFMT_FLOAT("Value3", fVal3), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a string named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatString);
const char *MessageExprStringFormatString(ExprContext *pContext, const char *pchString, const char* pchVal)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_STRING("Value", pchVal), 
		STRFMT_STRING("Value1", pchVal), 
		STRFMT_STRING("String", pchVal),
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format strings named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatString2);
const char *MessageExprStringFormatString2(ExprContext *pContext, const char *pchString, const char* pchVal1, const char* pchVal2)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_STRING("Value", pchVal1), 
		STRFMT_STRING("Value1", pchVal1), 
		STRFMT_STRING("Value2", pchVal2), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format strings named "Value1", "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatString3);
const char *MessageExprStringFormatString3(ExprContext *pContext, const char *pchString, const char* pchVal1, const char* pchVal2, const char* pchVal3)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_STRING("Value", pchVal1), 
		STRFMT_STRING("Value1", pchVal1), 
		STRFMT_STRING("Value2", pchVal2), 
		STRFMT_STRING("Value3", pchVal3), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a timestamp named "Value".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatDateTime);
const char *MessageExprStringFormatDateTime(ExprContext *pContext, const char *pchString, U32 uiVal)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_DATETIME("Value", uiVal), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format timestamps named "Value1" and "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatDateTime2);
const char *MessageExprStringFormatDateTime2(ExprContext *pContext, const char *pchString, U32 uiVal1, U32 uiVal2)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_DATETIME("Value", uiVal1), 
		STRFMT_DATETIME("Value1", uiVal1), 
		STRFMT_DATETIME("Value2", uiVal2), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a string named "Value1" and an int named "Value2".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatStringInt);
const char *MessageExprStringFormatStringInt(ExprContext *pContext, const char *pchString, const char* pchVal, S32 iVal2)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_STRING("Value", pchVal), 
		STRFMT_STRING("Value1", pchVal),
		STRFMT_INT("Value2", iVal2),
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a string named "Value1" and ints named "Value2" and "Value3".
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(StringFormatStringInt2);
const char *MessageExprStringFormatStringInt2(ExprContext *pContext, const char *pchString, const char* pchVal, S32 iVal2, S32 iVal3)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, pchString, 
		STRFMT_STRING("Value", pchVal), 
		STRFMT_STRING("Value1", pchVal),
		STRFMT_INT("Value2", iVal2),
		STRFMT_INT("Value3", iVal3),
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// TODO: These could be a lot more efficient and call itoa_with_grouping. But this make
// sure they're consistent. So until that's a problem, do it this way.

// RMARR - it's a problem.  I fixed MessageExprRawFormatInt, because we call it a lot.  I could do the same to MessageExprRawFormatFloat, but we almost never call that

// Format an integer.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatInt);
const char *MessageExprRawFormatInt(ExprContext *pContext, S32 i)
{
	// insanity
	/*static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, "{Value}",
		STRFMT_INT("Value", i), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";*/

	char const * pchResult = PrettyPrintInt(i,0);
	return pchResult ? exprContextAllocString(pContext, pchResult) : "";
}

// Format an integer and places the sign in front of the number even if it's positive
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatSignedInt);
const char *MessageExprRawFormatSignedInt(ExprContext *pContext, S32 i)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	if (i > 0)
	{
		FormatGameString(&s_pch, "+{Value}", 
			STRFMT_INT("Value", i), 
			STRFMT_END);
	}
	else
	{
		FormatGameString(&s_pch, "{Value}", 
			STRFMT_INT("Value", i), 
			STRFMT_END);
	}
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a floating point number.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatFloat);
const char *MessageExprRawFormatFloat(ExprContext *pContext, F32 f)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, "{Value}", 
		STRFMT_FLOAT("Value", f),
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a date using the local short format.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatDateTime);
const char *MessageExprRawFormatDateTime(ExprContext *pContext, U32 uiValue)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, "{Value.DateShortAndTime}",
		STRFMT_DATETIME("Value", uiValue),
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a date using the local short format.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatDate);
const char *MessageExprRawFormatDate(ExprContext *pContext, U32 uiValue)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, "{Value.DateShort}", 
		STRFMT_DATETIME("Value", uiValue), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a date using the local short format.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatTime);
const char *MessageExprRawFormatTime(ExprContext *pContext, U32 uiValue)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, "{Value.ClockTime}", 
		STRFMT_DATETIME("Value", uiValue),
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

// Format a date using the local short format.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatMonthAbbrAndDay);
const char *MessageExprRawFormatMonthAbbrAndDay(ExprContext *pContext, U32 uiValue)
{
	static unsigned char *s_pch = NULL;
	estrClear(&s_pch);
	FormatGameString(&s_pch, "{Value.DateMonthAbbrAndDay}",
		STRFMT_DATETIME("Value", uiValue), 
		STRFMT_END);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatU32toRGB);
const char *MessageExprFormatU32toRGB(ExprContext *pContext, U32 uiValue)
{
	static unsigned char *s_pch = NULL;
	if( s_pch == NULL )
		estrCreate(&s_pch);

	estrSetSize(&s_pch, 6);
	estrPrintf(&s_pch, "%06.6x", (uiValue >> 8));
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(FormatU32toRGBA);
const char *MessageExprFormatU32toRGBA(ExprContext *pContext, U32 uiValue)
{
	static unsigned char *s_pch = NULL;
	if( s_pch == NULL )
		estrCreate(&s_pch);

	estrSetSize(&s_pch, 8);
	estrPrintf(&s_pch, "%08.8x", uiValue);
	return s_pch ? exprContextAllocString(pContext, s_pch) : "";
}




//////////////////////////////////////////////////////////////////////////
// Register types

#define REGISTER_FORMAT_CODE(chType, cbFormat) \
	s_Formatters[(chType)] = \
		devassertmsgf(!s_Formatters[(chType)], "Game string format code '%c' already registered.", (chType)) \
			? (cbFormat) \
			: s_Formatters[(chType)]
#define REGISTER_CONDITION_CODE(chType, cbCondition) \
	s_Conditions[(chType)] = \
		devassertmsgf(!s_Conditions[(chType)], "Game string condition format code '%c' already registered.", (chType)) \
			? (cbCondition) \
			: s_Conditions[(chType)]

#define REGISTER_FORMAT_MESSAGE(filename, key, handle) \
	if (RefSystem_ReferentFromString("Message", key)) { \
		SET_HANDLE_FROM_STRING("Message", key, handle); \
	} else { \
		ErrorFilenamef(filename, "Game is missing required message key '%s' for GameStringFormat code", key); \
	}


AUTO_RUN_LATE;
void GameStringFormat_RegisterTypes(void)
{
	REGISTER_FORMAT_CODE(STRFMT_CODE_ENTITY, EntityFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_ITEM, ItemFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_ITEMDEF, ItemDefFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_POWER, PowerFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_POWERDEF, PowerDefFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_POWERTAGS, PowerTagsFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_MISSION, MissionFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_MISSIONDEF, MissionDefFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_CONTACTINFO, ContactInfoFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_CONTACTDEF, ContactDefFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_GUILD, GuildFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_GUILDMEMBER, GuildMemberFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_INT, IntFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_FLOAT, FloatFormatField);

	REGISTER_FORMAT_CODE(STRFMT_CODE_ENTITEM, EntItemFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_ENTITEMDEF, EntItemDefFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_ENTPOWER, EntPowerFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_ENTPOWERDEF, EntPowerDefFormatField);

	REGISTER_CONDITION_CODE(STRFMT_CODE_ENTITY, EntityConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_ITEM, ItemConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_ITEMDEF, ItemDefConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_INT, IntConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_FLOAT, FloatConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_STRING, StringConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_STRUCT, StructConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_CONTACTINFO, ContactInfoConditionField);

	REGISTER_CONDITION_CODE(STRFMT_CODE_ENTITEM, EntItemConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_ENTITEMDEF, EntItemDefConditionField);

	REGISTER_FORMAT_CODE(STRFMT_CODE_INNATEMOD, InnateModFormatField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_INNATEMOD, InnateModConditionField);

	REGISTER_FORMAT_CODE(STRFMT_CODE_ACTIVITYVARS, MapVarsFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_MAPVARS, MapVarsFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_MISSIONVARS, MissionVarsFormatField);

	REGISTER_FORMAT_CODE(STRFMT_CODE_CHARACTERCLASS, CharClassFormatField);

	REGISTER_FORMAT_CODE(STRFMT_CODE_MICROTRANSACTIONDEF, MicroTransDefFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_MICROTRANSACTION, MicroTransProductFormatField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_MICROTRANSACTIONDEF, MicroTransDefConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_MICROTRANSACTION, MicroTransProductConditionField);

	// All the time-handling stuff
	REGISTER_FORMAT_CODE(STRFMT_CODE_TIMER, TimerFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_DATETIME, DateTimeFormatField);
	REGISTER_FORMAT_CODE(STRFMT_CODE_UGLYINT, UglyIntFormatField);

	REGISTER_CONDITION_CODE(STRFMT_CODE_TIMER, TimerConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_UGLYINT, IntConditionField);
	REGISTER_CONDITION_CODE(STRFMT_CODE_DATETIME, DateTimeConditionField);

}

AUTO_STARTUP(GameStringFormat) ASTRT_DEPS(AS_Messages);
void gamestringformat_Init(void)
{
	g_Initialized = true;

	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Timer", s_DateTimeMessage.hTimer);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_TimerText", s_DateTimeMessage.hTimerText);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_FullTimerText", s_DateTimeMessage.hTimerTextFull);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_ClockTime", s_DateTimeMessage.hClockTime);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_DateShort", s_DateTimeMessage.hDateShort);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_DateMonthAbbrAndDay", s_DateTimeMessage.hDateMonthAbbrAndDay);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_DateLong", s_DateTimeMessage.hDateLong);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_DateShortAndTime", s_DateTimeMessage.hDateShortAndTime);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_DateLongAndTime", s_DateTimeMessage.hDateLongAndTime);

	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Sunday", s_DateTimeMessage.ahDaysOfWeek[0]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Monday", s_DateTimeMessage.ahDaysOfWeek[1]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Tuesday", s_DateTimeMessage.ahDaysOfWeek[2]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Wednesday", s_DateTimeMessage.ahDaysOfWeek[3]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Thursday", s_DateTimeMessage.ahDaysOfWeek[4]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Friday", s_DateTimeMessage.ahDaysOfWeek[5]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Saturday", s_DateTimeMessage.ahDaysOfWeek[6]);

	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_January", s_DateTimeMessage.ahMonths[0]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_February", s_DateTimeMessage.ahMonths[1]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_March", s_DateTimeMessage.ahMonths[2]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_April", s_DateTimeMessage.ahMonths[3]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_May", s_DateTimeMessage.ahMonths[4]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_June", s_DateTimeMessage.ahMonths[5]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_July", s_DateTimeMessage.ahMonths[6]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_August", s_DateTimeMessage.ahMonths[7]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_September", s_DateTimeMessage.ahMonths[8]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_October", s_DateTimeMessage.ahMonths[9]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_November", s_DateTimeMessage.ahMonths[10]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_December", s_DateTimeMessage.ahMonths[11]);

	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Jan", s_DateTimeMessage.ahMonthsAbbr[0]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Feb", s_DateTimeMessage.ahMonthsAbbr[1]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Mar", s_DateTimeMessage.ahMonthsAbbr[2]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Apr", s_DateTimeMessage.ahMonthsAbbr[3]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_May", s_DateTimeMessage.ahMonthsAbbr[4]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Jun", s_DateTimeMessage.ahMonthsAbbr[5]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Jul", s_DateTimeMessage.ahMonthsAbbr[6]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Aug", s_DateTimeMessage.ahMonthsAbbr[7]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Sep", s_DateTimeMessage.ahMonthsAbbr[8]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Oct", s_DateTimeMessage.ahMonthsAbbr[9]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Nov", s_DateTimeMessage.ahMonthsAbbr[10]);
	REGISTER_FORMAT_MESSAGE("messages/DateTime.ms", "DateTime_Dec", s_DateTimeMessage.ahMonthsAbbr[11]);

	REGISTER_FORMAT_MESSAGE("messages/GameStringFormat.ms", "ListEntry_Default", s_ListMessages.hDefaultListEntry);
	REGISTER_FORMAT_MESSAGE("messages/GameStringFormat.ms", "ListEntry_CharacterClass", s_ListMessages.hClassListEntry);
	REGISTER_FORMAT_MESSAGE("messages/GameStringFormat.ms", "ListEntry_AlgoDesc", s_ListMessages.hItemAlgoDescListEntry);
}

//////////////////////////////////////////////////////////////////////////
// Test cases
#define FAIL_UNLESS_EQUAL(pch1, pch2) devassertmsg(!strcmp(pch1, pch2), "Strings are not equal")

AUTO_COMMAND ACMD_CATEGORY(Debug);
void GameStringFormat_Test(void)
{
	char *pchTest = NULL;
	char *apchValues[] = {
		"Simple Test", "Simple Test",
		"Escaped\\\\ \\{Test\\}", "Escaped\\ {Test}",
	};
	S32 i;
	estrStackCreate(&pchTest);
	for (i = 0; i < ARRAY_SIZE_CHECKED(apchValues) - 1; i += 2)
	{
		estrClear(&pchTest);
		langFormatGameString(langGetCurrent(), &pchTest, apchValues[i], STRFMT_END);
		FAIL_UNLESS_EQUAL(pchTest, apchValues[i+1]);
	}

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Count.4}", STRFMT_INT("Count", 22), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "0022");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Count.4}", STRFMT_INT("Count", 22), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "0022");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Count.6}", STRFMT_INT("Count", 0), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "000000");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Count.0}", STRFMT_INT("Count", 12), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "12");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", 15), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "0:15");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", -15), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "-0:15");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", 60), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "1:00");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", 123), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "2:03");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", 60*60), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "1:00:00");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", 2*60*60+12*60+25), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "2:12:25");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", -(2*60*60+12*60+25)), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "-2:12:25");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time}", STRFMT_TIMER("Time", 3*60*60*24+2*60*60+12*60+25), STRFMT_END);
	FAIL_UNLESS_EQUAL(pchTest, "3:2:12:25");

	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest, "{Time.ClockTime}", STRFMT_DATETIME("Time", 123456789), STRFMT_END);
	// 1:33 or 2:33 depending on daylight savings time.
	devassertmsg(!strcmp(pchTest, "1:33 p.m.") || !strcmp(pchTest, "2:33 p.m."), "Strings are not equal");

	// test case I can breakpoint to get known good results
	estrClear(&pchTest);
	langFormatGameString(langGetCurrent(), &pchTest,
		"Clock Time: {Time.ClockTime}\n"
		"Short Date: {Time.DateShort}\n"
		"Long Date: {Time.DateLong}\n"
		"Short Date+Time: {Time.DateShortAndTime}\n"
		"Long Date+Time: {Time.DateLongAndTime}\n"
		"Hours 12: {Time.Hours12}\n"
		"Hours 24: {Time.Hours24}\n"
		"Hours: {Time.Hours}\n"
		"Minutes: {Time.Minutes}\n"
		"Seconds: {Time.Seconds}\n"
		"Month Name: {Time.MonthName}\n"
		"Month Number: {Time.MonthNumber}\n"
		"Year: {Time.Year}\n"
		"WeekdayName: {Time.WeekdayName}\n"
		,
		STRFMT_DATETIME("Time", timeSecondsSince2000()), STRFMT_END);


	estrDestroy(&pchTest);
}
