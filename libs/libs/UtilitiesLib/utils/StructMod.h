#ifndef _STRUCTMOD_H_
#define _STRUCTMOD_H_

#include "stdtypes.h"

AUTO_STRUCT;
typedef struct StructMod
{
	ParseTable* table;	NO_AST
	int column;
	void* ptr;			NO_AST
	int idx;

	int id;

	const char* val;
	const char* name;
} StructMod;

StructMod* structModCreate(void);
void structModDestroy(StructMod* mod);

int structModResolvePath(void* ptr, ParseTable *tableIn, const char* relObjPath, StructMod *mod);

void structModApply(StructMod *mod);

#endif