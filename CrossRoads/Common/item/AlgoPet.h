/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef ALGOPET_H
#define ALGOPET_H

#include "referencesystem.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct PTNodeDefRefCont PTNodeDefRefCont;
typedef struct PTNodeDef PTNodeDef;
typedef struct PetDef PetDef;
typedef struct SpeciesDefRef SpeciesDefRef;
typedef struct AllegianceDef AllegianceDef;

typedef struct NOCONST(AlgoPet) NOCONST(AlgoPet);

extern DefineContext *g_pAlgoCategory;
extern DictionaryHandle g_hAlgoPetDict;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pAlgoCategory);
typedef enum AlgoCategory
{
	AlgoPetCat_Base = 0, ENAMES(Base)
}AlgoCategory;

AUTO_STRUCT;
typedef struct AlgoCategoryNames
{
	const char **pchNames; AST(NAME(AlgoCatName))

} AlgoCategoryNames;

AUTO_STRUCT;
typedef struct AlgoPetPowerDef {
	REF_TO(PTNodeDef) hPowerNode;		AST(REFDICT(PowerTreeNodeDef) NAME(PowerNode) STRUCTPARAM)
	U32 *puiCategory;					AST(NAME(Category) SUBTABLE(AlgoCategoryEnum))
	U32 *puiExclusiveCategory;			AST(NAME(ExclusiveCategory) SUBTABLE(AlgoCategoryEnum))
	F32 fWeight;						AST(DEFAULT(1.0f))
	Expression *pExprWeightMulti;		AST(LATEBIND)
}AlgoPetPowerDef;

AUTO_STRUCT;
typedef struct AlgoPetPowerChoice {
	U32 *puiCategory;					AST(NAME(Category) SUBTABLE(AlgoCategoryEnum))
}AlgoPetPowerChoice;

AUTO_STRUCT;
typedef struct AlgoPetPowerQuality {
	ItemQuality uiRarity;				AST(NAME(ItemRarity) STRUCTPARAM) 
	U32 *puiSharedCategories;			AST(NAME(SharedCategory) SUBTABLE(AlgoCategoryEnum))
	AlgoPetPowerChoice **ppChoices;		AST(NAME(PowerChoice))
}AlgoPetPowerQuality;


//Everything here is used for editor purposes only, and will not load or work
//in any other part of the code except AlgoPetEditor files.
AUTO_STRUCT;
typedef struct AlgoPetEditorPowerChoice {
	ItemQuality uiRarity;				AST(NAME(Rarity))
	U32 *puiSharedCategories;			AST(NAME(SharedCategory) SUBTABLE(AlgoCategoryEnum))
	U32 *puiCategory;					AST(NAME(Category) SUBTABLE(AlgoCategoryEnum))
}AlgoPetEditorPowerChoice;

AUTO_STRUCT;
typedef struct CostumeRefForAlgoPet {
	REF_TO(PlayerCostume) hPlayerCostume;   AST(STRUCTPARAM REFDICT(PlayerCostume))
	F32 fWeight;							AST(STRUCTPARAM DEFAULT(1.0f))
} CostumeRefForAlgoPet;

AUTO_STRUCT;
typedef struct AlgoPetDef {
	char *pchName;						AST(KEY STRUCTPARAM POOL_STRING)
	char *pchScope;						AST(NAME(Scope) POOL_STRING)
	char *pchFileName;					AST(CURRENTFILE)
	AlgoPetPowerDef **ppPowers;			AST(NAME(Power))
	AlgoPetPowerQuality **ppPowerQuality; AST(NAME(Quality))

	SpeciesDefRef **eaSpecies;			AST(NAME(Species))
	CostumeRefForAlgoPet **eaUniforms;	AST(NAME(UniformOverlay))
	CONST_INT_EARRAY eaExcludeSpeciesClassType;		AST(SUBTABLE(CharClassTypesEnum))

	//Everything here is used for editor purposes only, and will not load or work
	//in any other part of the code except AlgoPetEditor files.
	AlgoPetEditorPowerChoice **ppPowerChoices; AST(NO_TEXT_SAVE NO_INDEX)

}AlgoPetDef;

AUTO_STRUCT AST_CONTAINER AST_IGNORE(hAllegiance);
typedef struct AlgoPet {
	REF_TO(AlgoPetDef) hAlgoPet;							AST(PERSIST SUBSCRIBE REFDICT(AlgoPetDef) )
	CONST_EARRAY_OF(PTNodeDefRefCont) ppEscrowNodes;		AST(PERSIST SUBSCRIBE FORCE_CONTAINER)	
	const int iCostume;										AST(PERSIST SUBSCRIBE NAME(Costume) DEFAULT(-1.0f) )
	CONST_OPTIONAL_STRUCT(PlayerCostume) pCostume;			AST(PERSIST SUBSCRIBE NAME(RandomCostume) )
	REF_TO(SpeciesDef) hSpecies;							AST(PERSIST SUBSCRIBE REFDICT(Species) )
	CONST_STRING_MODIFIABLE pchPetName;						AST(PERSIST SUBSCRIBE NAME(PetName) )
	CONST_STRING_MODIFIABLE pchPetSubName;					AST(PERSIST SUBSCRIBE NAME(PetSubName) )
}AlgoPet;

NOCONST(AlgoPet) *algoPetDef_CreateNew(AlgoPetDef *pDef, PetDef *pPetDef, U32 uiRarity, int iLevel, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, U32 *pSeed);

const char *algoPetDef_GenerateRandomName(SpeciesDef *pSpecies, const char **ppcSubName, U32 *pSeed);

bool algoPetDef_Validate(AlgoPetDef *pDef);

ExprContext *algoPetGetContext(void);

#endif