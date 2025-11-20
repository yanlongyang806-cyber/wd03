#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynNode.h"

extern DictionaryHandle hDynAnimNodeAuxTransformListDict;

AUTO_STRUCT AST_ENDTOK("END");
typedef struct DynAnimNodeAuxTransform
{
	// I'm using the components to set a DynTransform so I can determine
	// which fields have been set and initialize the unset values without
	// having to change DynTransform's definition
	const char *pcNode;			AST(POOL_STRING KEY)
	Quat qRot;					AST(NAME(Rotation))
	Vec3 vPos;					AST(NAME(Position))
	Vec3 vScale;				AST(NAME(Scale))
	DynTransform xForm;			NO_AST
	U32	bfParamsSpecified[1];	AST( USEDFIELD )
}
DynAnimNodeAuxTransform;
extern ParseTable parse_DynAnimNodeAuxTransform[];
#define TYPE_parse_DynAnimNodeAuxTransform DynAnimNodeAuxTransform

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynAnimNodeAuxTransformList
{
	const char *pcName;		AST(STRUCTPARAM POOL_STRING KEY)
	const char *pcFileName;	AST(CURRENTFILE)
	const char *pcComments;	AST(SERVER_ONLY)
	const char *pcScope;	AST(SERVER_ONLY POOL_STRING NO_TEXT_SAVE)
	DynAnimNodeAuxTransform **eaTransforms; AST(NAME(AuxTransform))
	StashTable stAuxTransforms; NO_AST
}
DynAnimNodeAuxTransformList;
extern ParseTable parse_DynAnimNodeAuxTransformList[];
#define TYPE_parse_DynAnimNodeAuxTransformList DynAnimNodeAuxTransformList

void dynAnimNodeAuxTransformLoadAll(void);
const DynTransform *dynAnimNodeAuxTransform(const DynAnimNodeAuxTransformList *pList, const char *pcName);