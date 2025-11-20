//// Choice Tables allow for extra randomness at run time.
////
//// Choice Tables are WorldVariables that get calculated at runtime
//// by looking up in a randomized, weighted table. (Think the Lone
//// Wolf adventure book series.)  They can include any value that a
//// WorldVariable could include.
////
//// Choice Tables can also reference other Choice Tables for
//// organizational purposes.
#pragma once
GCC_SYSTEM

#include "WorldVariable.h"
#include "ExpressionFunc.h"

typedef struct ChoiceEntry ChoiceEntry;
typedef struct ChoiceValue ChoiceValue;

AUTO_ENUM;
typedef enum TimeInterval
{
	kTimeInterval_Minute	=		60,
	kTimeInterval_Hour		=		3600,
	kTimeInterval_Day		=		86400,
} TimeInterval;
extern StaticDefineInt TimeIntervalEnum[];

/// The value is computed differently depending on the type.
///
/// Value   - Directly holds ChoiceValues
/// Include - include an entire other Choice Table
AUTO_ENUM;
typedef enum ChoiceEntryType
{
	CET_None, EIGNORE
	CET_Value,
	CET_Include,
} ChoiceEntryType;
extern StaticDefineInt ChoiceEntryTypeEnum[];

/// This specific value is computed differently depending on the type
///
/// Value	- Directly holds WorldVariables
/// Choice	- Lookup in another choice table.
AUTO_ENUM;
typedef enum ChoiceValueType
{
	CVT_None, EIGNORE
	CVT_Value,
	CVT_Choice,
} ChoiceValueType;
extern StaticDefineInt ChoiceValueTypeEnum[];

/// These types determine how the choice entry is selected
///
/// Random			- Uniformly random selection of entries
/// Timed Random	- Randomly chosen at selected periods of time
AUTO_ENUM;
typedef enum ChoiceSelectType
{
	CST_Random,
	CST_TimedRandom,
} ChoiceSelectType;
extern StaticDefineInt ChoiceSelectTypeEnum[];

/// Define the type for one named value in a choice table.
AUTO_STRUCT;
typedef struct ChoiceTableValueDef
{
	const char* pchName;		AST( STRUCTPARAM NAME(Name) POOL_STRING )
	WorldVariableType eType;	AST( STRUCTPARAM NAME(Type) )
} ChoiceTableValueDef;
extern ParseTable parse_ChoiceTableValueDef[];
#define TYPE_parse_ChoiceTableValueDef ChoiceTableValueDef

/// The top level structure for ChoiceTables.
AUTO_STRUCT;
typedef struct ChoiceTable
{
	const char* pchName;			AST( NAME(Name) STRUCTPARAM KEY POOL_STRING )
	const char* pchFileName;		AST( CURRENTFILE )
	const char* pchScope;			AST( NAME(Scope) POOL_STRING )
	char* pchNotes;					AST( NAME(Notes) )

	ChoiceSelectType eSelectType;	AST( NAME(SelectType) )
	TimeInterval eTimeInterval;		AST( NAME(TimeInterval) )
	int iValuesPerInterval;			AST( NAME(ValuesPerInterval) )
	int iNumUniqueIntervals;		AST( NAME(NumUniqueIntervals) )

	ChoiceTableValueDef **eaDefs;	AST( NAME(Def) )
	ChoiceEntry **eaEntry;			AST( NAME(Entry) )
} ChoiceTable;
extern ParseTable parse_ChoiceTable[];
#define TYPE_parse_ChoiceTable ChoiceTable

/// A single entry in the Choice Table.
AUTO_STRUCT;
typedef struct ChoiceEntry
{
	ChoiceEntryType eType;				AST( NAME(Type) DEF(CET_Value) )
	F32 fWeight;						AST( NAME(Weight) DEF(1) )

	ChoiceValue **eaValues;				AST( NAME(Value) )
	REF_TO(ChoiceTable) hChoiceTable;	AST( NAME(ChoiceTable) )
} ChoiceEntry;
extern ParseTable parse_ChoiceEntry[];
#define TYPE_parse_ChoiceEntry ChoiceEntry

/// A single value in the Choice Table.
AUTO_STRUCT;
typedef struct ChoiceValue
{
	ChoiceValueType eType;				AST( NAME(Type) DEF(CVT_Value) )

	WorldVariable value;				AST( NAME(Value) STRUCT(parse_WorldVariable) )
	
	REF_TO(ChoiceTable) hChoiceTable;	AST( NAME(ChoiceTable) )
	char* pchChoiceName;				AST( NAME(ChoiceName) )
	int iChoiceIndex;					AST( NAME(ChoiceIndex) )
} ChoiceValue;
extern ParseTable parse_ChoiceValue[];
#define TYPE_parse_ChoiceValue ChoiceValue

extern DictionaryHandle g_hChoiceTableDict;
extern WorldVariable choice_defaultValue;

bool choice_Validate( SA_PARAM_NN_VALID ChoiceTable* table );

int choice_ValueIndex( SA_PARAM_NN_VALID ChoiceTable* table, const char* name );
WorldVariableType choice_ValueType( SA_PARAM_NN_VALID ChoiceTable* table, const char* name );
char** choice_ListNames( SA_PARAM_NN_STR const char* tableName );
void choice_Load( void );
int choice_TotalEntries(SA_PARAM_NN_VALID ChoiceTable *table);
int choice_TotalEntriesEx(SA_PARAM_NN_VALID ChoiceTable *table, ChoiceEntry ***pppEntriesOut);

int choice_TimedRandomRange(SA_PARAM_NN_VALID ChoiceTable *table);
int choice_TimedRandomValuesPerInterval(SA_PARAM_NN_VALID ChoiceTable *table);
int choice_TimedRandomNumIntervals(SA_PARAM_NN_VALID ChoiceTable *table);

SA_RET_NN_VALID WorldVariable* choice_ChooseValueForClient( SA_PARAM_NN_VALID ChoiceTable* table, const char* name );

#define MAX_DAILY_TABLE_RANDOM_RANGE 999
#define MAX_DAILY_TABLE_VALUES_PER_INTERVAL 99
#define MAX_DAILY_TABLE_INTERVALS 100