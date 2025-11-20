#ifndef _TRANSFORMATION_COMMON_
#define _TRANSFORMATION_COMMON_

#include "referencesystem.h"

AST_PREFIX(WIKI(AUTO))

typedef struct Entity Entity;
typedef struct PCGeometryDef PCGeometryDef;
typedef struct PCPart PCPart;
typedef struct PlayerCostume PlayerCostume;
typedef struct PCBoneDef PCBoneDef;

AUTO_ENUM;
typedef enum ETransformEffectColorOrigin
{
	ETransformEffectColorOrigin_NONE = 0,				ENAMES(none)
	ETransformEffectColorOrigin_SOURCE0,				ENAMES(source_0)
	ETransformEffectColorOrigin_SOURCE1,				ENAMES(source_1)
	ETransformEffectColorOrigin_SOURCE2,				ENAMES(source_2)
	ETransformEffectColorOrigin_SOURCE3,				ENAMES(source_3)
	ETransformEffectColorOrigin_DEST0,				ENAMES(destination_0)
	ETransformEffectColorOrigin_DEST1,				ENAMES(destination_1)
	ETransformEffectColorOrigin_DEST2,				ENAMES(destination_2)
	ETransformEffectColorOrigin_DEST3,				ENAMES(destination_3)
	ETransformEffectColorOrigin_COUNT
} ETransformEffectColorOrigin;

AUTO_STRUCT;
typedef struct TransformationEventDef
{
	// the time this event fires
	F32			fTime;							AST(NAME("Time"))

	// the bone	to perform the costume swap / removal on
	REF_TO(PCBoneDef)	hBoneDef;				AST(NAME("BoneSwapPart"))
	
	// the color source to pull from when playing the effect
	ETransformEffectColorOrigin	effectColorOrigin;

	// the effect to trigger on this event
	const char *pchEffect;						AST(NAME("Effect") POOL_STRING)

	// swaps the skin color on the costume to the dest costumes
	U32			swapSkinColor;

} TransformationEventDef;


AUTO_STRUCT WIKI("TransformationDef");
typedef struct TransformationDef
{
	// list of events that will be triggered
	TransformationEventDef **eaEventDef;		AST( NAME("Event"))

	// the total time of the transformation, 
	// after the time is expired, the costume will swap to the target costume
	F32 fTotalTime;								AST( NAME("TotalTime"))

	int bKeyFromSourceCostume;					AST( NAME("FromSourceCostume"))
	
	const char* pchName;						AST(KEY, STRUCTPARAM)
	char* pchFilename;							AST(CURRENTFILE)

} TransformationDef;


AUTO_STRUCT;
typedef struct CostumeTransformation
{
	PlayerCostume					*pCurrentCostume;	NO_AST

	PlayerCostume					*pSourceCostume;

	REF_TO(TransformationDef)		hDef;

	F32								fCurTime;			NO_AST
	U32								bIsTransforming;	NO_AST
} CostumeTransformation;


// 
void Transformation_SetTransformation(Entity *e, const char *pchTransformDef);
void Transformation_Destroy(CostumeTransformation **ppTrans);


__forceinline bool Transformation_IsColorOriginSource(ETransformEffectColorOrigin en)
{
	return en >= ETransformEffectColorOrigin_SOURCE0 && en <= ETransformEffectColorOrigin_SOURCE3;
}

__forceinline bool Transformation_IsColorOriginDest(ETransformEffectColorOrigin en)
{
	return en >= ETransformEffectColorOrigin_DEST0 && en <= ETransformEffectColorOrigin_DEST3;
}

__forceinline int Transformation_GetColorIndex(ETransformEffectColorOrigin en)
{
	if (Transformation_IsColorOriginSource(en))
	{
		return en - ETransformEffectColorOrigin_SOURCE0;
	}
	else if (Transformation_IsColorOriginDest(en))
	{
		return en - ETransformEffectColorOrigin_DEST0;
	}

	return -1;
}

#endif
