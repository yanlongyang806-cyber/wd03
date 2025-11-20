#pragma once
GCC_SYSTEM

#include "Message.h"
#include "StringFormat.h"
#include "ExpressionMinimal.h"


//////////////////////////////////////////////////////////////////////////
// String formatting codes/macros for game objects.
// In addition to these, the codes for ints and floats are handled
// slightly differently.

// I think we're out of letters. 
// Maybe we should use numbers instead, or an enum or something. 

#define STRFMT_CODE_ENTITY 'E'
#define STRFMT_CODE_CONTACTINFO 'C'
#define STRFMT_CODE_MISSION 'M'
#define STRFMT_CODE_POWER 'P'
#define STRFMT_CODE_ITEM 'I'

#define STRFMT_CODE_ENTPOWER 'V'
#define STRFMT_CODE_ENTITEM 'B'
#define STRFMT_CODE_ENTPOWERDEF 'L'
#define STRFMT_CODE_ENTITEMDEF 'Z'
#define STRFMT_CODE_POWERTAGS 'R'

#define STRFMT_CODE_CONTACTDEF 'X'
#define STRFMT_CODE_MISSIONDEF 'N'
#define STRFMT_CODE_POWERDEF 'O'
#define STRFMT_CODE_ITEMDEF 'U'

#define STRFMT_CODE_INNATEMOD 'K'

#define STRFMT_CODE_MAPSTATE 'S'
#define STRFMT_CODE_ACTIVITYVARS 'Y'
#define STRFMT_CODE_MAPVARS 'W'
#define STRFMT_CODE_MISSIONVARS 'J'

#define STRFMT_CODE_GUILD 'H'
#define STRFMT_CODE_GUILDMEMBER 'A'

#define STRFMT_CODE_CHARACTERCLASS 'F'

#define STRFMT_CODE_MICROTRANSACTIONDEF 'Q'
#define STRFMT_CODE_MICROTRANSACTION '$'

#define STRFMT_CODE_STASHEDINTS '#'

// Takes an absolute number of seconds
#define STRFMT_CODE_TIMER 'T'

// Takes a number of seconds since 2000
#define STRFMT_CODE_DATETIME 'D'

// Used by the time formatters for year formatting so we get 2000 instead of 2,000.
#define STRFMT_CODE_UGLYINT 'G'

#define STRFMT_CODE_MULTIVAL '?'

#define STRFMT_POINTER_CHECKED(StructType, pchName, chType, pValue) (pchName), (char)(chType), STRFMT_CHECK(StructType, pValue)

#define STRFMT_CHECK(StructType, pValue) ((pValue) == ((StructType *)(pValue)) ? (pValue) : (pValue))

#define STRFMT_ENTITY_KEY(pchName, pEntity) STRFMT_POINTER_CHECKED(Entity, (pchName), STRFMT_CODE_ENTITY, (pEntity))

#define STRFMT_CONTACTINFO_KEY(pchName, pContact) STRFMT_POINTER_CHECKED(ContactInfo, (pchName), STRFMT_CODE_CONTACTINFO, (pContact))
#define STRFMT_CONTACTDEF_KEY(pchName, pContactDef) STRFMT_POINTER_CHECKED(ContactDef, (pchName), STRFMT_CODE_CONTACTDEF, (pContactDef))

#define STRFMT_MISSION_KEY(pchName, pMission) STRFMT_POINTER_CHECKED(Mission, (pchName), STRFMT_CODE_MISSION, (pMission))
#define STRFMT_POWER_KEY(pchName, pPower) STRFMT_POINTER_CHECKED(Power, (pchName), STRFMT_CODE_POWER, (pPower))
#define STRFMT_ITEM_KEY(pchName, pItem) STRFMT_POINTER_CHECKED(Item, (pchName), STRFMT_CODE_ITEM, (pItem))

#define STRFMT_POWERTAGS_KEY(pchName, eaiPowerTags) STRFMT_POINTER_CHECKED(S32, (pchName), STRFMT_CODE_POWERTAGS, (eaiPowerTags))

#define STRFMT_MISSIONDEF_KEY(pchName, pMissionDef) STRFMT_POINTER_CHECKED(MissionDef, (pchName), STRFMT_CODE_MISSIONDEF, (pMissionDef))
#define STRFMT_POWERDEF_KEY(pchName, pPowerDef) STRFMT_POINTER_CHECKED(PowerDef, (pchName), STRFMT_CODE_POWERDEF, (pPowerDef))
#define STRFMT_ITEMDEF_KEY(pchName, pItemDef) STRFMT_POINTER_CHECKED(ItemDef, (pchName), STRFMT_CODE_ITEMDEF, (pItemDef))

#define STRFMT_GUILD_KEY(pchName, pGuild) STRFMT_POINTER_CHECKED(Guild, (pchName), STRFMT_CODE_GUILD, (pGuild))
#define STRFMT_GUILDMEMBER_KEY(pchName, pMember) STRFMT_POINTER_CHECKED(GuildMember, (pchName), STRFMT_CODE_GUILDMEMBER, (pMember))

#define STRFMT_ENTPOWER_KEY(pchName, pEntity, pPower, pchUsage) STRFMT_POINTER_CHECKED(Power, (pchName), STRFMT_CODE_ENTPOWER, (pPower)), STRFMT_CHECK(Entity, pEntity), (pchUsage)
#define STRFMT_ENTITEM_KEY(pchName, pEntity, pItem, pchUsage) STRFMT_POINTER_CHECKED(Item, (pchName), STRFMT_CODE_ENTITEM, (pItem)), STRFMT_CHECK(Entity, pEntity), (pchUsage)
#define STRFMT_ENTPOWERDEF_KEY(pchName, pEntity, pPowerDef, pchUsage) STRFMT_POINTER_CHECKED(PowerDef, (pchName), STRFMT_CODE_ENTPOWERDEF, (pPowerDef)), STRFMT_CHECK(Entity, pEntity), (pchUsage)
#define STRFMT_ENTITEMDEF_KEY(pchName, pEntity, pItemDef, pchUsage) STRFMT_POINTER_CHECKED(ItemDef, (pchName), STRFMT_CODE_ENTITEMDEF, (pItemDef)), STRFMT_CHECK(Entity, pEntity), (pchUsage)

#define STRFMT_INNATEMOD_KEY(pchName, pAutoDescInnateModDetails, pCompare) STRFMT_POINTER_CHECKED(AutoDescInnateModDetails, (pchName), STRFMT_CODE_INNATEMOD, (pAutoDescInnateModDetails)), STRFMT_CHECK(AutoDescInnateModDetails, (pCompare))

#define STRFMT_ACTIVITYVARS_KEY(pchName, ppActivityVars) STRFMT_POINTER_CHECKED(WorldVariable*, (pchName), STRFMT_CODE_ACTIVITYVARS, (ppActivityVars))
#define STRFMT_MAPVARS_KEY(pchName, ppMapVars) STRFMT_POINTER_CHECKED(WorldVariable*, (pchName), STRFMT_CODE_MAPVARS, (ppMapVars))
#define STRFMT_MISSIONVARS_KEY(pchName, ppMissionVars) STRFMT_POINTER_CHECKED(WorldVariableContainer*, (pchName), STRFMT_CODE_MISSIONVARS, (ppMissionVars))

#define STRFMT_CHARACTERCLASS_KEY(pchName, pCharacterClass) STRFMT_POINTER_CHECKED(CharacterClass, (pchName), STRFMT_CHARACTERCLASS, (pCharacterClass))

#define STRFMT_MICROTRANSACTIONDEF_KEY(pchName, pMicroTransactionDef) STRFMT_POINTER_CHECKED(MicroTransactionDef, (pchName), STRFMT_CODE_MICROTRANSACTIONDEF, (pMicroTransactionDef))
#define STRFMT_MICROTRANSACTION_KEY(pchName, pMicroTransactionProduct) STRFMT_POINTER_CHECKED(MicroTransactionProduct, (pchName), STRFMT_CODE_MICROTRANSACTION, (pMicroTransactionProduct))

#define STRFMT_MULTIVAL(pchName, pmv) STRFMT_POINTER_CHECKED(MultiVal, (pchName), STRFMT_CODE_MULTIVAL, (pmv))

#define STRFMT_TIMER(pchName, iValue) (pchName), (char)STRFMT_CODE_TIMER, (S32)(iValue)
#define STRFMT_DATETIME(pchName, uiValue) (pchName), (char)STRFMT_CODE_DATETIME, (S32)(uiValue)

#define STRFMT_UGLYINT(pchName, uiValue) (pchName), (char)STRFMT_CODE_UGLYINT, (S32)(uiValue)

// You can pass in a stashtable and a staticdefine list to easily format a large number of keys.
#define STRFMT_STASHEDINTS(pStash, pStaticDefine) "INTERNAL_STASH", (char)STRFMT_CODE_STASHEDINTS, (pStash), (pStaticDefine)

///////////////////////////////////////////////////////////////////////////
// Some variables are filled in by default based on other variables. In
// cases where the root object is implied the default name of that type i
// used (e.g. if a raw "{CharName}" is found, it's the same as
// "{Entity.CharName}").

#define STRFMT_ENTITY_DEFAULT "Entity"
#define STRFMT_PLAYER_DEFAULT "Player"
#define STRFMT_CRITTER_DEFAULT "Critter"
#define STRFMT_CONTACTINFO_DEFAULT "Contact"
#define STRFMT_CONTACTDEF_DEFAULT "Contact"
#define STRFMT_MISSION_DEFAULT "Mission"
#define STRFMT_MISSIONDEF_DEFAULT "Mission"
#define STRFMT_POWER_DEFAULT "Power"
#define STRFMT_POWERDEF_DEFAULT "Power"
#define STRFMT_ITEM_DEFAULT "Item"
#define STRFMT_ITEMDEF_DEFAULT "Item"
#define STRFMT_POWERTAGS_DEFAULT "PowerTags"

#define STRFMT_ENTITEM_DEFAULT "Item"
#define STRFMT_ENTITEMDEF_DEFAULT "Item"
#define STRFMT_ENTPOWER_DEFAULT "Power"
#define STRFMT_ENTPOWERDEF_DEFAULT "Power"

#define STRFMT_INNATEMOD_DEFAULT "Stat"

#define STRFMT_ACTIVITYVARS_DEFAULT "ActivityVar"

#define STRFMT_MAPSTATE_DEFAULT "Map"
#define STRFMT_MAPVARS_DEFAULT "MapVar"
#define STRFMT_MISSIONVARS_DEFAULT "MissionVar"

#define STRFMT_MICROTRANSACTIONDEF_DEFAULT "Product"
#define STRFMT_MICROTRANSACTION_DEFAULT "Product"

#define STRFMT_CHARACTERCLASS_DEFAULT "Class"

#define STRFMT_DATETIME_DEFAULT "DateTime"

//////////////////////////////////////////////////////////////////////////
// Some convenient macros...

#define STRFMT_PLAYER(pPlayer) STRFMT_ENTITY_KEY(STRFMT_PLAYER_DEFAULT, (pPlayer))
#define STRFMT_TARGET(pEntity) STRFMT_ENTITY_KEY("Target", (pEntity))
#define STRFMT_CRITTER(pEntity) STRFMT_ENTITY_KEY(STRFMT_CRITTER_DEFAULT, (pEntity))
#define STRFMT_ENTITY(pEntity) STRFMT_ENTITY_KEY(STRFMT_ENTITY_DEFAULT, (pEntity))

#define STRFMT_CONTACTINFO(pContact) STRFMT_CONTACTINFO_KEY(STRFMT_CONTACTINFO_DEFAULT, (pContact))
#define STRFMT_CONTACTDEF(pContactDef) STRFMT_CONTACTDEF_KEY(STRFMT_CONTACTDEF_DEFAULT, (pContact))

#define STRFMT_POWER(pPower) STRFMT_POWER_KEY(STRFMT_POWER_DEFAULT, (pPower))
#define STRFMT_POWERDEF(pPowerDef) STRFMT_POWERDEF_KEY(STRFMT_POWERDEF_DEFAULT, (pPower))
#define STRFMT_POWERTAGS(eaiPowerTags) STRFMT_POWERTAGS_KEY(STRFMT_POWERTAGS_DEFAULT, (eaiPowerTags))

#define STRFMT_MISSION(pMission) STRFMT_MISSION_KEY(STRFMT_MISSION_DEFAULT, (pMission))
#define STRFMT_MISSIONDEF(pMissionDef) STRFMT_MISSIONDEF_KEY(STRFMT_MISSIONDEF_DEFAULT, (pMissionDef))

#define STRFMT_ITEM(pItem) STRFMT_ITEM_KEY(STRFMT_ITEM_DEFAULT, (pItem))
#define STRFMT_ITEMDEF(pItemDef) STRFMT_ITEMDEF_KEY(STRFMT_ITEMDEF_DEFAULT, (pItemDef))

#define STRFMT_ENTPOWER(pEntity, pPower, pchUsage) STRFMT_ENTPOWER_KEY(STRFMT_POWER_DEFAULT, (pEntity), (pPower), (pchUsage))
#define STRFMT_ENTPOWERDEF(pEntity, pPowerDef, pchUsage) STRFMT_ENTPOWERDEF_KEY(STRFMT_POWERDEF_DEFAULT, (pEntity), (pPower), (pchUsage))
#define STRFMT_ENTITEM(pEntity, pItem, pchUsage) STRFMT_ENTITEM_KEY(STRFMT_ENTITEM_DEFAULT, (pEntity), (pItem), (pchUsage))
#define STRFMT_ENTITEMDEF(pEntity, pItemDef, pchUsage) STRFMT_ENTITEMDEF_KEY(STRFMT_ENTITEMDEF_DEFAULT, (pEntity), (pItemDef), (pchUsage))

#define STRFMT_INNATEMOD(pAutoDescInnateModDetails, pCompare) STRFMT_INNATEMOD_KEY(STRFMT_INNATEMOD_DEFAULT, (pAutoDescInnateModDetails), (pCompare))

#define STRFMT_ACTIVITYVARS(ppMapVars) STRFMT_ACTIVITYVARS_KEY(STRFMT_ACTIVITYVARS_DEFAULT, (ppMapVars))
#define STRFMT_MAPVARS(ppMapVars) STRFMT_MAPVARS_KEY(STRFMT_MAPVARS_DEFAULT, (ppMapVars))
#define STRFMT_MISSIONVARS(ppMissionVars) STRFMT_MISSIONVARS_KEY(STRFMT_MISSIONVARS_DEFAULT, (ppMissionVars))

#define STRFMT_CHARACTERCLASS(pCharacterClass) STRFMT_CHARACTERCLASS_KEY(STRFMT_CHARACTERCLASS_DEFAULT, (pCharacterClass))

#define STRFMT_MICROTRANSACTIONDEF(pMicroTransactionDef) STRFMT_MICROTRANSACTIONDEF_KEY(STRFMT_MICROTRANSACTIONDEF_DEFAULT, (pMicroTransactionDef))
#define STRFMT_MICROTRANSACTION(pMicroTransactionProduct) STRFMT_MICROTRANSACTION_KEY(STRFMT_MICROTRANSACTION_DEFAULT, (pMicroTransactionProduct))

//////////////////////////////////////////////////////////////////////////
// Actual format functions.

// Format in the given language.
void langFormatGameStringv(Language eLang, SA_PRE_NN_OP_STR unsigned char **ppchResult, SA_PARAM_OP_STR const unsigned char *pchFormat, va_list va);
void langFormatGameString(Language eLang, SA_PRE_NN_OP_STR unsigned char **ppchResult, SA_PARAM_OP_STR const unsigned char *pchFormat, ...);
void langFormatGameDisplayMessage(Language eLang, SA_PRE_NN_OP_STR unsigned char **ppchResult, SA_PARAM_OP_VALID DisplayMessage *pDisplayMessage, ...);
void langFormatGameMessage(Language eLang, SA_PRE_NN_OP_STR unsigned char **ppchResult, SA_PARAM_OP_VALID Message *pMessage, ...);
void langFormatGameMessageKey(Language eLang, SA_PRE_NN_OP_STR unsigned char **ppchResult, const char *pchMessageKey, ...);
void langFormatGameMessageKeyV(Language eLang, SA_PRE_NN_OP_STR unsigned char **ppchResult, const char *pchMessageKey, va_list va);

// Format in the given entity's language.
#define entFormatGameString(pEnt, ppchResult, pchFormat, ...) langFormatGameString(entGetLanguage(pEnt), (ppchResult), pchFormat, ##__VA_ARGS__, STRFMT_END)
#define entFormatGameDisplayMessage(pEnt, ppchResult, pDisplayMessage, ...) langFormatGameDisplayMessage(entGetLanguage(pEnt), (ppchResult), pDisplayMessage, ##__VA_ARGS__, STRFMT_END)
#define entFormatGameMessage(pEnt, ppchResult, pMessage, ...) langFormatGameMessage(entGetLanguage(pEnt), (ppchResult), pMessage, ##__VA_ARGS__, STRFMT_END)
#define entFormatGameMessageKey(pEnt, ppchResult, pchKey, ...) langFormatGameMessageKey(entGetLanguage(pEnt), (ppchResult), pchKey, ##__VA_ARGS__, STRFMT_END)
#define entFormatGameMessageKeyV(pEnt, ppchResult, pchKey, va) langFormatGameMessageKey(entGetLanguage(pEnt), (ppchResult), pchKey, va)

#define entFormatMessageStruct(pEnt, ppchResult, pFmtStruct) langFormatMessageStructDefault(entGetLanguage(pEnt), ppchResult, pFmtStruct, pFmtStruct->pchKey)

#define entTranslateMessageKey(pEnt, pchKey) langTranslateMessageKey(entGetLanguage(pEnt), (pchKey))
#define entTranslateDisplayMessage(pEnt, pDisplayMessage) langTranslateMessage(entGetLanguage(pEnt), GET_REF((pDisplayMessage).hMessage))
#define entTranslateMessage(pEnt, pMessage) langTranslateMessage(entGetLanguage(pEnt), (pMessage))
#define entTranslateMessageRef(pEnt, hMessage) langTranslateMessageRef(entGetLanguage(pEnt), (hMessage))

// Format in the default language on the client, error on the server.
#ifdef GAMECLIENT
#define FormatGameString(ppchResult, pchFormat, ...) langFormatGameString(langGetCurrent(), (ppchResult), pchFormat, ##__VA_ARGS__, STRFMT_END)
#define FormatGameDisplayMessage(ppchResult, pDisplayMessage, ...) langFormatGameDisplayMessage(langGetCurrent(), (ppchResult), pDisplayMessage, ##__VA_ARGS__, STRFMT_END)
#define FormatGameMessage(ppchResult, pMessage, ...) langFormatGameMessage(langGetCurrent(), (ppchResult), pMessage, ##__VA_ARGS__, STRFMT_END)
#define FormatGameMessageKey(ppchResult, pchKey, ...) langFormatGameMessageKey(langGetCurrent(), (ppchResult), pchKey, ##__VA_ARGS__, STRFMT_END)
#else
#define FormatGameString(ppchResult, pchFormat, ...) { Errorf("can't call default formatting function on the server, specify a language! (file %s, line %d)", __FILE__, __LINE__); langFormatGameString(langGetCurrent(), (ppchResult), pchFormat, ##__VA_ARGS__); }
#define FormatGameDisplayMessage(ppchResult, pDisplayMessage, ...) { Errorf("can't call default formatting function on the server, specify a language! (file %s, line %d)", __FILE__, __LINE__); langFormatGameDisplayMessage(langGetCurrent(), (ppchResult), pDisplayMessage, ##__VA_ARGS__); }
#define FormatGameMessage(ppchResult, pMessage, ...) { Errorf("can't call default formatting function on the server, specify a language! (file %s, line %d)",  __FILE__, __LINE__); langFormatGameMessage(langGetCurrent(), (ppchResult), pMessage, ##__VA_ARGS__); }
#define FormatGameMessageKey(ppchResult, pchKey, ...) { Errorf("can't call default formatting function on the server, specify a language! (file %s, line %d)",  __FILE__, __LINE__); langFormatGameMessageKey(langGetCurrent(), (ppchResult), pchKey, ##__VA_ARGS__); }
#endif

const char *MessageExprFormatString2Int2(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, 
											const char* pchVal, const char* pchVal1, S32 iVal2, S32 iVal3);

const char *MessageExprFormatStringInt(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, 
											const char* pchVal, S32 iVal2);
