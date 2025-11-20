/* File AppLocale.h
 *	Contains mappings between locale ID and string.  There is probably an ANSI way to do this.
 *	
 */

#ifndef APP_LOCALE_H
#define APP_LOCALE_H
#pragma once
GCC_SYSTEM

#define DEFAULT_LOCALE_ID 0
#define LOCALE_ID_INVALID -1 // To represent "no locale"

// Pulled from the list of xbox-supported languages
AUTO_ENUM;
typedef enum Language
{
	LANGUAGE_DEFAULT = 0, // Special language, used as default for all other languages
	LANGUAGE_ENGLISH,
	LANGUAGE_JAPANESE,
	LANGUAGE_GERMAN,
	LANGUAGE_FRENCH,
	LANGUAGE_SPANISH,
	LANGUAGE_ITALIAN,
	LANGUAGE_KOREAN,
	LANGUAGE_TCHINESE,
	LANGUAGE_PORTUGUESE,
	LANGUAGE_SCHINESE,
	LANGUAGE_POLISH,
	LANGUAGE_RUSSIAN,
	LANGUAGE_CZECH,
	LANGUAGE_DUTCH,
	LANGUAGE_NORWEGIAN,
	LANGUAGE_TURKISH,
	LANGUAGE_NONE,
	LANGUAGE_MAX
} Language;
extern StaticDefineInt LanguageEnum[];

// for future reference, as of 1/14/2013, you can find these codes here:
// http://msdn.microsoft.com/en-us/goglobal/bb964664.aspx
typedef enum WindowsLocale
{
	LOCALE_ENGLISH = 1033,				// English - United States
	LOCALE_CHINESE_TRADITIONAL = 1028,	// Chinese - Taiwan
	LOCALE_CHINESE_SIMPLIFIED = 2052,	// Chinese - People's Republic of China
	LOCALE_CHINESE_HONGKONG = 3076,		// Chinese - Hong Kong SAR
	LOCALE_KOREAN = 1042,				// Korean
	LOCALE_JAPANESE = 1041,				// Japanese
	LOCALE_GERMAN = 1031,				// German - Germany
	LOCALE_FRENCH = 1036,				// French - France
	LOCALE_SPANISH = 1034,				// Spanish - Spain (Traditional Sort)
	LOCALE_ITALIAN = 1040,				// Italian - Italy
	LOCALE_RUSSIAN = 1049,				// Russian
	LOCALE_POLISH = 1045,				// Polish
	LOCALE_TURKISH = 1055,				// Turkish
	LOCALE_PORTUGUESE = 1046,			// Portuguese - Brazil
} WindowsLocale;

// Must match LocaleTable in AppLocale.h
typedef enum LocaleID
{
	LOCALE_ID_ENGLISH = 0,
	LOCALE_ID_CHINESE_TRADITIONAL, // taiwan
	LOCALE_ID_KOREAN,
	LOCALE_ID_JAPANESE,
	LOCALE_ID_GERMAN,
	LOCALE_ID_FRENCH,
	LOCALE_ID_SPANISH,
	LOCALE_ID_ITALIAN,
	LOCALE_ID_RUSSIAN,
	LOCALE_ID_POLISH,
	LOCALE_ID_CHINESE_SIMPLIFIED, // mainland china
	LOCALE_ID_TURKISH,
	LOCALE_ID_PORTUGUESE, // brazil
	LOCALE_ID_COUNT,
} LocaleID;

const char* locGetName(LocaleID localeID);
const char* locGetCrypticSpecific2LetterIdentifier(LocaleID localeID);

/// Return if LOCALE-ID is used at all by any shard running the
/// current game.
///
/// NOTE: A locale being implemented DOES NOT mean the current shard
/// supports that locale's language.  Consider calling
/// langIsSupportedThisShard().
bool locIsImplemented(LocaleID localeID);

/// Return if LANG is supported on the current shard.
///
/// On AppServers and GameServers, this is valid after auto-settings
/// are updated.  On GameClients, this is valid after connecting to a
/// LoginServer.
///
/// NOTE: A language not being supported DOES NOT mean that all shards
/// are the same.  Consider calling locIsImplemented().
bool langIsSupportedThisShard(Language lang);

/// Set the list of supported languages.
void langSetSupportedLanguagesThisShard(int* eaLang);

/// Get the internal EArray list of supported languages
const int* langGetSupportedLanguagesThisShard(void);

Language locGetAlternateLanguage(LocaleID localeID);
const char* locGetIETFLanguageTag(LocaleID localeID); // e.g. en-US

LocaleID locGetIDByName(const char* ID);
LocaleID locGetIDByLanguage(Language langID);
Language locGetAlternateLanguageFromLang(Language langID);
LocaleID locGetIDByCrypticSpecific2LetterIdentifier(const char* code);
LocaleID locGetIDByNameOrID(const char* str);
int locGetMaxLocaleCount(void);
bool locIDIsValid(LocaleID localeID);
LocaleID locGetIDByWindowsLocale(WindowsLocale windowsLocale);
WindowsLocale locGetWindowsLocale(LocaleID localeID);
Language locGetLanguage(LocaleID localeID);
const char* locGetDisplayName(LocaleID localeID);

LocaleID locGetIDInRegistry(void);
void locSetIDInRegistry(LocaleID localeID);

LocaleID getCurrentLocale(void);
void setCurrentLocale(LocaleID localeID);

//goes through the locale table, finds all the implemented locales, and
//puts copies of those locale names into an earray
void locGetImplementedLocaleNames(char ***pppOutLocaleNames);

void locProductNameWasJustSet(const char *pProductName);

#endif
