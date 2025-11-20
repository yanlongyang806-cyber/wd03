#ifndef CATEGORY_H
#define CATEGORY_H
#pragma once
GCC_SYSTEM

#include "referencesystem.h"

typedef struct Message Message;

AUTO_STRUCT;
typedef struct CategoryName
{
	char *key; AST(KEY)
} CategoryName;

AUTO_STRUCT AST_IGNORE(Hidden);
typedef struct Category
{
	char *key; AST(KEY)
	REF_TO(Message) hDisplayName;			AST(NAME(DisplayName, hDisplayName) )
	REF_TO(Message) hDescriptionRef;		AST(NAME(Description, hDescriptionRef) )

	STRING_EARRAY ppSubCategoryNames; // stored as keys
	struct Category **ppSubCategories; NO_AST

	// TRUE if tickets created with this category should be PRIVATE by default
	bool bShowTicketsDefault;
} Category;

AUTO_STRUCT;
typedef struct CategoryList
{
	Category **ppCategories;

	STRING_EARRAY ppMainCategoryNames;
	Category **ppMainCategories; NO_AST
} CategoryList;

// This only has two levels
AUTO_STRUCT;
typedef struct CategoryMain
{
	Category category; AST(EMBEDDED_FLAT)
	Category **ppSubCategories; AST(UNOWNED)
} CategoryMain;

AUTO_STRUCT;
typedef struct CategoryListWrapper
{
	CategoryMain **ppCategories; AST(FORMATSTRING(XML_UNWRAP_ARRAY = 1))
} CategoryListWrapper;

int CategoryConvert (const char *oldMainCategory, SA_PARAM_NN_STR const char *oldCategory,
					 SA_PARAM_NN_VALID char **estrMainCategory, SA_PARAM_NN_VALID char **estrSubcategory);

Category *** getCategories(void);
void categorySetFile(const char * filename);
void categorySetPublicFile(const char * filename);
int categoryLoadAllCategories(void);

int categoryGetIndex(const char * pMSKey);
const char * categoryGetKey(int index);
const char * categoryGetTranslationFromIndex(int index);
const char * categoryGetTranslation(const char * pMSKey);

int subcategoryGetIndex (int wmainIndex, const char *pMSKey);
const char * subcategoryGetKey(int wmainIndex, int index);
const char * subcategoryGetTranslationFromIndex(int wmainIndex, int index);

Category * getMainCategory(const char *pMainCategoryKey);
Category * getCategory(const char *pMainCategoryKey, const char *pCategoryKey);
Category * getCategoryFromMain (Category *mainCategory, const char *pCategoryKey);

// Used for HTML lists of categories
const char *** const getCategoryStringsCombined(void);
void categoryConvertFromCombinedIndex (int index, int *mainIndex, int *subIndex);
int categoryConvertToCombinedIndex (int wmainIndex, int subIndex);

#endif