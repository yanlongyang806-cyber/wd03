#include "Category.h"
#include "Category_h_ast.h"

#include "Message.h"
#include "fileutil.h"
#include "foldercache.h"
#include "error.h"
#include "estring.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static CategoryList sgCategories = {0};
static CategoryList sgPublicCategories = {0};
static char **sppCategoryStringsCombined = NULL;

// Caches for XMPP
static CategoryListWrapper *spCategoryList = NULL;
static CategoryListWrapper *spCategoryPublicList = NULL;
AUTO_FIXUPFUNC;
TextParserResult fixupCategoryListWrapper(CategoryListWrapper *p, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		eaDestroy(&p->ppCategories);
	}
	return PARSERESULT_SUCCESS;
}

#define GET_CATEGORY(list,key) (eaIndexedGetUsingString(&(list)->ppCategories,key))

static char *sCategoryConversionMap[] = 
{
	// Category Mappings #1
	/*"CBug.Category.Mission",	"CBug.CategoryMain.InGame", "CBug.Category.InGame.Missions",
	"CBug.Category.Inventory",	"CBug.CategoryMain.InGame", "CBug.Category.InGame.Items",
	"CBug.Category.NPC",		"CBug.CategoryMain.InGame", "CBug.Category.InGame.Environment",
	"CBug.Category.UI",			"CBug.CategoryMain.InGame", "CBug.Category.InGame.UI",
	"CBug.Category.Environment","CBug.CategoryMain.InGame", "CBug.Category.InGame.Environment",
	"CBug.Category.Powers",		"CBug.CategoryMain.InGame", "CBug.Category.InGame.Powers",
	"CBug.Category.CBug",		"CBug.CategoryMain.InGame", "CBug.Category.InGame.Bugs",
	"CBug.Category.Other",		"CBug.CategoryMain.InGame", "CBug.Category.InGame.General",
	"CBug.Category.GMRequest",	"CBug.CategoryMain.InGame", "CBug.Category.InGame.General",
	"CBug.Category.Feedback",	"CBug.CategoryMain.InGame", "CBug.Category.InGame.General",
	"CBug.Category.Technical",	"CBug.CategoryMain.Technical", "CBug.Category.Tech.General",
	"CBug.Category.Art",		"CBug.CategoryMain.Technical", "CBug.Category.Tech.General",
	"CBug.Category.Audio",		"CBug.CategoryMain.Technical", "CBug.Category.Tech.Audio",
	"CBug.Category.Language",	"CBug.CategoryMain.Technical", "CBug.Category.Tech.General",
	"CBug.Category.Harassment",	"CBug.CategoryMain.Account", "CBug.Category.Account.Harrassment",
	"CBug.Category.IPViolation","CBug.CategoryMain.Account", "CBug.Category.Account.IPViolation",
	"CBug.Category.Other",		"CBug.CategoryMain.InGame", "CBug.Category.InGame.General",*/
	"CBug.Category.Mission",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Missions",
	"CBug.Category.Inventory",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Items",
	"CBug.Category.NPC",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Graphics",
	"CBug.Category.UI",			"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.UI",
	"CBug.Category.Environment","CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Graphics",
	"CBug.Category.Powers",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Powers",
	"CBug.Category.Audio",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Audio",
	"CBug.Category.Other",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Misc",
	"CBug.Category.Technical",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Graphics",
	"CBug.Category.Art",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Graphics",
	"CBug.Category.Feedback",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Misc",
	//"CBug.Category.CBug",		
	//"CBug.Category.Other",		"CBug.CategoryMain.InGame", "CBug.Category.InGame.General",
	"CBug.Category.GMRequest",	"CBug.CategoryMain.GM", "CBug.Category.GM.Misc",
	"CBug.Category.Language",	"CBug.CategoryMain.GM", "CBug.Category.GM.Behavior",
	"CBug.Category.Harassment",	"CBug.CategoryMain.GM", "CBug.Category.GM.Behavior",
	"CBug.Category.IPViolation","CBug.CategoryMain.GM", "CBug.Category.GM.Character",

	// Category Mappings #2
	// TODO
	//"CBug.Category.InGame.Account", 
	//"CBug.Category.InGame.Bugs", 
	"CBug.Category.InGame.Character",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Character", 
	"CBug.Category.InGame.Classes",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Character", 
	"CBug.Category.InGame.Combat",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Powers",
	"CBug.Category.InGame.Crafting",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Items",
	"CBug.Category.InGame.Environment", "CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Graphics",
	"CBug.Category.InGame.Instances",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Missions",
	"CBug.Category.InGame.Items",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Items",
	"CBug.Category.InGame.Missions",	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Missions",
	"CBug.Category.InGame.PVP",			"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Powers",
	"CBug.Category.InGame.Powers",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Powers",
	//"CBug.Category.InGame.Translation",
	//"CBug.Category.InGame.Teams", 
	"CBug.Category.InGame.UI",			"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.UI",
	//"CBug.Category.InGame.General",		"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Misc",

	"CBug.Category.Account.Exploit",	 "CBug.CategoryMain.GM", "CBug.Category.GM.Behavior", 
	"CBug.Category.Account.Policy",		 "CBug.CategoryMain.GM", "CBug.Category.GM.Behavior", 
	"CBug.Category.Account.IPViolation", "CBug.CategoryMain.GM", "CBug.Category.GM.Character", 
	"CBug.Category.Account.Harrassment", "CBug.CategoryMain.GM", "CBug.Category.GM.Behavior",
	"CBug.Category.Account.Compromised", "CBug.CategoryMain.GM", "CBug.Category.GM.Misc",
	"CBug.Category.Account.Inquiry",	 "CBug.CategoryMain.GM", "CBug.Category.GM.Misc",
	"CBug.Category.Account.General",	 "CBug.CategoryMain.GM", "CBug.Category.GM.Misc",

	"CBug.Category.Billing.Payment",  "CBug.CategoryMain.GM", "CBug.Category.GM.Misc",
	"CBug.Category.Billing.Creation", "CBug.CategoryMain.GM", "CBug.Category.GM.Misc",

	"CBug.Category.Billing.Keys",        "CBug.CategoryMain.Billing", "CBug.Category.Billing.Key",
	"CBug.Category.Billing.Creation",    "CBug.CategoryMain.Billing", "CBug.Category.Billing.Key", 
	"CBug.Category.Billing.Compromised", "CBug.CategoryMain.Billing", "CBug.Category.Billing.Other",

	//"CBug.Category.Tech.Patching", 
	"CBug.Category.Tech.Video",		 "CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Graphics",
	//"CBug.Category.Tech.Connection", 
	//"CBug.Category.Tech.Install", 
	//"CBug.Category.Tech.Game", 
	"CBug.Category.Tech.Audio",		 "CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Audio",
	//"CBug.Category.Tech.Crash", 
	//"CBug.Category.Tech.General", 
	
	""
};
static char *sCategoryConversionDefault[] = 
{
	//"CBug.CategoryMain.InGame", "CBug.Category.InGame.General"
	"CBug.CategoryMain.GameSupport", "CBug.Category.GameSupport.Misc"
};

int CategoryConvert (const char *mainCategory, const char *category, char **estrMainCategory, char **estrSubcategory)
{
	int i;
	if (mainCategory)
	{
		int iMainIndex = categoryGetIndex(mainCategory);
		if (iMainIndex >= 0)
		{
			if (!category || subcategoryGetIndex(iMainIndex, category) )
				return 0;
			// This category is up-to-date
			// Leaves the estrings NULL
		}
	}

	for (i=0; sCategoryConversionMap[i][0]; i+=3)
	{
		if (stricmp(category, sCategoryConversionMap[i]) == 0)
		{
			estrCopy2(estrMainCategory, sCategoryConversionMap[i+1]);
			estrCopy2(estrSubcategory, sCategoryConversionMap[i+2]);
			return 1;
		}
	}

	// No default map anymore
	//estrCopy2(estrMainCategory, sCategoryConversionDefault[0]);
	//estrCopy2(estrSubcategory, sCategoryConversionDefault[1]);			
	return 0; // not changed
}

__forceinline static void initializeCategories(const char **ppNames, Category ***eaCategories)
{
	int i, size;
	eaIndexedDisable(eaCategories);
	size = eaSize(&ppNames);
	for (i=0; i<size; i++)
	{
		Category *pCategory = GET_CATEGORY(&sgCategories, ppNames[i]);
		if (pCategory)
			eaPush(eaCategories, pCategory);
	}
}

Category *** getCategories(void)
{
	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
	return &sgCategories.ppMainCategories;
}

//static CategoryListWrapper *spCategoryList = NULL;
//static CategoryListWrapper *spCategoryPublicList = NULL;
static void categoryInitializeListWrapper(CategoryListWrapper *pWrapper, CategoryList *pList)
{
	int i, size;
	eaIndexedDisable(&pWrapper->ppCategories);
	size = eaSize(&pList->ppMainCategoryNames);
	for (i=0; i<size; i++)
	{
		Category *pCategory = GET_CATEGORY(pList, pList->ppMainCategoryNames[i]);
		if (pCategory)
		{
			CategoryMain *pMain = StructCreate(parse_CategoryMain);
			int j, subSize;
			pMain->category.key = strdup(pCategory->key);
			pMain->category.hDisplayName = pCategory->hDisplayName;
			pMain->category.hDescriptionRef = pCategory->hDescriptionRef;
			subSize = eaSize(&pCategory->ppSubCategoryNames);
			for (j=0; j<subSize; j++)
			{
				Category *pSubCategory = GET_CATEGORY(pList, pCategory->ppSubCategoryNames[j]);
				if (pSubCategory)
					eaPush(&pMain->ppSubCategories, pSubCategory);
			}
			eaPush(&pWrapper->ppCategories, pMain);
		}
	}
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN CategoryListWrapper* TT_GetCategoriesList()
{
	if (!spCategoryList)
	{
		spCategoryList = StructCreate(parse_CategoryListWrapper);
		categoryInitializeListWrapper(spCategoryList, &sgCategories);
	}
	/*if (!list || !eaSize(&list->ppCategories))
		Errorf("Failed to get Categories list");*/
	return spCategoryList;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
ACMD_STATIC_RETURN CategoryListWrapper* TT_GetPublicCategoriesList()
{
	if (!spCategoryPublicList && sgPublicCategories.ppMainCategoryNames)
	{
		spCategoryPublicList = StructCreate(parse_CategoryListWrapper);
		categoryInitializeListWrapper(spCategoryPublicList, &sgPublicCategories);
	}
	else if (!spCategoryList)
	{
		spCategoryList = StructCreate(parse_CategoryListWrapper);
		categoryInitializeListWrapper(spCategoryList, &sgCategories);
	}
	if (spCategoryPublicList)
		return spCategoryPublicList;
	return spCategoryList;
}
// TODO

// default is only the public names
#define DEFAULT_CATEGORY_FILE "public.category"
static char sCategoryFilename[64] = DEFAULT_CATEGORY_FILE; 

// This is used by TT for storing public category names
static char sPublicCategoryFilename[64] = "";

#ifndef GAMECLIENT
AUTO_CMD_STRING(sCategoryFilename, CategoryFile) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
AUTO_CMD_STRING(sPublicCategoryFilename, PublicCategory) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
#endif

void categorySetFile(const char * filename)
{
	sprintf(sCategoryFilename, "%s", filename);
}
void categorySetPublicFile(const char * filename)
{
	sprintf(sPublicCategoryFilename, "%s", filename);
}

static void categoryValidate (CategoryList *pList, Category *pCategory, int iLevel)
{
	int i;
	iLevel++;
	// If the nested level of categories goes above 10, someone needs to be shot
	devassertmsg(iLevel < 10, "Possible recursive Category definition.");
	for (i=eaSize(&pCategory->ppSubCategoryNames)-1; i>=0; i--)
	{
		const char *subcategoryKey = pCategory->ppSubCategoryNames[i];
		Category *pSubCategory = GET_CATEGORY(pList, subcategoryKey);
		if (!pSubCategory)
		{
			Errorf("Subcategory '%s' not found.", subcategoryKey);
			eaRemove(&pCategory->ppSubCategoryNames, i);
		}
		else
			categoryValidate(pList, pSubCategory, iLevel);
	}
}

static void categoryListValidate(CategoryList *pList)
{
	int i;
	for (i=eaSize(&pList->ppMainCategoryNames)-1; i>=0; i--)
	{
		const char *categoryKey = pList->ppMainCategoryNames[i];
		Category *pCategory = GET_CATEGORY(pList, categoryKey);
		if (!pCategory)
		{
			Errorf("Category '%s' not found.", categoryKey);
			eaRemove(&pList->ppMainCategoryNames, i);
		}
		else
			categoryValidate(pList, pCategory, 0);
	}
}

// Recursively cleans up all the category caches (does not actually destroy the structs)
static void cleanupCategory (Category *pCategory)
{
	if (pCategory->ppSubCategories)
		eaDestroyEx(&pCategory->ppSubCategories, cleanupCategory);
}

static void categoryReloadCallback(const char *relpath, int when)
{
	CategoryList newCategories = {0};
	bool bIsPublicFile = false;
	int result;

	loadstart_printf("Reloading Categories...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	if (!fileExists(relpath))
		; // File was deleted, do we care here?
	if (sPublicCategoryFilename[0] && strstri(relpath, sPublicCategoryFilename))
		bIsPublicFile = true;
	if (!bIsPublicFile && strstri(relpath, sCategoryFilename) == NULL)
		return; // do not reload on other files

	if (bIsPublicFile)
		result = ParserLoadFiles("ui",  sPublicCategoryFilename, "PublicCategories.bin", 0, parse_CategoryList, &newCategories);
	else
		result = ParserLoadFiles("ui",  sCategoryFilename, "Categories.bin", 0, parse_CategoryList, &newCategories);
		
	if(!result)
	{
		ErrorFilenamef(relpath, "Error reloading category file: %s", relpath);
	}
	else
	{
		if (bIsPublicFile)
		{
			eaDestroyEx(&sgPublicCategories.ppMainCategories, cleanupCategory);
			StructDeInit(parse_CategoryList, &sgPublicCategories);
			sgPublicCategories.ppCategories = newCategories.ppCategories;
			categoryListValidate(&sgPublicCategories);
			StructDestroy(parse_CategoryListWrapper, spCategoryPublicList);
			spCategoryPublicList = NULL;
		}
		else
		{
			eaDestroyEString(&sppCategoryStringsCombined);

			eaDestroyEx(&sgCategories.ppMainCategories, cleanupCategory);
			StructDeInit(parse_CategoryList, &sgCategories);
			sgCategories.ppCategories = newCategories.ppCategories;
			categoryListValidate(&sgCategories);
			StructDestroy(parse_CategoryListWrapper, spCategoryList);
			spCategoryList = NULL;
		}
	}

	loadend_printf("done");
}

AUTO_STARTUP(Category) ASTRT_DEPS(AS_Messages);
int categoryLoadAllCategories(void)
{
	static bool loadedOnce = false;
	int result;
	
	if (loadedOnce)
		return 1;

	result = ParserLoadFiles("ui", sCategoryFilename, "Categories.bin", PARSER_OPTIONALFLAG, parse_CategoryList, &sgCategories);
	if (!result && stricmp (sCategoryFilename, DEFAULT_CATEGORY_FILE))
	{
		strcpy(sCategoryFilename, DEFAULT_CATEGORY_FILE);
		result = ParserLoadFiles("ui", sCategoryFilename, "Categories.bin", PARSER_OPTIONALFLAG, parse_CategoryList, &sgCategories);
	}
	if (sPublicCategoryFilename[0])
	{
		if (stricmp(sCategoryFilename, sPublicCategoryFilename))
		{
			ParserLoadFiles("ui", sPublicCategoryFilename, "PublicCategories.bin", PARSER_OPTIONALFLAG, parse_CategoryList, &sgPublicCategories);
			categoryListValidate(&sgPublicCategories);
		}
		else
			sPublicCategoryFilename[0] = '\0'; // they're the same bloody file!
	}
	categoryListValidate(&sgCategories);

	if (!result)
		return 0;
#if !_PS3
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/*.category", categoryReloadCallback);
	}
#endif
	loadedOnce = true;

	return 1;
}

//////////////////////////////////////////////////////////////
// Functions to translate Message Keys to and from Indices

int categoryGetIndex(const char * pMSKey)
{	
	int i, size;
	if (!pMSKey)
		return -1;
	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
	size = eaSize(&sgCategories.ppMainCategories);
	for (i=0; i<size; i++)
	{
		Message *ms = GET_REF(sgCategories.ppMainCategories[i]->hDisplayName);
		if (ms && stricmp(ms->pcMessageKey, pMSKey) == 0)
			return i;
	}
	return -1;
}

const char * categoryGetKey(int index)
{
	Message * msg;
	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
	if (index < 0 || index >= eaSize(&sgCategories.ppMainCategories))
		return "";
	msg = GET_REF(sgCategories.ppMainCategories[index]->hDisplayName);
	return msg ? msg->pcMessageKey : "";
}

const char * categoryGetTranslationFromIndex(int index)
{
	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
	if (index < 0 || index >= eaSize(&sgCategories.ppMainCategories))
		return "";
	return TranslateMessageRef(sgCategories.ppMainCategories[index]->hDisplayName);
}

const char * categoryGetTranslation(const char * pMSKey)
{
	return TranslateMessageKeyDefault(pMSKey, pMSKey);
}

int subcategoryGetIndex (int mainIndex, const char *pMSKey)
{
	int i, size;
	Category *main;

	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);

	if (!pMSKey || mainIndex < 0 || mainIndex >= eaSize(&sgCategories.ppMainCategories))
		return -1;
	main = sgCategories.ppMainCategories[mainIndex];
	if (!main->ppSubCategories)
		initializeCategories(main->ppSubCategoryNames, &main->ppSubCategories);
	size = eaSize(&main->ppSubCategories);
	for (i=0; i<size; i++)
	{
		Message *ms = GET_REF(main->ppSubCategories[i]->hDisplayName);
		if (ms && stricmp(ms->pcMessageKey, pMSKey) == 0)
			return i;
	}
	return -1;
}

const char * subcategoryGetKey(int mainIndex, int index)
{
	Message * msg;
	Category *main;

	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);

	if (mainIndex < 0 || mainIndex >= eaSize(&sgCategories.ppMainCategories))
		return "";

	main = sgCategories.ppMainCategories[mainIndex];
	if (!main->ppSubCategories)
		initializeCategories(main->ppSubCategoryNames, &main->ppSubCategories);

	if (index < 0 || index >= eaSize(&main->ppSubCategories))
		return "";
	msg = GET_REF(main->ppSubCategories[index]->hDisplayName);
	return msg ? msg->pcMessageKey : "";
	return NULL;
}

const char * subcategoryGetTranslationFromIndex(int mainIndex, int index)
{
	Category *main;
	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);

	if (mainIndex < 0 || mainIndex >= eaSize(&sgCategories.ppMainCategories))
		return "";

	main = sgCategories.ppMainCategories[mainIndex];
	if (!main->ppSubCategories)
		initializeCategories(main->ppSubCategoryNames, &main->ppSubCategories);

	if (index < 0 || index >= eaSize(&main->ppSubCategories))
		return "";
	return TranslateMessageRef(main->ppSubCategories[index]->hDisplayName);
}

// pParentString includes the name for pCategory
static void appendCategoryStrings (char ***eaCategoryString, Category *pCategory, const char *pParentString)
{
	int i, subSize;
	char *onlyMain = NULL;

	if (!pCategory->ppSubCategories)
		initializeCategories(pCategory->ppSubCategoryNames, &pCategory->ppSubCategories);
	subSize = eaSize(&pCategory->ppSubCategories);

	if (subSize == 0)
	{
		char *parentCopy = NULL;
		estrCopy2(&parentCopy, pParentString);
		eaPush(&sppCategoryStringsCombined, parentCopy);
		return;
	}

	estrPrintf(&onlyMain, "%s (all)", pParentString);
	eaPush(&sppCategoryStringsCombined, onlyMain);

	for (i=0; i<subSize; i++)
	{
		char *combined = NULL;
		char *subName = (char*) TranslateMessageRef(pCategory->ppSubCategories[i]->hDisplayName);

		estrPrintf(&combined, "%s - %s", pParentString, subName ? subName : "[Unknown]");
		appendCategoryStrings(eaCategoryString, pCategory->ppSubCategories[i], combined);
		estrDestroy(&combined);
		//eaPush(&sppCategoryStringsCombined, combined);
	}
}

const char *** const getCategoryStringsCombined(void)
{
	if (!sppCategoryStringsCombined)
	{
		int i, size;

		if (!sgCategories.ppMainCategories)
			initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
		size = eaSize(&sgCategories.ppMainCategories);
		for (i=0; i<size; i++)
		{
			Category *main = sgCategories.ppMainCategories[i];
			char *mainName = (char*) TranslateMessageRef(main->hDisplayName);

			appendCategoryStrings(&sppCategoryStringsCombined, main, mainName ? mainName : "[Unknown]");
		}
	}
	return &sppCategoryStringsCombined;
}


void categoryConvertFromCombinedIndex (int index, int *mainIndex, int *subIndex)
{
	if (index >= 0)
	{
		int i, mainSize, count = 0;

		if (!sgCategories.ppMainCategories)
			initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
		mainSize = eaSize(&sgCategories.ppMainCategories);
		for (i=0; i < mainSize; i++)
		{
			Category *main = sgCategories.ppMainCategories[i];
			int subSize;

			if (!main->ppSubCategories)
				initializeCategories(main->ppSubCategoryNames, &main->ppSubCategories);
			subSize = eaSize(&main->ppSubCategories) + 1;
			if (count <= index && index < count + subSize)
			{
				*mainIndex = i;
				*subIndex = index - count - 1; // 0th subindex = do-not-match subcategory
				return;
			}
			count += subSize;
		}
	}
	*mainIndex = -1;
	*subIndex = -1;
}

int categoryConvertToCombinedIndex (int mainIndex, int subIndex)
{
	int i, count = 0;
	int mainSize, subSize;

	if (mainIndex < 0)
		return -1;

	if (!sgCategories.ppMainCategories)
		initializeCategories(sgCategories.ppMainCategoryNames, &sgCategories.ppMainCategories);
	mainSize = eaSize(&sgCategories.ppMainCategories);
	if (mainIndex >= mainSize)
		return -1;

	for (i=0; i < mainIndex; i++)
	{
		Category *main = sgCategories.ppMainCategories[i];
		if (!main->ppSubCategories)
			initializeCategories(main->ppSubCategoryNames, &main->ppSubCategories);
		count += eaSize(&main->ppSubCategories) + 1;
	}
	if (!sgCategories.ppMainCategories[mainIndex]->ppSubCategories)
		initializeCategories(sgCategories.ppMainCategories[mainIndex]->ppSubCategoryNames, 
			&sgCategories.ppMainCategories[mainIndex]->ppSubCategories);
	subSize = eaSize(&sgCategories.ppMainCategories[mainIndex]->ppSubCategories);
	if (subIndex >= subSize)
		return -1;
	return (count + subIndex + 1);
}

Category * getMainCategory(const char *pMainCategoryKey)
{
	int iMainCategoryIndex = categoryGetIndex(pMainCategoryKey);
	if (iMainCategoryIndex >= 0)
	{
		if (!sgCategories.ppMainCategories[iMainCategoryIndex]->ppSubCategories)
			initializeCategories(sgCategories.ppMainCategories[iMainCategoryIndex]->ppSubCategoryNames, 
				&sgCategories.ppMainCategories[iMainCategoryIndex]->ppSubCategories);

		return sgCategories.ppMainCategories[iMainCategoryIndex];
	}
	return NULL;
}

Category * getCategory(const char *pMainCategoryKey, const char *pCategoryKey)
{
	int iMainCategoryIndex = categoryGetIndex(pMainCategoryKey);
	int iCategoryIndex = subcategoryGetIndex(iMainCategoryIndex, pCategoryKey);
	
	if (iMainCategoryIndex >= 0 && iCategoryIndex >= 0)
	{
		return sgCategories.ppMainCategories[iMainCategoryIndex]->ppSubCategories[iCategoryIndex];
	}
	return NULL;
}

Category * getCategoryFromMain (Category *mainCategory, const char *pCategoryKey)
{
	int i, size;
	if (!mainCategory)
		return NULL;

	if (!mainCategory->ppSubCategories)
		initializeCategories(mainCategory->ppSubCategoryNames, &mainCategory->ppSubCategories);
	size = eaSize(&mainCategory->ppSubCategories);
	for (i=0; i<size; i++)
	{
		Message *ms = GET_REF(mainCategory->ppSubCategories[i]->hDisplayName);
		if (ms && stricmp(ms->pcMessageKey, pCategoryKey) == 0)
			return mainCategory->ppSubCategories[i];
	}
	return NULL;
}

#include "Category_h_ast.c"