#pragma once
GCC_SYSTEM

#include "message.h"
#include "memcheck.h"

typedef U32 ContainerID;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct MissionDef MissionDef;
typedef struct ParseTable ParseTable;
typedef struct StaticDefineInt StaticDefineInt;
typedef struct CritterDef CritterDef;
typedef struct CritterGroup CritterGroup;
typedef struct ChoiceTable ChoiceTable;
typedef struct FSMExternVar FSMExternVar;
typedef struct NOCONST(WorldVariableContainer) NOCONST(WorldVariableContainer);

// Hardcoded map variable names
#define FORCESIDEKICK_MAPVAR_MIN "ForceSidekickMinLevel"
#define FORCESIDEKICK_MAPVAR_MAX "ForceSidekickMaxLevel"

// If set, when leaving the map, always return to the last static map instead of going to the intended destination
#define FORCEMISSIONRETURN_MAPVAR "ForceMissionReturn"  

// If set, this map is a Flashback Mission for the specified MissionDef
#define FLASHBACKMISSION_MAPVAR "FlashbackMission"
	

// VARIABLE_TYPES: Add variable type here if add to the available variable types
AUTO_ENUM;
typedef enum WorldVariableType {
	WVAR_NONE,
	WVAR_INT,
	WVAR_FLOAT,
	WVAR_STRING,
	WVAR_LOCATION_STRING,		ENAMES(LOCATION_STRING NAMED_POINT)
	WVAR_MESSAGE,
	WVAR_ANIMATION,
	WVAR_CRITTER_DEF,
	WVAR_CRITTER_GROUP,
	WVAR_MAP_POINT,
	WVAR_ITEM_DEF,
	WVAR_MISSION_DEF,
	
	WVAR_PLAYER,  // This type is special, and can't be set in data
} WorldVariableType;
extern StaticDefineInt WorldVariableTypeEnum[];

AUTO_ENUM;
typedef enum WorldVariableDefaultValueType {
	WVARDEF_SPECIFY_DEFAULT,
	WVARDEF_CHOICE_TABLE,
	WVARDEF_MAP_VARIABLE,
	WVARDEF_MISSION_VARIABLE,
	WVARDEF_EXPRESSION,
	WVARDEF_ACTIVITY_VARIABLE,
} WorldVariableDefaultValueType;
extern StaticDefineInt WorldVariableDefaultValueTypeEnum[];


// This structure is used to persist a variable value
// NOTE: This should be consistent with "WorldVariable" (but not necessarily identical)
AUTO_STRUCT AST_CONTAINER;
typedef struct WorldVariableContainer
{
	CONST_STRING_POOLED pcName;						AST( PERSIST SUBSCRIBE NAME("Name") STRUCTPARAM POOL_STRING KEY)
	const WorldVariableType eType;					AST( PERSIST SUBSCRIBE NAME("Type") )

	CONST_STRING_MODIFIABLE pcZoneMap;				AST( PERSIST SUBSCRIBE NAME("ZoneMap") )
	const int iIntVal;								AST( PERSIST SUBSCRIBE NAME("IntVal") )
	const float fFloatVal;							AST( PERSIST SUBSCRIBE NAME("FloatVal") )
	CONST_STRING_MODIFIABLE pcStringVal;			AST( PERSIST SUBSCRIBE NAME("StringVal") )
	REF_TO(Message) hMessage;						AST( PERSIST SUBSCRIBE NAME("MessageVal") )
	REF_TO(CritterDef) hCritterDef;					AST( PERSIST SUBSCRIBE NAME("CritterDef") )
	REF_TO(CritterGroup) hCritterGroup;				AST( PERSIST SUBSCRIBE NAME("CritterGroup") )
	const ContainerID uContainerID;					AST( PERSIST SUBSCRIBE NAME("ContainerID") )
} WorldVariableContainer;
extern ParseTable parse_WorldVariableContainer[];
#define TYPE_parse_WorldVariableContainer WorldVariableContainer


// This structure is used in the runtime to track the value of a variable
// NOTE: This should be consistent with "WorldVariableContainer" (but not necessarily identical)
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldVariable
{
	const char *pcName;								AST( NAME("Name") STRUCTPARAM POOL_STRING KEY)
	WorldVariableType eType;						AST( NAME("Type") )

	char *pcZoneMap;								AST( NAME("ZoneMap") )
	int iIntVal;									AST( NAME("IntVal") )
	float fFloatVal;								AST( NAME("FloatVal") )
	char *pcStringVal;								AST( NAME("StringVal") )
	DisplayMessage messageVal;						AST( NAME("MessageVal") STRUCT(parse_DisplayMessage) )
	REF_TO(CritterDef) hCritterDef;					AST( NAME("CritterDef") )
	REF_TO(CritterGroup) hCritterGroup;				AST( NAME("CritterGroup") )
	ContainerID uContainerID;						AST( NAME("ContainerID") )

	int iStrChoiceCount;							NO_AST
	U32 toMultiCheckedForPipes : 1;					NO_AST
} WorldVariable;
extern ParseTable parse_WorldVariable[];
#define TYPE_parse_WorldVariable WorldVariable

AUTO_STRUCT;
typedef struct WorldVariableArray
{
	WorldVariable **eaVariables;					AST(NAME("Variable"))
} WorldVariableArray;
extern ParseTable parse_WorldVariableArray[];
#define TYPE_parse_WorldVariableArray WorldVariableArray

// This structure is used in the runtime to track the definition for a variable
// This includes how the variable's value defaults
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct WorldVariableDef
{
	const char *pcName;								AST(STRUCTPARAM POOL_STRING KEY)

	// Type
	WorldVariableType eType;						AST(NAME("Type"))
	WorldVariableDefaultValueType eDefaultType;		AST(NAME("DefaultType"))

	// Default value is one of the following
	WorldVariable *pSpecificValue;					AST(NAME("SpecificValue"))
	
	REF_TO(ChoiceTable) choice_table;				AST(NAME("ChoiceTable"))
	char *choice_name;								AST(NAME("ChoiceName"))
	int choice_index;								AST(NAME("ChoiceIndex") DEFAULT(1))

	char *map_variable;								AST(NAME("MapVariable"))

	char *mission_refstring;						AST(NAME("MissionRefString"))
	char *mission_variable;							AST(NAME("MissionVariable"))

	Expression *pExpression;						AST(NAME("Expression") LATEBIND)

	char *activity_name;							AST(NAME("ActivityName"))
	char *activity_variable_name;					AST(NAME("ActivityVariable"))
	WorldVariable *activity_default_value;			AST(NAME("ActivityDefaultValue"))

	bool bIsPublic;									AST(NAME("IsPublic"))
} WorldVariableDef;

extern ParseTable parse_WorldVariableDef[];
#define TYPE_parse_WorldVariableDef WorldVariableDef

AUTO_STRUCT;
typedef struct GameActionWarpData
{
	char *pcMapName;
	char *pcSpawnTarget;
	WorldVariable **eaVariables;
} GameActionWarpData;

ExprContext *worldVariableGetExprContext(void);
ExprContext *worldVariableGetNoPlayerExprContext(void);

bool worldVariableEquals(const WorldVariable *var1, const WorldVariable *var2);
bool worldVariableDefEquals(const WorldVariableDef *var1, const WorldVariableDef *var2);
int worldVariableNameEq(const WorldVariable *a, const WorldVariable *b);
int worldVariableNameCmp(const WorldVariable **a, const WorldVariable **b);
int worldVariableDefNameEq(const WorldVariableDef *a, const WorldVariableDef *b);
int worldVariableDefNameCmp(const WorldVariableDef **a, const WorldVariableDef **b);
bool worldVariableValidate(WorldVariableDef *pDef, WorldVariable *pVar, const char *pcReason, const char *pcFilename);
bool worldVariableValidateEx(WorldVariableDef *pDef, WorldVariable *pVar, const char *pcReason, const char *pcFilename, bool bIgnoreMessageOwnership);
bool worldVariableValidateValue(WorldVariable *pVar, const char *pcReason, const char *pcFilename, bool bIgnoreMessageOwnership);
bool worldVariableValidateDef(WorldVariableDef *pDef, WorldVariableDef *pVarDef, const char *pcReason, const char *pcFilename);
void worldVariableDefGenerateExpressions(WorldVariableDef *pVarDef, const char *pcReason, const char *pcFilename);
void worldVariableDefGenerateExpressionsNoPlayer(WorldVariableDef *pVarDef, const char *pcReason, const char *pcFilename);
void worldVariableDefCleanExpressions(WorldVariableDef *pVarDef);
void worldVariableCleanup(WorldVariable* var);
void worldVariableDefCleanup(WorldVariableDef* varDef);

void worldVariableCountStrings(WorldVariable *var);
#define worldVariableToEString(var,value) worldVariableToEString_dbg(var,value MEM_DBG_PARMS_INIT)
void worldVariableToEString_dbg(SA_PARAM_NN_VALID const WorldVariable* var, char** value MEM_DBG_PARMS);
bool worldVariableFromString(SA_PARAM_NN_VALID WorldVariable* var, const char* value, char **err_string);
const char *worldVariableTypeToString(WorldVariableType eType);
const char* worldVariableArrayToString(WorldVariable** vars);
bool worldVariableStringToArray(const char *buf, WorldVariable*** vars);
const char* WorldVariableDefToString(WorldVariableDef* pVarDef);
void worldVariableToMultival(SA_PARAM_OP_VALID ExprContext* pContext, SA_PARAM_NN_VALID WorldVariable* var, SA_PARAM_NN_VALID MultiVal* multival);
void worldVariableFromMultival(SA_PARAM_NN_VALID const MultiVal *pMultiVal, SA_PARAM_NN_VALID WorldVariable *pVarResult, WorldVariableType eVarType);

WorldVariableType worldVariableTypeFromFSMExternVar(SA_PARAM_NN_VALID const FSMExternVar* externVar);
bool worldVariableTypeCompatibleWithFSMExternVar(WorldVariableType eType, SA_PARAM_NN_VALID const FSMExternVar *externVar);

void worldVariableCopyToContainer(NOCONST(WorldVariableContainer) *pDestContainer, const WorldVariable *pSourceVar);
void worldVariableCopyFromContainer(WorldVariable *pDestVar, WorldVariableContainer *pSourceContainer);
