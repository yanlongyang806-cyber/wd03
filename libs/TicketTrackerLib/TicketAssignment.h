#ifndef TICKET_ASSIGNMENT_H
#define TICKET_ASSIGNMENT_H

// TODO(Theo) Deprecate/remove all of this

AUTO_ENUM;
typedef enum RuleScope
{
	RULES_GLOBAL = 0,
	RULES_GROUP,
} RuleScope;

AUTO_ENUM;
typedef enum ActionType
{
	ASSIGN_GROUP = 0,
	ASSIGN_USER, // removed
	ASSIGN_PRIORITY,
} ActionType;

AUTO_STRUCT;
typedef struct AssignmentFilter
{
	bool bDoesNotMatch; // match all strings that do _not_ contain the filter string?
	char *pFilter; AST(ESTRING)
} AssignmentFilter;

AUTO_STRUCT;
typedef struct AssignmentAction
{
	ActionType eAction;
	U32 uActionTarget;
} AssignmentAction;

AUTO_STRUCT AST_IGNORE(CategoryFilters);
typedef struct AssignmentRule
{
	char *pMainCategoryFilter; AST(ESTRING)
	char *pCategoryFilter; AST(ESTRING)

	char *pKeywordFilter; AST(ESTRING)
	char *pProductName; AST(ESTRING)

	// Actions
	AssignmentAction **ppActions;
	char *pName; AST(ESTRING)
	U32 uID;
} AssignmentRule;

AUTO_STRUCT;
typedef struct AssignmentRulesList
{
	RuleScope eScope;

	AssignmentRule **ppRules;
	U32 uNextID;
} AssignmentRulesList;

typedef struct TicketEntryConst_AutoGen_NoConst TicketEntry;

// always append, return index == count
int createNewAssignmentRule (RuleScope eScope, const char *pName, const char *pMainCategoryFilter, const char *pSubCategoryFilter, 
							 const char *pProductFilter, const char *pKeywordFilter, const char *pActionString);
int editAssignmentRule (U32 uID, const char *pName, const char *pMainCategoryFilter, const char *pSubCategoryFilter, 
						const char *pProductFilter, const char *pKeywordFilter, const char *pActionString);
int deleteAssignmentRule(U32 uID);

// Split GLOBAL and GROUP functions?
bool applyAssignmentRules (RuleScope eScope, TicketEntry *pEntry);

AssignmentRulesList * getGlobalRules(void);
AssignmentRulesList * getGroupRules(void);
AssignmentRulesList * getAssignmentRules(RuleScope eScope);

//void writeCategoryFiltersToString (char **estr, AssignmentFilter **ppCategoryFilters);
//AssignmentFilter ** readCategoryFiltersFromString (const char *pFilterString);

void writeActionsToString(char **estr, AssignmentAction **ppActions);
AssignmentAction ** readActionsFromString(const char *pActionString);

AssignmentRule *findAssignmentRuleByID(U32 uID);

U32 getActionTarget(AssignmentRule *pRule, ActionType eType);

bool loadAssignmentRules(void);
bool saveAssignmentRules(void);

#endif