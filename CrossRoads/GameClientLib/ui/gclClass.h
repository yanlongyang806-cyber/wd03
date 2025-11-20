#ifndef GCLCLASS_H_
#define GCLCLASS_H_

//Nothing now

#include "textparser.h" // For StaticDefineInt
#include "StashTable.h" // For StashTable struct

AUTO_STRUCT;
typedef struct CharacterClassClientData
{
	const char* pchKey;	AST(STRUCTPARAM KEY)
	F32* eafData;		AST(NAME(Data))
} CharacterClassClientData;

extern DictionaryHandle g_hCharacterClassClientDataDict;

void characterclassclientdata_Load(void);

#endif //GCLCLASS_H_