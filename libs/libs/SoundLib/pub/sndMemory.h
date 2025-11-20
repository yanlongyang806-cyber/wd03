/***************************************************************************



***************************************************************************/

#pragma once

#ifndef _SNDMEMORY_H
#define _SNDMEMORY_H
GCC_SYSTEM

#include "stdtypes.h"

typedef struct SoundSource SoundSource; 
typedef struct SoundObject SoundObject;

typedef struct AudioMemData {
	int totalmem;
} AudioMemData;

AUTO_STRUCT;
typedef struct AudioMemEntry {
	SoundObject *object;  NO_AST
	const char *file_name;  AST(POOL_STRING)
	const char *desc_name; AST(POOL_STRING)
	const char *orig_name; AST(POOL_STRING)
	int total_mem;
} AudioMemEntry;

extern ParseTable parse_AudioMemEntry[];
#define TYPE_parse_AudioMemEntry AudioMemEntry

extern AudioMemData sndMemData;

void sndMemInit(void);

void sndMemGetUsage(AudioMemEntry ***entries);

#endif