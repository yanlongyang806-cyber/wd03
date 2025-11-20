#ifndef __WORLDEDITOROPTIONS_H__
#define __WORLDEDITOROPTIONS_H__
GCC_SYSTEM

typedef struct UITab UITab;
typedef struct EditorObject EditorObject;
typedef enum InfoWindowData InfoWindowData;

/******
* World Editor Options defines the EMOptionsTab's that are pushed onto the world editor's options_tabs
* EArray for display in the global options dialog window.
*
* This file also contains constants used by various preference-related code.
******/

// Preference strings
#define WLE_PREF_EDITOR_NAME "WorldEditor"
#define WLE_PREF_CAT_OPTIONS "Options"
#define WLE_PREF_CAT_UI	"UI"

/********************
* FILTERS
********************/
typedef struct WleCriterion WleCriterion;

AUTO_ENUM;
typedef enum WleCriterionCond
{
	WLE_CRIT_EQUAL,
	WLE_CRIT_NOT_EQUAL,
	WLE_CRIT_LESS_THAN,
	WLE_CRIT_GREATER_THAN,
	WLE_CRIT_CONTAINS,
	WLE_CRIT_BEGINS_WITH,
	WLE_CRIT_ENDS_WITH,
} WleCriterionCond;

typedef bool (*WleCriterionCheck)(EditorObject *obj, const char *propertyName, WleCriterionCond cond, const char *val, void *data);

AUTO_STRUCT;
typedef struct WleCriterion
{
	char *propertyName;
	WleCriterionCond *allConds;
	char **possibleValues;

	WleCriterionCheck checkCallback;		NO_AST
	void *checkData;						NO_AST
} WleCriterion;

AUTO_STRUCT;
typedef struct WleFilterCriterion
{
	WleCriterion *criterion;				NO_AST
	char *propertyName;
	WleCriterionCond cond;
	char *val;
} WleFilterCriterion;

#define WLE_FILTER_VOLUMES					1
#define WLE_FILTER_SPAWNPOINTS				(1 << 1)
#define WLE_FILTER_INTERACTABLES			(1 << 2)
#define WLE_FILTER_LIGHTS					(1 << 3)
#define WLE_FILTER_ROOMS					(1 << 4)
#define WLE_FILTER_PATROLS					(1 << 5)
#define WLE_FILTER_PATROL_POINTS			(1 << 6)
#define WLE_FILTER_ENCOUNTERS				(1 << 7)
#define WLE_FILTER_ACTORS					(1 << 8)

AUTO_STRUCT;
typedef struct WleFilter
{
	// Global data
	char *name;
	bool ignoreNodeState;					AST(DEFAULT(true))

	// Target(s)
	int affectType;				// 0 = all; 1 = include; 2 = exclude
	U32 filterTargets;

	// Criteria list
	WleFilterCriterion **criteria;
} WleFilter;

AUTO_STRUCT;
typedef struct WleFilterList
{
	WleFilter **filters;
} WleFilterList;

extern ParseTable parse_WleCriterion[];
#define TYPE_parse_WleCriterion WleCriterion

#ifndef NO_EDITORS

/********************
* SEARCH FILTERS
********************/
void wleCriterionRegister(WleCriterion *criterion);
WleCriterion *wleCriterionGet(const char *propertyName);
void wleOptionsFilterEdit(WleFilter *filter);
bool wleFilterApply(SA_PARAM_NN_VALID EditorObject *obj, SA_PARAM_OP_VALID WleFilter *filter);

bool wleCriterionStringTest(const char *string, const char *critString, WleCriterionCond cond, bool *output);
bool wleCriterionNumTest(const float val, const float critVal, WleCriterionCond cond, bool *output);

/********************
* MAIN
********************/
void wleOptionsRegisterTabs(void);

extern void wleOptionsVolumesLoadPrefs();

#endif // NO_EDITORS

#endif // __WORLDEDITOROPTIONS_H__