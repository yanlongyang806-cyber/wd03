#include "chatStringFormat.h"
#include "chatdb.h"
#include "users.h"
#include "chatGlobal.h"
#include "msgsend.h"

#include "estring.h"
#include "file.h"
#include "error.h"
#include "StringFormat.h"
#include "GameStringFormat.h" // Just for STRFMT defs

// Most of this is copied directly from GameStringFormat.h to avoid having to 
// include the boatload of Game-related includes that are tied to that file

typedef bool (*FieldFormat)(unsigned char **ppchResult, StrFmtContainer *pContainer, void *pData, const unsigned char *pchField, StrFmtContext *pContext);
typedef bool (*FieldCondition)(StrFmtContainer *pContainer, void *pData, const unsigned char *pchField, StrFmtContext *pContext);
static FieldFormat s_Formatters[128];
static FieldCondition s_Conditions[128];

#define STRFMT_DAYS_KEY "Days"
#define STRFMT_HOURS_KEY "Hours"
#define STRFMT_HOURS12_KEY "Hours12"
#define STRFMT_HOURS24_KEY "Hours24"
#define STRFMT_MINUTES_KEY "Minutes"
#define STRFMT_TOTAL_MINUTES_KEY "TotalMinutes"
#define STRFMT_SECONDS_KEY "Seconds"
#define STRFMT_MONTH_NAME_KEY "MonthName"
#define STRFMT_MONTH_NUMBER_KEY "MonthNumber"
#define STRFMT_DAY_KEY "Day"
#define STRFMT_YEAR_KEY "Year"
#define STRFMT_WEEKDAY_NAME_KEY "WeekdayName"

static struct  
{
	REF_TO(Message) hTimer;
	REF_TO(Message) hClockTime;
	REF_TO(Message) hDateShort;
	REF_TO(Message) hDateLong;
	REF_TO(Message) hDateShortAndTime;
	REF_TO(Message) hDateLongAndTime;

	REF_TO(Message) ahDaysOfWeek[7];
	REF_TO(Message) ahMonths[12];
} s_DateTimeMessage;

__forceinline static const unsigned char *UglyPrintInt(S32 i, S32 iZeroPadding)
{
	static unsigned char buf[128];
	itoa_with_grouping(i, buf, 10, 1000000000, 0, ',', '.', iZeroPadding);
	return buf;
}

// This is the same as StringFormat.c:FromListFormat since Chat Server doesn't do any special formatting
static StrFmtContainer *ChatFormatSplitToken(const unsigned char *pchToken, StrFmtContext *pContext, const unsigned char **ppchVar, const unsigned char **ppchField)
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
	return pContainer;
}

static void FromListChatFormat(unsigned char **ppchResult, const unsigned char *pchToken, StrFmtContext *pContext)
{
	const unsigned char *pchVar = NULL;
	const unsigned char *pchField = NULL;
	StrFmtContainer *pContainer = ChatFormatSplitToken(pchToken, pContext, &pchVar, &pchField);
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

	if (!bSuccess && isDevelopmentMode())
	{
		Errorf("Unable to find a replacement for (Var: %s / Field: %s / Token: %s) (string is %s so far)", pchVar, pchField, pchToken, *ppchResult);
		estrConcatf(ppchResult, "{Unknown Token (Var: %s / Field: %s / Token: %s)}", pchVar, pchField, pchToken);
	}
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
	StrFmtContainer Minutes = {STRFMT_CODE_UGLYINT, (iTime / 60) % 60};
    StrFmtContainer TotalMinutes = {STRFMT_CODE_UGLYINT, (iTime / 60)};
	StrFmtContainer Seconds = {STRFMT_CODE_UGLYINT, (iTime % 60)};
	const char *pchMessage;
	const char *pchSubfield = strchr(pchField, '.');
	if (pchSubfield)
		pchSubfield++;
	else
		pchSubfield = "";
	pchMessage = langTranslateMessageRefDefault(pContext->langID, s_DateTimeMessage.hTimer, "{Days > 0 ? {Days}:}{Hours > 0 ? {Hours}:{Minutes.2}:{Seconds.2} | {Minutes}:{Seconds.2}}");
	if (pContainer->iValue < 0)
		estrConcatChar(ppchResult, '-');
	if (!*pchField)
	{
		stashAddPointer(pContext->stArgs, STRFMT_DAYS_KEY, &Days, true);
		stashAddPointer(pContext->stArgs, STRFMT_HOURS_KEY, &Hours, true);
		stashAddPointer(pContext->stArgs, STRFMT_MINUTES_KEY, &Minutes, true);
        stashAddPointer(pContext->stArgs, STRFMT_TOTAL_MINUTES_KEY, &TotalMinutes, true);
		stashAddPointer(pContext->stArgs, STRFMT_SECONDS_KEY, &Seconds, true);
		strfmt_Format(ppchResult, pchMessage, FromListChatFormat, pContext, NULL, pContext); // TODO FromListChatCondition?
	}
	else if (strStartsWith(pchField, STRFMT_DAYS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Days.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_HOURS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Hours.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_MINUTES_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Minutes.iValue, atoi(pchSubfield)));
    else if (strStartsWith(pchField, STRFMT_TOTAL_MINUTES_KEY))
        estrAppend2(ppchResult, UglyPrintInt(TotalMinutes.iValue, atoi(pchSubfield)));
	else if (strStartsWith(pchField, STRFMT_SECONDS_KEY))
		estrAppend2(ppchResult, UglyPrintInt(Seconds.iValue, atoi(pchSubfield)));
	return true;
}

#define REGISTER_FORMAT_CODE(chType, cbFormat) s_Formatters[(chType)] = (cbFormat)
#define REGISTER_CONDITION_CODE(chType, cbCondition) s_Conditions[(chType)] = (cbCondition)

AUTO_RUN_LATE;
void ChatStringFormat_RegisterTypes(void)
{
	// All the time-handling stuff
	REGISTER_FORMAT_CODE(STRFMT_CODE_TIMER, TimerFormatField);
	//REGISTER_CONDITION_CODE(STRFMT_CODE_TIMER, TimerConditionField);

	REGISTER_FORMAT_CODE(STRFMT_CODE_UGLYINT, UglyIntFormatField);
	//REGISTER_CONDITION_CODE(STRFMT_CODE_UGLYINT, IntConditionField);

	SET_HANDLE_FROM_STRING("Message", "DateTime_Timer", s_DateTimeMessage.hTimer);
	SET_HANDLE_FROM_STRING("Message", "DateTime_ClockTime", s_DateTimeMessage.hClockTime);
	SET_HANDLE_FROM_STRING("Message", "DateTime_DateShort", s_DateTimeMessage.hDateShort);
	SET_HANDLE_FROM_STRING("Message", "DateTime_DateLong", s_DateTimeMessage.hDateLong);
	SET_HANDLE_FROM_STRING("Message", "DateTime_DateShortAndTime", s_DateTimeMessage.hDateShortAndTime);
	SET_HANDLE_FROM_STRING("Message", "DateTime_DateLongAndTime", s_DateTimeMessage.hDateLongAndTime);

	SET_HANDLE_FROM_STRING("Message", "DateTime_Sunday", s_DateTimeMessage.ahDaysOfWeek[0]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_Monday", s_DateTimeMessage.ahDaysOfWeek[1]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_Tuesday", s_DateTimeMessage.ahDaysOfWeek[2]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_Wednesday", s_DateTimeMessage.ahDaysOfWeek[3]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_Thursday", s_DateTimeMessage.ahDaysOfWeek[4]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_Friday", s_DateTimeMessage.ahDaysOfWeek[5]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_Saturday", s_DateTimeMessage.ahDaysOfWeek[6]);

	SET_HANDLE_FROM_STRING("Message", "DateTime_January", s_DateTimeMessage.ahMonths[0]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_February", s_DateTimeMessage.ahMonths[1]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_March", s_DateTimeMessage.ahMonths[2]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_April", s_DateTimeMessage.ahMonths[3]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_May", s_DateTimeMessage.ahMonths[4]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_June", s_DateTimeMessage.ahMonths[5]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_July", s_DateTimeMessage.ahMonths[6]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_August", s_DateTimeMessage.ahMonths[7]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_September", s_DateTimeMessage.ahMonths[8]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_October", s_DateTimeMessage.ahMonths[9]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_November", s_DateTimeMessage.ahMonths[10]);
	SET_HANDLE_FROM_STRING("Message", "DateTime_December", s_DateTimeMessage.ahMonths[11]);
}

// Not thread safe
void ChatServer_Translate(SA_PARAM_NN_VALID ChatUser *user, ChatTranslation *translation, char **msg)
{
	static StrFmtContext s_context = {0};
	Message *pMessage;
	const char *pcString;
	Language langID = userGetLastLanguage(user);

	pMessage = RefSystem_ReferentFromString(gMessageDict, translation->key);
	pcString = langTranslateMessageDefault(langID, pMessage, NULL);
	if (!pcString && !quickLoadMessages)
	{
		Errorf("Invalid message key %s.  Putting '[UNTRANSLATED]%s' in the text as placeholder.", translation->key, translation->key);
		estrConcatf(msg, "[UNTRANSLATED]%s", translation->key);
	}
	else
	{
		int i, iParams = eaSize(&translation->ppParameters);
		if (!s_context.stArgs)
		{
			s_context.stArgs = stashTableCreateWithStringKeys(16, StashDefault);
			s_context.bTranslate = true;
		}
		s_context.langID = langID;

		for (i= 0; i<iParams; i++)
		{
			char chType = translation->ppParameters[i]->strFmt_Code;
			StrFmtContainer *pContainer = alloca(sizeof(*pContainer));
			pContainer->chType = chType;
			switch (chType)
			{
			case STRFMT_CODE_INT:
			case STRFMT_CODE_TIMER:
				pContainer->iValue = translation->ppParameters[i]->iIntValue;
			xcase STRFMT_CODE_STRING:
			case STRFMT_CODE_MESSAGEKEY:
				pContainer->pchValue = translation->ppParameters[i]->pchStringValue;
				if (!pContainer->pchValue)
					pContainer->pchValue = "";
			xcase STRFMT_CODE_FLOAT:
			case STRFMT_CODE_STRUCT:
			case STRFMT_CODE_MESSAGE:
				continue; // unsupported
			xdefault:
				devassertmsgf(0, "Invalid type code passed to %s from Global Chat Server", __FUNCTION__);
			}
			if (translation->ppParameters[i]->key)
				devassertmsg(stashAddPointer(s_context.stArgs, translation->ppParameters[i]->key, pContainer, false), 
				"Tried to add a key twice to a format arg list from Global Chat Server");
			else
				devassertmsg(0, "NULL key passed to Chat Server translation");
		}
		strfmt_Format(msg, pcString, FromListChatFormat, &s_context, NULL, &s_context);
		stashTableClear(s_context.stArgs);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(CHATSERVER);
void ChatServer_ForwardTranslation (U32 uAccountID, ChatTranslation *translation)
{
	ChatUser *user = userFindByContainerId(uAccountID);
	char *pcTranslatedString = NULL;
	if (!user)
		return;
	ChatServer_Translate(user, translation, &pcTranslatedString);
	sendChatSystemStaticMsg(user, translation->eType, translation->channel_name, pcTranslatedString);
	estrDestroy(&pcTranslatedString);
}
