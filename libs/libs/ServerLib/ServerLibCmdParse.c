#include "logging.h"
#include "objTransactions.h"
#include "LocalTransactionManager.h"
#include "StringUtil.h"
#include "TextFilterCommon.h"
#include "Autogen/ServerLib_autogen_remotefuncs.h"

extern void MakeSureReductionsAreReady();

// This setting is a comma separated list of short (2 character) language codes that are supported by this shard. We reject logins at the LoginServer and GameServer from clients (AL < 4)
// with a language not in this list. If empty, we support all incoming languages.
static char *s_SupportedLanguages = NULL;
AUTO_CMD_ESTRING(s_SupportedLanguages, SupportedLanguages) ACMD_AUTO_SETTING(LoginServer, LOGINSERVER, GAMESERVER, ACCOUNTSERVER, GUILDSERVER, UGCDATAMANAGER) ACMD_CALLBACK(langSupportedLanguagesUpdate);

//test command to see if logging gets through before servers die
AUTO_COMMAND ACMD_CATEGORY(Test);
void TestLogAndDie(int iDie)
{
	if (iDie)
	{
		log_printf(LOG_TEST, "I am about to die... did this message get through?");
		exit(-1);
	}
	else
	{
		log_printf(LOG_TEST, "I am not going to die anytime soon. No sir.");
	}

}

bool gbStopTestTransactions = false;


static void LotsOfTransactionsTest1_CB(TransactionReturnVal *returnVal, void *userData)
{
	char *pRetString = NULL;

	switch(RemoteCommandCheck_LotsOfTestTransactionsTest1(returnVal, &pRetString))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		return;

	case TRANSACTION_OUTCOME_SUCCESS:
		{
			U32 iType, iID;
			if (gbStopTestTransactions)
			{
				return;
			}
			sscanf(pRetString, "%u %u", &iType, &iID);
			RemoteCommand_LotsOfTestTransactionsTest1(objCreateManagedReturnVal(LotsOfTransactionsTest1_CB, NULL), iType, iID, GetAppGlobalType(), GetAppGlobalID());
		}
		return;
		
	}
}

static void LotsOfTransactionsTest2_CB(TransactionReturnVal *returnVal, void *userData)
{
	char *pRetString = NULL;

	switch(RemoteCommandCheck_LotsOfTestTransactionsTest1(returnVal, &pRetString))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		return;

	case TRANSACTION_OUTCOME_SUCCESS:
		{
			U32 iType, iID;
			if (gbStopTestTransactions)
			{
				return;
			}

			sscanf(pRetString, "%u %u", &iType, &iID);
			RemoteCommand_LotsOfTestTransactionsTest2(objCreateManagedReturnVal(LotsOfTransactionsTest2_CB, NULL), iType, iID, GetAppGlobalType(), GetAppGlobalID());
		}
		return;
	}
}


AUTO_COMMAND_REMOTE;
void GenerateLotsOfTestTransactions(char *pToWhoType, U32 ToWhoID, int iNum)
{
	int i;

	GlobalType eToWhoType = NameToGlobalType(pToWhoType);

	gbStopTestTransactions = false;

	if (!eToWhoType)
	{
		return;
	}

	for (i=0; i < iNum * 2; i++)
	{
		RemoteCommand_LotsOfTestTransactionsTest1(objCreateManagedReturnVal(LotsOfTransactionsTest1_CB, NULL), eToWhoType, ToWhoID, GetAppGlobalType(), GetAppGlobalID());
	}

	for (i=0; i < iNum; i++)
	{
		RemoteCommand_LotsOfTestTransactionsTest2(objCreateManagedReturnVal(LotsOfTransactionsTest2_CB, NULL), eToWhoType, ToWhoID, GetAppGlobalType(), GetAppGlobalID());
	}
}

AUTO_COMMAND_REMOTE;
void StopTestTransactions(void)
{
	gbStopTestTransactions = true;
}

AUTO_COMMAND_REMOTE;
char *LotsOfTestTransactionsTest1(U32 eWhoSent, U32 iID)
{
	static char *pRetString = NULL;

	estrPrintf(&pRetString, "%u %u", GetAppGlobalType(), GetAppGlobalID());

	return pRetString;
}

AUTO_COMMAND_REMOTE;
char *LotsOfTestTransactionsTest2(U32 eWhoSent, U32 iID)
{
	static char *pRetString = NULL;

	estrPrintf(&pRetString, "%u %u", GetAppGlobalType(), GetAppGlobalID());

	return pRetString;
}

void langSupportedLanguagesUpdate(void)
{
	int* eaSupportedLanguages = NULL;

	if(s_SupportedLanguages && s_SupportedLanguages[0])
	{
		// Parse the string from the auto setting into the array of languages.
		char **eaSupportedLanguageStrList = NULL;
		estrTokenize(&eaSupportedLanguageStrList, ", ", s_SupportedLanguages);

		FOR_EACH_IN_EARRAY(eaSupportedLanguageStrList, char, languageStr)
		{
			LocaleID localeID = locGetIDByCrypticSpecific2LetterIdentifier(languageStr);
			if(DEFAULT_LOCALE_ID == localeID && 0 != stricmp(languageStr, locGetCrypticSpecific2LetterIdentifier(localeID)))
				AssertOrAlert("INVALID_LANGUAGE_CODE_IN_SUPPORTED_LANGUAGES", "Invalid language short code in SupportedLanguages LoginServer/GameServer AUTO_SETTING: %s", languageStr);
			else
				eaiPush(&eaSupportedLanguages, locGetLanguage(localeID));
		}
		FOR_EACH_END;

		eaDestroyEString(&eaSupportedLanguageStrList);
	}
	else
	{
		// All implemented languages are supported
		LocaleID locIt;
		for( locIt = 0; locIt < LOCALE_ID_COUNT; ++locIt ) {
			if( locIsImplemented( locIt )) {
				eaiPush( &eaSupportedLanguages, locGetLanguage( locIt ));
			}
		}
	}

	langSetSupportedLanguagesThisShard( eaSupportedLanguages );
	TextFilterReload();
	MakeSureReductionsAreReady();
	eaiDestroy( &eaSupportedLanguages );
}

AUTO_STARTUP(AS_SupportedLanguages);
void langSupportedLanguagesInit(void)
{
	// Poke the supported languages change once at startup, so the supported languages array works in dev mode or when the setting is default.
	if( nullStr( s_SupportedLanguages )) {
		langSupportedLanguagesUpdate();
	}
}
