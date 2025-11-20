#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Message.h"

typedef struct SpeciesDef SpeciesDef;

//List of letters/string that represent common sounds
AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct PhonemeSet
{
	CONST_STRING_POOLED pcName;			AST(PERSIST SUBSCRIBE STRUCTPARAM KEY POOL_STRING NAME("Name"))
	const char *pcScope;				AST(POOL_STRING)
	const char *pcFileName;				AST(CURRENTFILE)

	DisplayMessage displayNameMsg;		AST( STRUCT(parse_DisplayMessage) )

	CONST_STRING_EARRAY pcPhonemes;		AST(PERSIST SUBSCRIBE NAME("Phoneme"))

	//Editor only
	bool bIsNotInDict;
}PhonemeSet;

AUTO_STRUCT;
typedef struct PhonemeSetRef
{
	REF_TO(PhonemeSet)	hPhonemeSet;	AST(STRUCTPARAM REFDICT(PhonemeSet))
}PhonemeSetRef;

//Each sound in a word comes from the listed PhonemeSet
AUTO_STRUCT;
typedef struct NameTemplate
{
	PhonemeSetRef **eaPhonemeSets;		AST(NAME("PhonemeSet"))
	F32	fWeight;						AST(NAME("Weight") DEF(1.0f))
	U32 bitsBelongGroup;				AST(NAME("Belong") DEF(0xFFFFFFFF))
	U32 bitsRestrictGroup;				AST(NAME("Restrict") DEF(0xFFFFFFFF))
	int bAutoCapsFirstLetter : 1;		AST(NAME("AutoCaps") DEF(1))
}NameTemplate;

//Used for speciesGen
AUTO_STRUCT;
typedef struct NameTemplateNoRef
{
	PhonemeSet **eaPhonemeSets;			AST(NAME(Phoneme))
	F32	fWeight;						AST(NAME("Weight") DEF(1.0f))
	U32 bitsBelongGroup;				AST(NAME("Belong") DEF(0xFFFFFFFF))
	U32 bitsRestrictGroup;				AST(NAME("Restrict") DEF(0xFFFFFFFF))
	int bAutoCapsFirstLetter : 1;		AST(NAME("AutoCaps") DEF(1))
}NameTemplateNoRef;

//Used for custom species
AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct NameTemplatePhonemeSetNames
{
	CONST_STRING_EARRAY pcPhonemeSetNames;	AST(PERSIST SUBSCRIBE NAME("PhonemeSetName"))
	const F32 fWeight;						AST(PERSIST SUBSCRIBE NAME("Weight") DEF(1.0f))
	const U32 bitsBelongGroup;				AST(PERSIST SUBSCRIBE NAME("Belong") DEF(0xFFFFFFFF))
	const U32 bitsRestrictGroup;			AST(PERSIST SUBSCRIBE NAME("Restrict") DEF(0xFFFFFFFF))
	const int bAutoCapsFirstLetter : 1;		AST(PERSIST SUBSCRIBE NAME("AutoCaps") DEF(1))
}NameTemplatePhonemeSetNames;

//A random name template is chosen from this list using the given weight
AUTO_STRUCT;
typedef struct NameTemplateList
{
	const char *pcName;					AST(STRUCTPARAM KEY POOL_STRING NAME("Name"))
	const char *pcScope;				AST(POOL_STRING)
	const char *pcFileName;				AST(CURRENTFILE)

	DisplayMessage displayNameMsg;		AST( STRUCT(parse_DisplayMessage) )

	NameTemplate **eaNameTemplates;		AST(NAME(NameTemplate))
}NameTemplateList;

//Used for speciesGen
AUTO_STRUCT;
typedef struct NameTemplateListNoRef
{
	const char *pcName;					AST(STRUCTPARAM KEY POOL_STRING NAME("Name"))
	const char *pcScope;				AST(POOL_STRING)
	const char *pcFileName;				AST(CURRENTFILE)

	DisplayMessage displayNameMsg;		AST( STRUCT(parse_DisplayMessage) )

	NameTemplateNoRef **eaNameTemplates;		AST(NAME(NameTemplate))
}NameTemplateListNoRef;

AUTO_STRUCT AST_CONTAINER AST_DONT_INCLUDE_ACTUAL_FIELD_NAME_AS_REDUNDANT;
typedef struct NameTemplateListRef
{
	REF_TO(NameTemplateList)	hNameTemplateList;	AST(PERSIST SUBSCRIBE NAME("NameTemplateList") STRUCTPARAM REFDICT(NameTemplateList))
}NameTemplateListRef;
extern ParseTable parse_NameTemplateListRef[];
#define TYPE_parse_NameTemplateListRef NameTemplateListRef

extern ParseTable parse_PhonemeSet[];
#define TYPE_parse_PhonemeSet PhonemeSet
extern ParseTable parse_NameTemplateList[];
#define TYPE_parse_NameTemplateList NameTemplateList

extern DictionaryHandle g_hPhonemeSetDict;
extern DictionaryHandle g_hNameTemplateListDict;

AUTO_STRUCT;
typedef struct GenNameEntry
{
	char *pcName;
	char *pcFirstName;
	char *pcMiddleName;
	char *pcLastName;
} GenNameEntry;

AUTO_STRUCT;
typedef struct GenNameList
{
	const char *pcSpecies; AST(KEY POOL_STRING STRUCTPARAM)
	GenNameEntry **eaNames;
	int iAvailable;
} GenNameList;

AUTO_STRUCT;
typedef struct GenNameListSet
{
	GenNameList **eaList; AST(NAME(List))
} GenNameListSet;

AUTO_STRUCT;
typedef struct GenNameListReq
{
	const char *pcSpecies;
	int iListNames;
	int iNameGroupIndex;
} GenNameListReq;

//Each NameTemplateList passed in the earray is a word of the name
const char *namegen_GenerateFullName(NameTemplateListRef **eaNameTemplateLists, U32 *pSeed);
const char *namegen_GenerateName(NameTemplateListRef *pNameTemplateList, U32 *bitsRestrictGroup, U32 *pSeed);

#ifndef GAMECLIENT
// Generate a list of names
GenNameList *nameGen_GenerateRandomNamesInternal(GenNameListReq *pReq);
#endif

#ifdef GAMECLIENT
// Receive a list random names on the client
void nameGen_ClientReceiveNames(GenNameList *pGenNameList);
#endif
