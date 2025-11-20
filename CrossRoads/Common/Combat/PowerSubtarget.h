/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef POWERSUBTARGET_H__
#define POWERSUBTARGET_H__
GCC_SYSTEM

// PowerSubtarget is a system which allows Powers to target specific subtargets
//  of a Character, however they may be attached to the Character.

#include "referencesystem.h"
#include "Message.h" // For DisplayMessage

// Forward declarations
typedef struct Character Character;
typedef struct PowerDef PowerDef;
typedef struct GameAccountDataExtract GameAccountDataExtract;

// Global dictionary handles
extern DictionaryHandle g_hPowerSubtargetCategoryDict;
extern DictionaryHandle g_hPowerSubtargetDict;

// Boolean if any PowerSubtarget data exists in this project (derived during load)
extern S32 g_bPowerSubtargets;


/***** ENUMS *****/

/***** END ENUMS *****/

// Category
AUTO_STRUCT;
typedef struct PowerSubtargetCategory
{
	const char *cpchName;						AST(STRUCTPARAM, KEY, POOL_STRING)
		// The internal name of the subtarget category

	DisplayMessage msgDisplayName;				AST(STRUCT(parse_DisplayMessage))
		// Message to display for the name

	const char *pchIconName;					AST(POOL_STRING)
		// Name of the icon

	const char *cpchFile;						AST(NAME(File), CURRENTFILE, EDIT_ONLY)
		// Current file (required for reloading)
} PowerSubtargetCategory;

// Single Subtarget
AUTO_STRUCT;
typedef struct PowerSubtarget
{
	const char *cpchName;						AST(STRUCTPARAM, KEY, POOL_STRING)
		// The internal name of the subtarget

	REF_TO(PowerSubtargetCategory) hCategory;	AST(NAME(Category))

	// Unimplemented future stuff:
	//  Size/Shape for automatic hit location/direction targeting
	//  Requires expression for restricting targeting
	//  Personal resistances

	const char *cpchFile;						AST(NAME(File), CURRENTFILE, EDIT_ONLY)
		// Current file (required for reloading)
} PowerSubtarget;

// Data sent over network involving status of PowerSubtargets or PowerSubtargetCategories
AUTO_STRUCT;
typedef struct PowerSubtargetNet
{
	const char *cpchName;					AST(POOL_STRING)
		// Internal name of the PowerSubtarget or PowerSubtargetCategory.  Not a ref, since the client
		//  loads all these anyway.

	S32 iHealth;
		// (S32) of individual PowerSubtarget health or entire PowerSubtargetCategory health

	S32 iHealthMax;
		// (S32) of individual PowerSubtarget max health or entire PowerSubtargetCategory max health

	U32 bCategory : 1;
		// If true, this is for a PowerSubtargetCategory, not a PowerSubtarget.  It would be possible
		//  to derive this rather than send it if we force the two dictionaries to share a namespace.

} PowerSubtargetNet;

// Structure used to track data about which subtarget is actually being targeted.
//  Right now only supports subtargeting an entire category.
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct PowerSubtargetChoice
{
	REF_TO(PowerSubtargetCategory) hCategory;	AST(PERSIST, NO_TRANSACT)
		// Subtarget Category chosen to be targeted

	F32 fAccuracy;								AST(PERSIST, NO_TRANSACT)
		// Accuracy parameter, used to determine the proportion of the effect that
		//  strikes subtargets

} PowerSubtargetChoice;


// Clears out the Character's subtarget choice
void character_ClearSubtarget(SA_PARAM_NN_VALID Character *pchar);

// Sets the Character's subtarget choice to the PowerSubtargetCategory
void character_SetSubtargetCategory(SA_PARAM_NN_VALID Character *pchar, SA_PARAM_NN_VALID PowerSubtargetCategory *pcat);

// Updates the Character's PowerSubtargetNet data
void character_SubtargetUpdateNet(SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Returns true if the subtarget matches the subtarget choice
S32 powersubtarget_MatchChoice(SA_PARAM_NN_VALID PowerSubtarget *pSubtarget, SA_PARAM_NN_VALID PowerSubtargetChoice *pChoice);

// Lets you know if the category exists
S32 powersubtarget_CategoryExists(const char *category);

// Gets the category
PowerSubtargetCategory* powersubtarget_GetCategoryByName(const char *pchCategory);

// Gets the PowerSubtarget
PowerSubtarget* powersubtarget_GetByName( const char* pchName );

#endif
