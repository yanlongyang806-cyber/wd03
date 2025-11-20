#ifndef _EMOTE_COMMON_H_
#define _EMOTE_COMMON_H_
GCC_SYSTEM

#include "earray.h"
#include "EString.h"

AUTO_STRUCT;
typedef struct Emote
{
	char *estrName;					AST(ESTRING NAME(Name))
	char *estrEmoteKey;				AST(ESTRING NAME(EmoteKey))
	char *estrDescription;			AST(ESTRING NAME(Description))
	char *estrFailsRequirements;	AST(ESTRING NAME(FailsRequirements))
	U8 bCanUse : 1;					AST(NAME(CanUse))
} Emote;

AUTO_STRUCT;
typedef struct EmoteList
{
	Emote **eaEmotes;
} EmoteList;

#endif
