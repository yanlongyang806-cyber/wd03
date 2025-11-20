#include"WorldVariable.h"

#include"Expression.h"
#include"ChoiceTable_common.h"
#include"EString.h"
#include"SimpleParser.h"
#include"StateMachine.h"
#include"StringCache.h"
#include"error.h"
#include"rand.h"
#include"ResourceInfo.h"

#include "AutoGen/WorldVariable_h_ast.h"

static ExprFuncTable* worldVariableCreateExprFuncTable(void)
{
	static ExprFuncTable* s_worldVarFuncTable = NULL;
	if(!s_worldVarFuncTable)
	{
		s_worldVarFuncTable = exprContextCreateFunctionTable("WorldVariable");
		exprContextAddFuncsToTableByTag(s_worldVarFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_worldVarFuncTable, "player");
	}
	return s_worldVarFuncTable;
}

static ExprFuncTable* worldVariableCreateNoPlayerExprFuncTable(void)
{
	static ExprFuncTable* s_worldVarFuncTable = NULL;
	if(!s_worldVarFuncTable)
	{
		s_worldVarFuncTable = exprContextCreateFunctionTable("WorldVariable_NoPlayer");
		exprContextAddFuncsToTableByTag(s_worldVarFuncTable, "util");
	}
	return s_worldVarFuncTable;
}

ExprContext *worldVariableGetExprContext(void)
{
	static ExprContext *pWorldVarExprContext = NULL;

	if (!pWorldVarExprContext)
	{
		pWorldVarExprContext = exprContextCreate();
		exprContextSetFuncTable(pWorldVarExprContext, worldVariableCreateExprFuncTable());
		exprContextSetAllowRuntimeSelfPtr(pWorldVarExprContext);
		exprContextSetAllowRuntimePartition(pWorldVarExprContext);
	}

	return pWorldVarExprContext;
}

ExprContext *worldVariableGetNoPlayerExprContext(void)
{
	static ExprContext *pWorldVarExprContext = NULL;

	if (!pWorldVarExprContext)
	{
		pWorldVarExprContext = exprContextCreate();
		exprContextSetFuncTable(pWorldVarExprContext, worldVariableCreateNoPlayerExprFuncTable());
		exprContextSetAllowRuntimePartition(pWorldVarExprContext);
	}

	return pWorldVarExprContext;
}

bool worldVariableValidateEx(WorldVariableDef *pDef, WorldVariable *pVar, const char *pcReason, const char *pcFilename, bool bIgnoreMessageOwnership)
{
	bool bResult = true;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	if (!pDef)
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set non-existent variable '%s'", pcReason, pVar->pcName);
		bResult = false;
	}
	else if (pDef->eType != pVar->eType)
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to type '%s' when the variables is defined as type '%s'", pcReason, pDef->pcName, worldVariableTypeToString(pVar->eType), worldVariableTypeToString(pDef->eType));
		bResult = false;
	}
	else if ((pDef->eType == WVAR_MESSAGE))
	{
		if(!GET_REF(pVar->messageVal.hMessage) && REF_STRING_FROM_HANDLE(pVar->messageVal.hMessage)) {
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent message '%s'", pcReason, pDef->pcName, REF_STRING_FROM_HANDLE(pVar->messageVal.hMessage));
			bResult = false;
		}
		if(!bIgnoreMessageOwnership && GET_REF(pVar->messageVal.hMessage) && !strStartsWith(GET_REF(pVar->messageVal.hMessage)->pcFilename, pcFilename)) {
			ErrorFilenamef(pcFilename, "%s is referencing a message '%s' that it does not own.  This will require textfile hacking to fix.",
						   pcReason, REF_STRING_FROM_HANDLE(pVar->messageVal.hMessage));
			bResult = false;
		}
	}
#ifdef GAMESERVER
	//animlists should only exist on the game server
	else if (pDef->eType == WVAR_ANIMATION)
	{
		if(pVar->pcStringVal)
		{
			char *ptrStart = pVar->pcStringVal;
			char cleanStr[1024];
			char *ptrEnd;

			ptrStart = pVar->pcStringVal;
			while(ptrStart != NULL)
			{
				strcpy(cleanStr, removeLeadingWhiteSpaces(ptrStart));
				ptrEnd = strchr(cleanStr, '|');
				if (ptrEnd) {
					*ptrEnd = '\0';
				}

				removeTrailingWhiteSpaces(cleanStr);

				if(!RefSystem_IsReferentStringValid("AIAnimList", cleanStr))
				{
					ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent animation '%s'", pcReason, pDef->pcName, pVar->pcStringVal);
					bResult = false;
				}
				ptrStart = strchr(ptrStart, '|');
				if(ptrStart)
				{
					ptrStart++;
					if(*ptrStart == '\0')
						ptrStart = NULL;
				}
			}
		}
	}
#endif
	else if ((pDef->eType == WVAR_CRITTER_DEF) && !REF_IS_VALID(pVar->hCritterDef))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent critter def '%s'", pcReason, pDef->pcName, REF_STRING_FROM_HANDLE(pVar->hCritterDef));
		bResult = false;
	}
#ifdef GAMESERVER
	//This can only be validated on the server. The dictionary uses resDictProvideMissingRequiresEditMode.
	else if ((pDef->eType == WVAR_CRITTER_GROUP) && !REF_IS_VALID(pVar->hCritterGroup))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent critter group '%s'", pcReason, pDef->pcName, REF_STRING_FROM_HANDLE(pVar->hCritterGroup));
		bResult = false;
	}
#endif
	else if (pDef->eType == WVAR_MAP_POINT)
	{ 
		// TomY TODO this doesn't validate UGC maps correctly
		if (pVar->pcZoneMap && !resHasNamespace(pVar->pcZoneMap) &&
			!RefSystem_IsReferentStringValid("ZoneMap", pVar->pcZoneMap))
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent zone map '%s'", pcReason, pDef->pcName, pVar->pcZoneMap);
	}
	else if (pDef->eType == WVAR_ITEM_DEF && !RefSystem_IsReferentStringValid("ItemDef", pVar->pcStringVal))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent ItemDef '%s'", pcReason, pDef->pcName, pVar->pcStringVal);
		bResult = false;
	}
	else if (pDef->eType == WVAR_MISSION_DEF && !RefSystem_IsReferentStringValid("Mission", pVar->pcStringVal))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent MissionDef '%s'", pcReason, pDef->pcName, pVar->pcStringVal);
		bResult = false;
	}

	return bResult;
}

bool worldVariableValidate(WorldVariableDef *pDef, WorldVariable *pVar, const char *pcReason, const char *pcFilename)
{
	return worldVariableValidateEx(pDef, pVar, pcReason, pcFilename, false);
}


// Used to validate a variable which has no def
bool worldVariableValidateValue(WorldVariable *pVar, const char *pcReason, const char *pcFilename, bool bIgnoreMessageOwnership)
{
	bool bResult = true;

	if ((pVar->eType == WVAR_MESSAGE))
	{
		if(!GET_REF(pVar->messageVal.hMessage) && REF_STRING_FROM_HANDLE(pVar->messageVal.hMessage)) {
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent message '%s'", pcReason, pVar->pcName, REF_STRING_FROM_HANDLE(pVar->messageVal.hMessage));
			bResult = false;
		}
		if(!bIgnoreMessageOwnership && GET_REF(pVar->messageVal.hMessage) && !strStartsWith(GET_REF(pVar->messageVal.hMessage)->pcFilename, pcFilename)) {
			ErrorFilenamef(pcFilename, "%s is referencing a message '%s' that it does not own.  This will require textfile hacking to fix.",
						   pcReason, REF_STRING_FROM_HANDLE(pVar->messageVal.hMessage));
			bResult = false;
		}
	}
#ifdef GAMESERVER
	//animlists should only exist on the game server
	else if (pVar->eType == WVAR_ANIMATION) 
	{
		if(pVar->pcStringVal)
		{
			char *ptrStart = pVar->pcStringVal;
			char cleanStr[1024];
			char *ptrEnd;

			ptrStart = pVar->pcStringVal;
			while(ptrStart != NULL)
			{
				strcpy(cleanStr, removeLeadingWhiteSpaces(ptrStart));
				ptrEnd = strchr(cleanStr, '|');
				if (ptrEnd) {
					*ptrEnd = '\0';
				}

				removeTrailingWhiteSpaces(cleanStr);

				if(!RefSystem_IsReferentStringValid("AIAnimList", cleanStr))
				{
					ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent animation '%s'", pcReason, pVar->pcName, pVar->pcStringVal);
					bResult = false;
				}
				ptrStart = strchr(ptrStart, '|');
				if(ptrStart)
				{
					ptrStart++;
					if(*ptrStart == '\0')
						ptrStart = NULL;
				}
			}
		}
	}
#endif
	else if ((pVar->eType == WVAR_CRITTER_DEF) && !REF_IS_VALID(pVar->hCritterDef))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent critter def '%s'", pcReason, pVar->pcName, REF_STRING_FROM_HANDLE(pVar->hCritterDef));
		bResult = false;
	}
#ifdef GAMESERVER
	//This can only be validated on the server. The dictionary uses resDictProvideMissingRequiresEditMode.
	else if ((pVar->eType == WVAR_CRITTER_GROUP) && !REF_IS_VALID(pVar->hCritterGroup))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent critter group '%s'", pcReason, pVar->pcName, REF_STRING_FROM_HANDLE(pVar->hCritterGroup));
		bResult = false;
	}
#endif
	else if (pVar->eType == WVAR_MAP_POINT)
	{ 
		// TomY TODO this doesn't validate UGC maps correctly
		if (pVar->pcZoneMap && !resHasNamespace(pVar->pcZoneMap) &&
			!RefSystem_IsReferentStringValid("ZoneMap", pVar->pcZoneMap))
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent zone map '%s'", pcReason, pVar->pcName, pVar->pcZoneMap);
	}
	else if (pVar->eType == WVAR_ITEM_DEF && !RefSystem_IsReferentStringValid("ItemDef", pVar->pcStringVal))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent ItemDef '%s'", pcReason, pVar->pcName, pVar->pcStringVal);
		bResult = false;
	}
	else if (pVar->eType == WVAR_MISSION_DEF && !RefSystem_IsReferentStringValid("Mission", pVar->pcStringVal))
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to a non-existent MissionDef '%s'", pcReason, pVar->pcName, pVar->pcStringVal);
		bResult = false;
	}

	return bResult;
}

// Cleans expressions related to this world variable def
void worldVariableDefCleanExpressions(WorldVariableDef *pVarDef)
{
	if (pVarDef && pVarDef->pExpression)
	{
		exprClean(pVarDef->pExpression);
	}
}


void worldVariableDefGenerateExpressions(WorldVariableDef *pVarDef, const char *pcReason, const char *pcFilename)
{
	if (pVarDef && pVarDef->eDefaultType == WVARDEF_EXPRESSION && pVarDef->pExpression)
	{
		if (!exprGenerate(pVarDef->pExpression, worldVariableGetExprContext()))
		{
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' based on a bad expression.", pcReason, pVarDef->pcName);
		}
	}
}

void worldVariableDefGenerateExpressionsNoPlayer(WorldVariableDef *pVarDef, const char *pcReason, const char *pcFilename)
{
	if (pVarDef && pVarDef->eDefaultType == WVARDEF_EXPRESSION && pVarDef->pExpression)
	{
		if (!exprGenerate(pVarDef->pExpression, worldVariableGetNoPlayerExprContext()))
		{
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' based on a bad expression.", pcReason, pVarDef->pcName);
		}
	}
}

bool worldVariableValidateDef(WorldVariableDef *pDef, WorldVariableDef *pVarDef, const char *pcReason, const char *pcFilename)
{
	bool bResult = true;

	if (!pDef)
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set non-existent variable '%s'", pcReason, pVarDef->pcName);
		bResult = false;
	}
	else if (pDef->eType != pVarDef->eType)
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to type '%s' when the type should be '%s'", pcReason, pVarDef->pcName, worldVariableTypeToString(pDef->eType), worldVariableTypeToString(pVarDef->eType));
		bResult = false;
	}
	else if (pVarDef->eType == WVAR_NONE)
	{
		ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' to type NONE and this type isn't allowed", pcReason, pVarDef->pcName);
		bResult = false;
	}
	else if (pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT)
	{
		if (pVarDef->pSpecificValue)
		{
			bResult = worldVariableValidate(pDef, pVarDef->pSpecificValue, pcReason, pcFilename);

			// If a name is present on the specified value, the name must match.  But it's okay for the name to be NULL.
			if (pVarDef->pSpecificValue->pcName && (stricmp(pVarDef->pcName, pVarDef->pSpecificValue->pcName) != 0)) {
				ErrorFilenamef(pcFilename, "%s variable '%s' has a specified value with the name '%s' when the name needs to match", pcReason, pVarDef->pcName, pVarDef->pSpecificValue->pcName);
				bResult = false;
			}
		}
		else
		{
			ErrorFilenamef(pcFilename, "%s variable '%s' is missing its default value", pcReason, pVarDef->pcName);
			bResult = false;
		}
	}
	else if (pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE)
	{
		ChoiceTable* pVarTable = GET_REF(pVarDef->choice_table);
		const char* pcChoiceName = pVarDef->choice_name; 
		if (!pVarTable)
		{
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' based on non-existent Choice Table '%s'", pcReason, pVarDef->pcName, REF_STRING_FROM_HANDLE(pVarDef->choice_table));
			bResult = false;
		}
		else if (choice_ValueType( pVarTable, pcChoiceName ) != pVarDef->eType)
		{
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s', which is of type '%s', but its Choice Table is of a different type", pcReason, pVarDef->pcName, StaticDefineIntRevLookup(WorldVariableTypeEnum, pVarDef->eType));
			bResult = false;
		}
		else if (pVarTable->eSelectType == CST_TimedRandom && (pVarDef->choice_index > choice_TimedRandomValuesPerInterval(pVarTable) || pVarDef->choice_index <= 0))
		{
			if (pVarDef->choice_index > choice_TimedRandomValuesPerInterval(pVarTable))
			{
				ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s' based on timed index '%i' that is out of bounds", pcReason, pVarDef->pcName, pVarDef->choice_index);
				bResult = false;
			}
		}
	}

	if (pVarDef->eType == WVAR_PLAYER )
	{
		if (pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE)
		{
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s', but variables of type '%s' are not allowed to use a Choice Table.", pcReason, pVarDef->pcName, StaticDefineIntRevLookup(WorldVariableTypeEnum, pVarDef->eType));
			bResult = false;
		}
		else if (pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT && pVarDef->pSpecificValue && pVarDef->pSpecificValue->uContainerID != 0)
		{
			ErrorFilenamef(pcFilename, "%s is attempting to set variable '%s', but variables of type '%s' are not allowed to have a specific value set.", pcReason, pVarDef->pcName, StaticDefineIntRevLookup(WorldVariableTypeEnum, pVarDef->eType));
			bResult = false;
		}
	}

	return bResult;
}

/// Remove any extra data lying around in VAR. 
void worldVariableCleanup(WorldVariable* var)
{
	// VARIABLE_TYPES: Add code below if add to the available variable types
	if( var->eType != WVAR_INT ) {
		var->iIntVal = 0;
	}
	if( var->eType != WVAR_FLOAT ) {
		var->fFloatVal = 0;
	}
	if(   var->eType != WVAR_STRING && var->eType != WVAR_LOCATION_STRING
		  && var->eType != WVAR_ANIMATION && var->eType != WVAR_MAP_POINT
		  && var->eType != WVAR_ITEM_DEF && var->eType != WVAR_MISSION_DEF) {
		StructFreeStringSafe(&var->pcStringVal);
	}
	if( var->eType != WVAR_MESSAGE ) {
		StructReset(parse_DisplayMessage, &var->messageVal);
	}
	if( var->eType != WVAR_CRITTER_DEF ) {
		REMOVE_HANDLE( var->hCritterDef );
	}
	if( var->eType != WVAR_CRITTER_GROUP ) {
		REMOVE_HANDLE( var->hCritterGroup );
	}
	if( var->eType != WVAR_MAP_POINT ) {
		StructFreeStringSafe(&var->pcZoneMap);
	}
}

void worldVariableDefCleanup(WorldVariableDef* varDef)
{
	if( varDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT ) {
		if( !varDef->pSpecificValue ) {
			varDef->pSpecificValue = StructCreate( parse_WorldVariable );
		}
		varDef->pSpecificValue->eType = varDef->eType;
		worldVariableCleanup( varDef->pSpecificValue );
	} else {
		StructDestroySafe( parse_WorldVariable, &varDef->pSpecificValue );
	}
	if( varDef->eDefaultType != WVARDEF_CHOICE_TABLE ) {
		REMOVE_HANDLE( varDef->choice_table );
		StructFreeStringSafe( &varDef->choice_name );
	}
	if( varDef->eDefaultType != WVARDEF_MAP_VARIABLE ) {
		StructFreeStringSafe( &varDef->map_variable );
	}
	if (varDef->eDefaultType != WVARDEF_MISSION_VARIABLE ) {
		StructFreeStringSafe( &varDef->mission_variable );
	}
	if (varDef->eDefaultType != WVARDEF_EXPRESSION ) {
		exprDestroy( varDef->pExpression );
		varDef->pExpression = NULL;
	}
}

void worldVariableToEString_dbg(SA_PARAM_NN_VALID const WorldVariable* var, char** value MEM_DBG_PARMS)
{
	if (!var)
		return;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch (var->eType)
	{
	case WVAR_INT:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%d", var->iIntVal );
		return;

	case WVAR_FLOAT:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%g", var->fFloatVal );
		return;

	case WVAR_STRING:
	case WVAR_LOCATION_STRING:
	case WVAR_ANIMATION:
	case WVAR_MISSION_DEF:
	case WVAR_ITEM_DEF:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%s", var->pcStringVal);
		return;

	case WVAR_CRITTER_DEF:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%s", REF_STRING_FROM_HANDLE(var->hCritterDef));
		return;

	case WVAR_CRITTER_GROUP:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%s", REF_STRING_FROM_HANDLE(var->hCritterGroup));
		return;

	case WVAR_MESSAGE:
		if (GET_REF(var->messageVal.hMessage)) {
			estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%s", GET_REF(var->messageVal.hMessage)->pcMessageKey);
		}
		return;

	case WVAR_MAP_POINT:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "mappoint:%s,%s",
					   var->pcZoneMap,
					   var->pcStringVal ? var->pcStringVal : "");
		return;

	case WVAR_PLAYER:
		estrPrintf_dbg(value MEM_DBG_PARMS_CALL, "%u", var->uContainerID );
		return;
	}
}

bool worldVariableFromString(SA_PARAM_NN_VALID WorldVariable* var, const char* value, char **err_string)
{
	if (!var)
		return false;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch (var->eType)
	{
	case WVAR_INT:
		var->iIntVal = atoi(value);
		return true;

	case WVAR_FLOAT:
		var->fFloatVal = atof(value);
		return true;

	case WVAR_STRING:
	case WVAR_LOCATION_STRING:
	case WVAR_ANIMATION:
	case WVAR_MISSION_DEF:
    case WVAR_ITEM_DEF:
		var->pcStringVal = StructAllocString(value);
		return true;

	case WVAR_CRITTER_DEF:
		SET_HANDLE_FROM_STRING("CritterDef", value, var->hCritterDef);
		return true;

	case WVAR_CRITTER_GROUP:
		SET_HANDLE_FROM_STRING("CritterGroup", value, var->hCritterGroup);
		return true;

	case WVAR_MESSAGE:
		if (RefSystem_ReferentFromString("Message", value)) {
			SET_HANDLE_FROM_STRING("Message", value, var->messageVal.hMessage);
			return true;
		} else if (err_string) {
			estrPrintf(err_string, "Unable to find message key '%s'", value);
		}
		return false;

	case WVAR_MAP_POINT:
		{
			char mapName[ 256 ] = { 0 };
			char* spawnPointName;
			char* delim = strchr( value, ',' );
			if( delim ) {
				memcpy( mapName, value, MIN( delim - value, 255 ));

				spawnPointName = delim + 1;
			} else {
				strcpy( mapName, value );
				spawnPointName = "";
			}

			StructFreeStringSafe( &var->pcZoneMap );
			var->pcZoneMap = StructAllocString( mapName );
			var->pcStringVal = StructAllocString( spawnPointName );
			return true;
		}

	case WVAR_PLAYER:
		{
			var->uContainerID = atoi(value);
			return true;
		}
	}
	return false;
}

const char *worldVariableTypeToString(WorldVariableType eType)
{
	const char* result = StaticDefineIntRevLookup(WorldVariableTypeEnum, eType);
	if (!result )
		result = "UNKNOWN";

	return result;
}

void worldVariableCountStrings(WorldVariable *var)
{
	char *ptrStart = NULL;
	if(!var->toMultiCheckedForPipes)
	{
		var->iStrChoiceCount = 1;
		var->toMultiCheckedForPipes = 1;
		ptrStart = var->pcStringVal;
		if( ptrStart ) {
			while(ptrStart = strchr(ptrStart, '|')) {
				++var->iStrChoiceCount;
				++ptrStart;
			}
		}
	}
}

/// Convert a WorldVariable to a MultiVal.
///
/// WARNING: If pContext is non-null, then this MUST allocate any memory using
/// pContext's scratch stack.
void worldVariableToMultival(ExprContext* pContext, WorldVariable* var, MultiVal* multival)
{
	const char *pRefString;
	MultiValClear(multival);

	if (!var)
		return;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch (var->eType)
	{
		case WVAR_INT:
			MultiValSetInt(multival, var->iIntVal);
			return;

		case WVAR_FLOAT:
			MultiValSetFloat(multival, var->fFloatVal);
			return;

		case WVAR_ANIMATION:
		case WVAR_STRING:
			{
				char *ptrStart = NULL;
				// Strings have a special rule that when putting into a MultiVal,
				// if the string has vertical bars, such as "a|b|c", then you randomly
				// pick one of the bar-separated values instead of setting the whole string.

				// Count vertical bars to find out how many choices there are
				// ALready done for shared memory
				worldVariableCountStrings(var);

				if (var->iStrChoiceCount <= 1) {
					// The whole string is one value
					MultiValSetStringForExpr(pContext, multival, var->pcStringVal ? var->pcStringVal : "");
				} else {
					// There are multiple values and need to pick one at random
					char cleanStr[1024];
					char *ptrEnd;
					int iRandom = 0;

					ptrStart = var->pcStringVal;
					for(iRandom = randInt(var->iStrChoiceCount); iRandom > 0; --iRandom) {
						ptrStart = strchr(ptrStart, '|') + 1;
					}
					strcpy(cleanStr, removeLeadingWhiteSpaces(ptrStart));
					ptrEnd = strchr(cleanStr, '|');
					if (ptrEnd) {
						*ptrEnd = '\0';
					}
					removeTrailingWhiteSpaces(cleanStr);

					MultiValSetStringForExpr(pContext, multival, cleanStr);
				}
				return;
			}

		case WVAR_LOCATION_STRING:
			multival->type = MULTIOP_LOC_STRING;
			multival->str = allocAddString( var->pcStringVal);
			return;

		case WVAR_CRITTER_DEF:
			pRefString = REF_STRING_FROM_HANDLE(var->hCritterDef);
			MultiValSetStringForExpr(pContext, multival, pRefString ? pRefString : "");
			return;

		case WVAR_CRITTER_GROUP:
			pRefString = REF_STRING_FROM_HANDLE(var->hCritterGroup);
			MultiValSetStringForExpr(pContext, multival, pRefString ? pRefString : "");
			return;

		case WVAR_MESSAGE:
			pRefString = REF_STRING_FROM_HANDLE(var->messageVal.hMessage);
			MultiValSetStringForExpr(pContext, multival, pRefString ? pRefString : "");
			return;

		case WVAR_MAP_POINT: {
			char buffer[1024];
			sprintf( buffer, "mappoint:%s,%s", var->pcZoneMap, var->pcStringVal ? var->pcStringVal : "" );
			MultiValSetStringForExpr(pContext, multival, buffer);
			return;
							 }

		case WVAR_ITEM_DEF:
			MultiValSetStringForExpr(pContext, multival, var->pcStringVal);
			return;

		case WVAR_MISSION_DEF:
			MultiValSetStringForExpr(pContext, multival, var->pcStringVal);
			return;
	}
}

void worldVariableFromMultival(const MultiVal *pMultiVal, WorldVariable *pVarResult, WorldVariableType eVarType)
{
	if (!pMultiVal || !pVarResult)
		return;

	// if no type is specified as a parameter, try to get the type from the variable itself
	if (eVarType == WVAR_NONE)
		eVarType = pVarResult->eType;
	pVarResult->eType = eVarType;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch (eVarType)
	{
		xcase WVAR_INT:
			pVarResult->iIntVal = MultiValGetInt(pMultiVal, NULL);

		xcase WVAR_FLOAT:
			pVarResult->fFloatVal = MultiValGetFloat(pMultiVal, NULL);

		xcase WVAR_STRING:
		case WVAR_LOCATION_STRING:
		case WVAR_ANIMATION:
		case WVAR_MAP_POINT:
		case WVAR_ITEM_DEF:
		case WVAR_MISSION_DEF:
			pVarResult->pcStringVal = StructAllocString(MultiValGetString(pMultiVal, NULL));

		xcase WVAR_CRITTER_DEF:
			SET_HANDLE_FROM_STRING("CritterDef", MultiValGetString(pMultiVal, NULL), pVarResult->hCritterDef);

		xcase WVAR_CRITTER_GROUP:
			SET_HANDLE_FROM_STRING("CritterGroup", MultiValGetString(pMultiVal, NULL), pVarResult->hCritterGroup);

		xcase WVAR_MESSAGE:
			SET_HANDLE_FROM_STRING("Message", MultiValGetString(pMultiVal, NULL), pVarResult->messageVal.hMessage);
	}

	worldVariableCleanup(pVarResult);
}


bool worldVariableEquals(const WorldVariable *var1, const WorldVariable *var2)
{
	if (!var1 && !var2)
		return true;
	if (!var1 || !var2) 
		return false;
	if (var1->eType != var2->eType)
		return false;

	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch(var1->eType)
	{
	xcase WVAR_INT:
		if (var1->iIntVal != var2->iIntVal) 
			return false;

	xcase WVAR_FLOAT:
		if (var1->fFloatVal != var2->fFloatVal) 
			return false;

	xcase WVAR_STRING:
	case WVAR_LOCATION_STRING:
	case WVAR_ANIMATION:
	case WVAR_MISSION_DEF:
	case WVAR_ITEM_DEF:
		if ((!var1->pcStringVal && var2->pcStringVal) || (var1->pcStringVal && !var2->pcStringVal))
			return false;
		if (var1->pcStringVal && var2->pcStringVal && stricmp(var1->pcStringVal, var2->pcStringVal) != 0) 
			return false;

	xcase WVAR_CRITTER_DEF:
		if (GET_REF(var1->hCritterDef) != GET_REF(var2->hCritterDef))
			return false;

	xcase WVAR_CRITTER_GROUP:
		if (GET_REF(var1->hCritterGroup) != GET_REF(var2->hCritterGroup))
			return false;

	xcase WVAR_MESSAGE:
		if (GET_REF(var1->messageVal.hMessage) != GET_REF(var2->messageVal.hMessage))
			return false;
	xcase WVAR_PLAYER:
		if (var1->uContainerID != var2->uContainerID)
			return false;
	}

	return true;
}

bool worldVariableDefEquals(const WorldVariableDef *var1, const WorldVariableDef *var2)
{
	if (!var1 && !var2)
		return true;
	if (!var1 || !var2) 
		return false;
	if (var1->eType != var2->eType)
		return false;
	if (var1->eDefaultType != var2->eDefaultType)
		return false;

	switch(var1->eDefaultType)
	{
		xcase WVARDEF_SPECIFY_DEFAULT:
			return worldVariableEquals(var1->pSpecificValue, var2->pSpecificValue);

		xcase WVARDEF_CHOICE_TABLE:
			if( GET_REF(var1->choice_table) != GET_REF(var2->choice_table)) {
				return false;
			}
			if(   !var1->choice_name || !var2->choice_name
				  || stricmp( var1->choice_name, var2->choice_name ) != 0 ) {
				return false;
			}

		xcase WVARDEF_MAP_VARIABLE:
			if(   !var1->map_variable || !var2->map_variable
				  || stricmp(var1->map_variable, var2->map_variable) != 0 ) {
				return false;
			}

		xcase WVARDEF_MISSION_VARIABLE:
			if(   !var1->mission_variable || !var2->mission_variable
				|| stricmp(var1->mission_variable, var2->mission_variable) != 0 ) {
				return false;
			}

		xcase WVARDEF_EXPRESSION:
			return exprCompare(var1->pExpression, var2->pExpression);
	}
	return true;
}

// Compares two world variable defs by name
int worldVariableDefNameEq(const WorldVariableDef *a, const WorldVariableDef *b)
{
	return(worldVariableDefNameCmp(&a,&b) == 0);
}

// Compares two world variables by name
int worldVariableNameEq(const WorldVariable *a, const WorldVariable *b)
{
	return(worldVariableNameCmp(&a,&b) == 0);
}

// Compares two world variable defs by name
int worldVariableDefNameCmp(const WorldVariableDef **a, const WorldVariableDef **b)
{
	if(!a || !(*a) || !b || !(*b) || !(*a)->pcName || !(*b)->pcName)
		return 0;
	return strcmp((*a)->pcName, (*b)->pcName);
}

// Compares two world variables by name
int worldVariableNameCmp(const WorldVariable **a, const WorldVariable **b)
{
	if(!a || !(*a) || !b || !(*b) || !(*a)->pcName || !(*b)->pcName)
		return 0;
	return strcmp((*a)->pcName, (*b)->pcName);
}

const char* worldVariableArrayToString(WorldVariable** vars)
{
	if (eaSize(&vars)) 
	{
		WorldVariableArray var_array;
		char *estr = NULL;
		const char* ret;

		var_array.eaVariables = vars;
		ParserWriteText(&estr, parse_WorldVariableArray, &var_array, 0, 0, 0);

		ret = allocAddString(estr);
		estrDestroy(&estr);

		return ret;
	}
	else
		return allocAddString("");
}

bool worldVariableStringToArray(const char *buf, WorldVariable*** vars)
{
	WorldVariableArray var_array = {0};

	if (!ParserReadText(buf, parse_WorldVariableArray, &var_array, 0))
		return false;

	*vars = var_array.eaVariables;

	return true;
}

WorldVariableType worldVariableTypeFromFSMExternVar(SA_PARAM_NN_VALID const FSMExternVar* externVar)
{
	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch( externVar->type ) {
		case MULTI_FLOAT:			return WVAR_FLOAT;
		case MULTI_INT:				return WVAR_INT;
		case MULTIOP_LOC_STRING:	return WVAR_LOCATION_STRING;
		default: {
			const char* scType = externVar->scType;
			if( stricmp( scType, "message" ) == 0 ) {
				return WVAR_MESSAGE;
			} else if( stricmp( scType, "AIAnimList" ) == 0 ) {
				return WVAR_ANIMATION;
			} else {
				return WVAR_STRING;
			}
		}
	}
}

bool worldVariableTypeCompatibleWithFSMExternVar(WorldVariableType eType, SA_PARAM_NN_VALID const FSMExternVar *externVar)
{
	// VARIABLE_TYPES: Add code below if add to the available variable types
	switch( externVar->type ) {
		case MULTI_FLOAT:			return (eType == WVAR_FLOAT);
		case MULTI_INT:				return (eType == WVAR_INT);
		case MULTIOP_LOC_STRING:	return (eType == WVAR_LOCATION_STRING);
		default: {
			const char* scType = externVar->scType;
			if( stricmp( scType, "message" ) == 0 ) {
				return (eType == WVAR_MESSAGE);
			} else if( stricmp( scType, "AIAnimList" ) == 0 ) {
				return (eType == WVAR_ANIMATION) || (eType == WVAR_STRING);
			} else {
				return (eType == WVAR_STRING) || 
					(eType == WVAR_CRITTER_DEF) || // No known scType for this
					(eType == WVAR_CRITTER_GROUP); // No known scType for this
			}
		}
	}
}

const char* WorldVariableDefToString(WorldVariableDef* pVarDef)
{
	char* estrText = NULL;

	if(!pVarDef)
		return "";

	estrStackCreate(&estrText);

	estrPrintf(&estrText, "%s (%s): ", pVarDef->pcName, worldVariableTypeToString(pVarDef->eType));
	if (pVarDef->eDefaultType == WVARDEF_SPECIFY_DEFAULT) {
		char *estrText2 = NULL;
		worldVariableToEString(pVarDef->pSpecificValue, &estrText2);
		estrPrintf(&estrText, "%s [Specified Value] (%s) = %s", NULL_TO_EMPTY(pVarDef->pcName), worldVariableTypeToString(pVarDef->eType), estrText2);
		estrDestroy(&estrText2);
	} else if (pVarDef->eDefaultType == WVARDEF_CHOICE_TABLE) {
		estrPrintf(&estrText, "%s [Choice Table: '%s' | Field: '%s'] (%s)", NULL_TO_EMPTY(pVarDef->pcName), REF_STRING_FROM_HANDLE(pVarDef->choice_table), pVarDef->choice_name, worldVariableTypeToString(pVarDef->eType));
	} else if (pVarDef->eDefaultType == WVARDEF_MAP_VARIABLE) {
		estrPrintf(&estrText, "%s [Map Variable: '%s'] (%s)", NULL_TO_EMPTY(pVarDef->pcName), pVarDef->map_variable, worldVariableTypeToString(pVarDef->eType));
	} else if (pVarDef->eDefaultType == WVARDEF_MISSION_VARIABLE) {
		estrPrintf(&estrText, "%s [Mission Variable: '%s'] (%s)", NULL_TO_EMPTY(pVarDef->pcName), pVarDef->mission_variable, worldVariableTypeToString(pVarDef->eType));
	} else if (pVarDef->eDefaultType == WVARDEF_EXPRESSION) {
		estrPrintf(&estrText, "%s [Expression: '%s'] (%s)", NULL_TO_EMPTY(pVarDef->pcName), exprGetCompleteString(pVarDef->pExpression), worldVariableTypeToString(pVarDef->eType));
	}

	{
		const char* pRet = strdup(estrText);
		estrDestroy(&estrText);
		return pRet;
	}

	return "";
}

void worldVariableCopyToContainer(NOCONST(WorldVariableContainer) *pDestContainer, const WorldVariable *pSourceVar)
{
	pDestContainer->pcName = pSourceVar->pcName;
	pDestContainer->eType = pSourceVar->eType;

	StructFreeString(pDestContainer->pcZoneMap);
	pDestContainer->pcZoneMap = StructAllocString(pSourceVar->pcZoneMap);
	
	pDestContainer->iIntVal = pSourceVar->iIntVal;
	pDestContainer->fFloatVal = pSourceVar->fFloatVal;

	StructFreeString(pDestContainer->pcStringVal);
	pDestContainer->pcStringVal = StructAllocString(pSourceVar->pcStringVal);
	
	COPY_HANDLE(pDestContainer->hMessage, pSourceVar->messageVal.hMessage);
	COPY_HANDLE(pDestContainer->hCritterDef, pSourceVar->hCritterDef);
	COPY_HANDLE(pDestContainer->hCritterGroup, pSourceVar->hCritterGroup);
}

void worldVariableCopyFromContainer(WorldVariable *pDestVar, WorldVariableContainer *pSourceContainer)
{
	pDestVar->pcName = pSourceContainer->pcName;
	pDestVar->eType = pSourceContainer->eType;

	StructFreeString(pDestVar->pcZoneMap);
	pDestVar->pcZoneMap = StructAllocString(pSourceContainer->pcZoneMap);
	
	pDestVar->iIntVal = pSourceContainer->iIntVal;
	pDestVar->fFloatVal = pSourceContainer->fFloatVal;

	StructFreeString(pDestVar->pcStringVal);
	pDestVar->pcStringVal = StructAllocString(pSourceContainer->pcStringVal);
	
	COPY_HANDLE(pDestVar->messageVal.hMessage, pSourceContainer->hMessage);
	COPY_HANDLE(pDestVar->hCritterDef, pSourceContainer->hCritterDef);
	COPY_HANDLE(pDestVar->hCritterGroup, pSourceContainer->hCritterGroup);
}

void worldVariableContainerSetName(NOCONST(WorldVariableContainer) *pDestContainer, const char *pcName)
{
	pDestContainer->pcName = allocAddString(pcName);
}

#include"AutoGen/WorldVariable_h_ast.c"