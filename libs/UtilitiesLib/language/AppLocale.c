#include <stdlib.h>
#include <string.h>

#include "AppLocale.h"
#include "cmdparse.h"
#include "error.h"
#include "file.h"
#include "Prefs.h"
#include "RegistryReader.h"
#include "utils.h"
#include "version/AppRegCache.h"
#include "message.h"
#include "UtilitiesLib.h"
#include "stringUtil.h"
#include "utf8.h"
#include <Windows.h>

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

struct {
	char *name;
	char *crypticSpecific2LetterIdentifier; // this is the cryptic specified 2 letter code used in certain cryptic systems
	char *displayName;
	WindowsLocale windowsLocale;
	Language textLanguage;
	bool isImplemented;
	Language alternateLanguage; // Use LANGUAGE_NONE for no alternate, LANG_DEFAULT to use default string
	char *pProductsImplementedFor; //comma-separated list of products for which this locale
		//should be treated as if isImplemented is true (obviously can't be used until the product is set, which 
		//happens at command line parsing time, which is long after AUTO_RUNs)
} LocaleTable[] = {
	// Do not change the order of these, they MUST be in sync with LocaleID in AppLocale.h (not to be confused
	//with WindowsLocale or Language)
	{"English",			   "EN", "English",  LOCALE_ENGLISH,	LANGUAGE_ENGLISH,	1, LANGUAGE_DEFAULT},		// Do not move this entry.  Index 0 is used as the default locale.


	// for future reference, as of 1/14/2013,
	// * you can find (approximately) what to put in "crypticSpecific2LetterIdentifier" by looking at: http://reference.sitepoint.com/html/lang-codes
	// * you can find what to put in "displayName" by looking at: http://www.omniglot.com/language/names.htm
	{"ChineseTraditional", "ZI", "ZI",		 LOCALE_CHINESE_TRADITIONAL, LANGUAGE_TCHINESE, 0, LANGUAGE_NONE, "Night"}, // Taiwan
	{"Korean",		       "KO", "KO",	 LOCALE_KOREAN,	LANGUAGE_KOREAN,	0, LANGUAGE_DEFAULT},
	{"Japanese",	       "JA", "JA",	 LOCALE_JAPANESE,	LANGUAGE_JAPANESE,	0, LANGUAGE_DEFAULT, "Night"},
	{"German",		       "DE", "DE",  LOCALE_GERMAN,	LANGUAGE_GERMAN,	1, LANGUAGE_DEFAULT},
	{"French",		       "FR", "FR", LOCALE_FRENCH,	LANGUAGE_FRENCH,	1, LANGUAGE_DEFAULT},
	{"Spanish",		       "ES", "ES",  LOCALE_SPANISH,	LANGUAGE_SPANISH,	0, LANGUAGE_DEFAULT},
	{"Italian",            "IT", "IT", LOCALE_ITALIAN, LANGUAGE_ITALIAN, 0, LANGUAGE_DEFAULT, "Night"},
	{"Russian",            "RU", "RU", LOCALE_RUSSIAN, LANGUAGE_RUSSIAN, 0, LANGUAGE_DEFAULT, "Night"},
	{"Polish",             "PL", "PL", LOCALE_POLISH, LANGUAGE_POLISH, 0, LANGUAGE_DEFAULT, "Night"},
	{"ChineseSimplified",  "ZH", "ZH",		 LOCALE_CHINESE_SIMPLIFIED, LANGUAGE_SCHINESE, 0, LANGUAGE_NONE, "Night"}, // Mainland China
	{"Turkish",			   "TR", "TR",		 LOCALE_TURKISH, LANGUAGE_TURKISH, 0, LANGUAGE_DEFAULT, "Night"},
	{"Portuguese",		   "PT", "PT",	  LOCALE_PORTUGUESE, LANGUAGE_PORTUGUESE, 0, LANGUAGE_DEFAULT, "Night"}, // Brazil
};

STATIC_ASSERT_MESSAGE(ARRAY_SIZE(LocaleTable) == LOCALE_ID_COUNT, "LocaleTable and LocaleID enum must match!");

static LocaleID currentLocale = DEFAULT_LOCALE_ID;
static int* s_eaSupportedLanguages = NULL;

bool englishOnly = 0;
bool englishOnlyWasSet = false;


//name is now somewhat obsolete, as we can now make multiple OP files in one pass, but
//this always contains the first one
int giMakeOneLocaleOPFilesAndExit = 0;

//this contains all of them, including giMakeOneLocaleOPFilesAndExit
int *giAllLocalesToMakeAndExit = NULL;


//supports both int and locale name
AUTO_COMMAND ACMD_HIDE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void MakeOneLocaleOPFilesAndExit(char *pNameOrNumber)
{
	int i;
	int iFound = 0;
	bool bFound = false;

	if (StringToInt_Paranoid(pNameOrNumber, &iFound))
	{
		bFound = true;
	}
	else
	{
		for (i = 0; i < ARRAY_SIZE(LocaleTable); i++)
		{
			if (stricmp(LocaleTable[i].name, pNameOrNumber) == 0)
			{
				iFound = i;
				bFound = true;
				break;
			}
		}
	}

	if (bFound && iFound != LOCALE_ID_ENGLISH)
	{
		ea32Push(&giAllLocalesToMakeAndExit, iFound);
		if (!giMakeOneLocaleOPFilesAndExit)
		{
			giMakeOneLocaleOPFilesAndExit = iFound;
		}
	}
	else if( bFound )
	{
		assertmsgf(0, "This locale is not supported for OP file generation: %s\n", pNameOrNumber);
	}
	else
	{
		assertmsgf(0, "Unrecognized locale name or number: %s\n", pNameOrNumber);
	}
}

//supports both int and locale name
AUTO_COMMAND ACMD_HIDE ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void AddLocaleToMake(char *pNameOrNumber)
{
	int i;
	int iFound = 0;
	bool bFound = false;

	if (StringToInt_Paranoid(pNameOrNumber, &iFound))
	{
		bFound = true;
	}
	else
	{
		for (i = 0; i < ARRAY_SIZE(LocaleTable); i++)
		{
			if (stricmp(LocaleTable[i].name, pNameOrNumber) == 0)
			{
				iFound = i;
				bFound = true;
				break;
			}
		}
	}

	if (bFound)
	{
		ea32Push(&giAllLocalesToMakeAndExit, iFound);
	}
	else
	{
		assertmsgf(0, "Unrecognized locale name or number: %s\n", pNameOrNumber);
	}
}

// Only loads english dictionaries
AUTO_CMD_INT(englishOnly, englishOnly) ACMD_EARLYCOMMANDLINE ACMD_CALLBACK(englishOnlyCallback) ACMD_ACCESSLEVEL(0);

void englishOnlyCallback(CMDARGS)
{
	englishOnlyWasSet = true;

	if (englishOnly && getCurrentLocale() != DEFAULT_LOCALE_ID)
	{
		Errorf("-englishOnly may not be used with a non-default locale.");
		englishOnly = false;
	}
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void SetLocaleIsImplemented(char *pName, int iSet)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(LocaleTable); i++)
	{
		if (stricmp(LocaleTable[i].name, pName) == 0)
		{
			LocaleTable[i].isImplemented = iSet;
			
			//clear this as once we've set this via command that overrides anything product-specific
			LocaleTable[i].pProductsImplementedFor = NULL;
		}
	}
}

void locProductNameWasJustSet(const char *pProductName)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(LocaleTable); i++)
	{
		if (LocaleTable[i].pProductsImplementedFor)
		{
			char temp[256];

			if (CommaSeparatedListContainsWord(LocaleTable[i].pProductsImplementedFor, pProductName))
			{
				LocaleTable[i].isImplemented = true;
			}

			sprintf(temp, "!%s", pProductName);

			if (CommaSeparatedListContainsWord(LocaleTable[i].pProductsImplementedFor, temp))
			{
				LocaleTable[i].isImplemented = false;
			}
		}
	}
}


__forceinline bool isEnglishOnly(void){
	
	//all makebinning modes need all languages so they can make the translated .bin files
	if (giMakeOneLocaleOPFilesAndExit)
	{
		return false;
	}
	if (gbMakeBinsAndExit)
	{
		return false;
	}
	if(forceLoadAllTranslations())
	{
		return false;
	}

	// Consider to be in EnglishOnly mode if in development AND EnglishOnly was not manually set on the command line
	return (englishOnly || (!englishOnlyWasSet && isDevelopmentMode()));
}

const char* locGetName(LocaleID localeID){
	if(!locIDIsValid(localeID))
		return NULL;

	return LocaleTable[localeID].name;
}

const char* locGetCrypticSpecific2LetterIdentifier(LocaleID localeID){
	if(!locIDIsValid(localeID))
		return NULL;

	return LocaleTable[localeID].crypticSpecific2LetterIdentifier;
}

bool locIsImplemented(LocaleID localeID)
{
	if(!locIDIsValid(localeID))
		return false;

	return LocaleTable[localeID].isImplemented;
}

bool langIsSupportedThisShard(Language lang)
{
	assert( s_eaSupportedLanguages );

	if( isEnglishOnly() ) {
		return lang == LANGUAGE_ENGLISH;
	} else {
		return eaiFind( &s_eaSupportedLanguages, lang ) >= 0;
	}
}

void langSetSupportedLanguagesThisShard(int* eaLang)
{
	eaiCopy( &s_eaSupportedLanguages, &eaLang );
}

const int* langGetSupportedLanguagesThisShard(void)
{
	return s_eaSupportedLanguages;
}

Language locGetAlternateLanguage(LocaleID localeID)
{
	if(!locIDIsValid(localeID))
		return LANGUAGE_DEFAULT;

	return LocaleTable[localeID].alternateLanguage;
}

static void locWindowsLocaleToIETFLanguageTag(WindowsLocale windowsLocale, char **ppOutTag)
{
	char *pTempLangName = NULL;
	char *pTempCountryName = NULL;

	int langCodeLen = GetLocaleInfo_UTF8(windowsLocale, LOCALE_SISO639LANGNAME, &pTempLangName);
	int countryCodeLen = 0;

	if (!langCodeLen)
	{
		estrDestroy(&pTempLangName);
		estrCopy2(ppOutTag, "en");
		return;
	}

	estrCopy(ppOutTag, &pTempLangName);

	countryCodeLen = GetLocaleInfo_UTF8(windowsLocale, LOCALE_SISO3166CTRYNAME,
		&pTempCountryName);

	if (!countryCodeLen)
	{
		estrDestroy(&pTempLangName);
		estrDestroy(&pTempCountryName);
		return;
	}

	estrPrintf(ppOutTag, "%s-%s", pTempLangName, pTempCountryName);
	estrDestroy(&pTempLangName);
	estrDestroy(&pTempCountryName);
}

const char * locGetIETFLanguageTag(LocaleID localeID)
{
	WindowsLocale windowsLocale = 0;
	static char *pLocCache[LOCALE_ID_COUNT] = {0};
	LocaleID curID = 0;

	if (localeID < 0 || localeID >= LOCALE_ID_COUNT)
	{
		localeID = DEFAULT_LOCALE_ID;
	}

	ATOMIC_INIT_BEGIN;
	for (curID = 0; curID < LOCALE_ID_COUNT; curID++)
	{
		if (!estrLength(&pLocCache[curID]))
		{
			windowsLocale = locGetWindowsLocale(curID);
			locWindowsLocaleToIETFLanguageTag(windowsLocale, &pLocCache[curID]);
		}
	}
	ATOMIC_INIT_END;

	return pLocCache[localeID];
}

LocaleID locGetIDByName(const char* ID)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(LocaleTable); i++)
	{
		if(stricmp(LocaleTable[i].name, ID) == 0)
			return i;
	}

	return DEFAULT_LOCALE_ID;
}

void locGetImplementedLocaleNames(char ***pppOutLocaleNames)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(LocaleTable); i++)
	{
		if (locIsImplemented(i))
		{
			eaPush(pppOutLocaleNames, strdup(LocaleTable[i].name));
		}
	}
}

LocaleID locGetIDByLanguage(Language langID)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(LocaleTable); i++)
	{
		if(LocaleTable[i].textLanguage == langID)
			return i;
	}

	return DEFAULT_LOCALE_ID;
}

Language locGetAlternateLanguageFromLang(Language langID)
{
	return locGetAlternateLanguage(locGetIDByLanguage(langID));
}

LocaleID locGetIDByCrypticSpecific2LetterIdentifier(const char* code)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(LocaleTable); i++)
	{
		if(stricmp(LocaleTable[i].crypticSpecific2LetterIdentifier, code) == 0)
			return i;
	}

	return DEFAULT_LOCALE_ID;
}

WindowsLocale locGetWindowsLocale(LocaleID localeID)
{
	if (localeID < 0 || localeID >= ARRAY_SIZE(LocaleTable))
		localeID = 0;
	return  LocaleTable[localeID].windowsLocale;
}

Language locGetLanguage(LocaleID localeID)
{
	if (localeID < 0 || localeID >= ARRAY_SIZE(LocaleTable))
		localeID = 0;
	return  LocaleTable[localeID].textLanguage;
}

const char* locGetDisplayName(LocaleID localeID)
{
	if (localeID < 0 || localeID >= ARRAY_SIZE(LocaleTable))
		localeID = 0;
	return  LocaleTable[localeID].displayName;
}

LocaleID locGetIDByWindowsLocale(WindowsLocale windowsLocale)
{
	int i;
	for(i = 0; i < ARRAY_SIZE(LocaleTable); i++){
		if(LocaleTable[i].windowsLocale == windowsLocale)
			return i;
	}

	return DEFAULT_LOCALE_ID;
}


LocaleID locGetIDByNameOrID(const char* str)
{
	int locale;

	if(strIsNumeric(str))
	{
		locale = atoi(str);
		if(!locIDIsValid(locale))
			locale = DEFAULT_LOCALE_ID;
	}
	else
		locale = locGetIDByName(str);

	return locale;
}

int locGetMaxLocaleCount(void){
	if (isEnglishOnly())
		return 1;
	return ARRAY_SIZE(LocaleTable);
}

bool locIDIsValid(LocaleID localeID)
{
	if (isEnglishOnly())
	{
		if (localeID == 0)
			return true;
		return false;
	}
	if(localeID >= ARRAY_SIZE(LocaleTable) || localeID < 0)
		return false;
	else
		return true;
}

LocaleID locGetIDInRegistry(void)
{
	LocaleID localeID = DEFAULT_LOCALE_ID;

	// Get the locale setting from the registry.
	localeID = GamePrefGetInt("Locale", -1);
	if (localeID == -1) {
		RegReader reader;
		char temp[256];
		// No locale specified, try to find the locale the installer was installed in
		reader = createRegReader();
		initRegReader(reader, regGetAppKey());
		if (rrReadString(reader, "InstallLanguage", temp, ARRAY_SIZE(temp))) {
			// convert from windows locale to CoH locale
			localeID = atoi(temp);
			localeID = locGetIDByWindowsLocale(localeID);
		}
		destroyRegReader(reader);
	}

	if(!locIDIsValid(localeID))
		return DEFAULT_LOCALE_ID;
	else
		return localeID;
}

void locSetIDInRegistry(LocaleID localeID){
	GamePrefStoreInt("Locale", localeID);
}

LocaleID getCurrentLocale(void)
{
	return currentLocale;
}

void setCurrentLocale(LocaleID localeID){

	// Refuse to set the locale if -englishOnly was specified.
	// In Dev mode, "-englishOnly 0" MUST be included BEFORE the "-Locale <name>" option
	if (isEnglishOnly() && localeID != DEFAULT_LOCALE_ID)
	{
		Errorf("Setting non-default locales is not valid in englishOnly mode.");
		return;
	}

	currentLocale = localeID;
}

#include "AutoGen/AppLocale_h_ast.c"
