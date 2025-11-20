#include "ChoiceTable_common.h"

#include "ResourceManager.h"
#include "error.h"
#include "File.h"
#include "StringCache.h"
#include "WorldVariable.h"

#define CHOICE_TABLE_BASE_DIR "defs/choice"
#define CHOICE_TABLE_EXTENSION "choice"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static int choice_ResValidateCB( enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ChoiceTable *table, U32 userID );

WorldVariable choice_defaultValue = { 0 };

DictionaryHandle g_hChoiceTableDict;

/// Validate TABLE.
bool choice_Validate( ChoiceTable* table )
{
	bool retcode = true;
	#define ChoiceErrorf(fmt, ...)									\
		ErrorFilenamef( table->pchFileName, fmt, ##__VA_ARGS__ );	\
		retcode = false
	
	if( !resIsValidName( table->pchName )) {
		ChoiceErrorf( "Choice Table name \"%s\" is illegal.", table->pchName );
	}

	if( !resIsValidScope( table->pchScope )) {
		ChoiceErrorf( "Choice Table scope \"%s\" is illegal.", table->pchScope );
	}

	{
		const char* pchTempFileName = table->pchFileName;
		if( resFixPooledFilename( &pchTempFileName, CHOICE_TABLE_BASE_DIR, table->pchScope, table->pchName, CHOICE_TABLE_EXTENSION )) {
			if( IsServer()) {
				ChoiceErrorf( "Choice Table filename does not match name \"%s\" scope \"%s\".", table->pchName, table->pchScope );
			}
		}
	}

	// timed random validation
	if (table->eSelectType == CST_TimedRandom)
	{
		if (table->eTimeInterval <= 0) {
			ChoiceErrorf( "Choice Table \"%s\" must have a positive timed random interval.", table->pchName );
		}
		if (table->iValuesPerInterval > choice_TotalEntries(table)) {
			ChoiceErrorf( "Choice Table \"%s\" cannot choose more values per timed interval than the number of entries.", table->pchName );
		}
		else if (table->iValuesPerInterval < 0) {
			ChoiceErrorf( "Choice Table \"%s\" cannot choose a negative number of values per timed interval.", table->pchName );
		}
		if (table->iNumUniqueIntervals < 0) {
			ChoiceErrorf( "Choice Table \"%s\" cannot have a negative number of unique intervals.", table->pchName );
		}

		// test values against bounds of random table generation
		if (choice_TimedRandomRange(table) > MAX_DAILY_TABLE_RANDOM_RANGE) {
			ChoiceErrorf("Choice Table \"%s\" has too many timed random values to select from.", table->pchName);
		}
		if (choice_TimedRandomValuesPerInterval(table) > MAX_DAILY_TABLE_VALUES_PER_INTERVAL) {
			ChoiceErrorf("Choice Table \"%s\" is choosing too many values per interval.", table->pchName);
		}
		if (choice_TimedRandomNumIntervals(table) > MAX_DAILY_TABLE_INTERVALS) {
			ChoiceErrorf("Choice Table \"%s\" has too many unique intervals.", table->pchName);
		}
	}

	{
		int it;
		int valueIt;
		for( it = 0; it != eaSize( &table->eaEntry ); ++it ) {
			ChoiceEntry* entry = table->eaEntry[ it ];

			if ( IsServer() && entry->eType == CET_Include) {
				ChoiceTable *includeTable = GET_REF(entry->hChoiceTable);
				if (!includeTable) {
					ChoiceErrorf("Entry: #%d -- Entry includes non-existent choice table \"%s\".", it + 1, REF_STRING_FROM_HANDLE(entry->hChoiceTable));
				}
				else if (includeTable->eSelectType != table->eSelectType) {
					ChoiceErrorf("Entry: #%d -- Entry includes choice table \"%s\" that is a different select type.", it + 1, REF_STRING_FROM_HANDLE(entry->hChoiceTable));
				}
			}
			if( IsServer() && entry->fWeight < 0 ) {
				ChoiceErrorf( "Entry: #%d -- Entry has negative weight \"%f\".", it + 1, entry->fWeight );
			}
			if( IsServer() && entry->eaValues && eaSize( &entry->eaValues ) != eaSize( &table->eaDefs )) {
				ChoiceErrorf( "Entry: #%d -- Entry does not have the %d values.", it + 1, eaSize( &entry->eaValues ));
			} else {
				for( valueIt = 0; valueIt != eaSize( &entry->eaValues ); ++valueIt ) {
					ChoiceTableValueDef* def = table->eaDefs[ valueIt ];
					ChoiceValue* value = entry->eaValues[ valueIt ];
					WorldVariableDef varDef = {0};
					char buf[1024];

					switch( value->eType ) {
						case CVT_Value:
							if( IsGameServerSpecificallly_NotRelatedTypes() && (value->value.eType != WVAR_NONE)) {
								varDef.pcName = def->pchName;
								varDef.eType = def->eType;
								sprintf(buf, "Choice table entry: #%d, Name: %s", it+1, def->pchName);
								retcode &= worldVariableValidate(&varDef, &value->value, buf, table->pchFileName);
							}
						xcase CVT_Choice:
							if( IsServer() && !IS_HANDLE_ACTIVE( value->hChoiceTable )) {
								ChoiceErrorf( "Entry: #%d, Name: %s -- Name does not reference a choice table.",
											  it + 1, def->pchName );
							} else if( IsServer() && !GET_REF( value->hChoiceTable )) {
								ChoiceErrorf( "Entry: #%d, Name: %s -- Name references non-existant choice table '%s'.",
											  it + 1, def->pchName, REF_STRING_FROM_HANDLE( value->hChoiceTable ));
							} else {
								ChoiceTable* valueTable = GET_REF( value->hChoiceTable );
								if( IsServer() && !value->pchChoiceName ) {
									ChoiceErrorf( "Entry: #%d, Name: %s -- Name does not reference a choice name.",
												  it + 1, def->pchName );
								} else if( IsServer() && valueTable && choice_ValueType( valueTable, value->pchChoiceName ) != def->eType ) {
									ChoiceErrorf( "Entry: #%d, Name: %s -- Name is of type %s, but references choice table entry of type %s.",
												  it + 1, def->pchName,
												  StaticDefineIntRevLookup( WorldVariableTypeEnum, def->eType ),
												  StaticDefineIntRevLookup( WorldVariableTypeEnum, choice_ValueType( valueTable, value->pchChoiceName )));
								}
							}
					}
				}
			}
		}
	}

	return retcode;
}

/// Find out what index the value NAME is in TABLE.
int choice_ValueIndex( SA_PARAM_NN_VALID ChoiceTable* table, const char* name )
{
	int it;

	name = allocAddString( name );
	for( it = 0; it != eaSize( &table->eaDefs ); ++it ) {
		if( name == table->eaDefs[ it ]->pchName ) {
			return it;
		}
	}

	return -1;
}

/// Return the type of value returned by TABLE's entry with name NAME.
WorldVariableType choice_ValueType( SA_PARAM_NN_VALID ChoiceTable* table, const char* name )
{
	int index = choice_ValueIndex( table, name );
	if( index < 0 ) {
		return WVAR_NONE;
	}
	
	return table->eaDefs[ index ]->eType;
}

static int choice_CompareStrings( const char** string1, const char** string2 )
{
	if( (!string1 || (*string1)) && (!string2 || !(*string2)) )
		return 0;

	if(!string1 || !(*string1))
		return -1;

	if(!string2 || !(*string2))
		return 1;

	return stricmp( (*string1), (*string2) );
}

/// Return a list of the names that are legal for choosing.
///
/// The list must be freed with eaDestroyEx( list, NULL ).
char** choice_ListNames( SA_PARAM_NN_STR const char* tableName )
{
	ChoiceTable* table = RefSystem_ReferentFromString( g_hChoiceTableDict, tableName );
	char** partNames = NULL;

	if( table ) {
		int it;
		for( it = 0; it != eaSize( &table->eaDefs ); ++it ) {
			ChoiceTableValueDef* def = table->eaDefs[ it ];
			eaPush( &partNames, strdup( def->pchName ));
		}

		eaQSort( partNames, choice_CompareStrings );
	}
	return partNames;
}

int choice_TotalEntriesEx(ChoiceTable *table, ChoiceEntry ***pppEntriesOut)
{
	int iTotal = 0;

	if (table)
	{
		int i;
		for(i = 0; i < eaSize( &table->eaEntry ); i++)
		{
			ChoiceEntry* entry = table->eaEntry[i];

			switch(entry->eType)
			{
			case CET_Value:
				iTotal++;
				if(pppEntriesOut)
					eaPush(pppEntriesOut,entry);

			xcase CET_Include:
				iTotal += choice_TotalEntriesEx(GET_REF(entry->hChoiceTable),pppEntriesOut);
			}
		}
	}

	return iTotal;
}

int choice_TotalEntries(ChoiceTable *table)
{
	return choice_TotalEntriesEx(table,NULL);
}

int choice_TimedRandomRange(ChoiceTable *table)
{
	if (table->eSelectType == CST_TimedRandom)
		return choice_TotalEntries(table);
	return -1;
}

int choice_TimedRandomValuesPerInterval(ChoiceTable *table)
{
	if (table->eSelectType == CST_TimedRandom)
		return table->iValuesPerInterval ? table->iValuesPerInterval : choice_TotalEntries(table);
	return -1;
}

int choice_TimedRandomNumIntervals(ChoiceTable *table)
{
	if (table->eSelectType == CST_TimedRandom)
		return table->iNumUniqueIntervals ? table->iNumUniqueIntervals : 1;
	return -1;
}

/// A version of CHOOSE-VALUE that does not expose our RNG.
///
/// It always chooses the first element.
WorldVariable* choice_ChooseValueForClient( ChoiceTable* table, const char* name )
{
	int index;

	if( !table || eaSize( &table->eaEntry ) == 0 ) {
		return &choice_defaultValue;
	}
	index = choice_ValueIndex( table, name );
	if( index < 0 ) {
		return &choice_defaultValue;
	}

	{
		ChoiceValue* value = table->eaEntry[ 0 ]->eaValues[ index ];

		switch( value->eType ) {
			case CVT_Choice: {
				ChoiceTable* valueTable = GET_REF( value->hChoiceTable );
				if( valueTable ) {
					return choice_ChooseValueForClient( valueTable, value->pchChoiceName );
				}
			}
				
			case CVT_Value:
				return &value->value;
		}
	}

	return &choice_defaultValue;
}

/// Validation callback
int choice_ResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ChoiceTable *table, U32 userID)
{
	switch( eType ) {	
		xcase RESVALIDATE_FIX_FILENAME: // Called for filename check
			resFixPooledFilename(&table->pchFileName, CHOICE_TABLE_BASE_DIR, table->pchScope, table->pchName, CHOICE_TABLE_EXTENSION);
			return VALIDATE_HANDLED;
		xcase RESVALIDATE_CHECK_REFERENCES: // Called on all objects in dictionary after any load/reload of this dictionary
			choice_Validate( table );
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void choice_RegisterDictionary( void )
{
	g_hChoiceTableDict = RefSystem_RegisterSelfDefiningDictionary("ChoiceTable", false, parse_ChoiceTable, true, true, NULL);

	resDictManageValidation( g_hChoiceTableDict, choice_ResValidateCB );
	resDictSetDisplayName( g_hChoiceTableDict, "Choice Table", "Choice Tables", RESCATEGORY_DESIGN );

	if( IsServer() ) {
		resDictProvideMissingResources( g_hChoiceTableDict );
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex( g_hChoiceTableDict, ".Name", ".Scope", NULL, ".Notes", NULL );
		}
	} else {
		resDictRequestMissingResources( g_hChoiceTableDict, 8, false, resClientRequestSendReferentCommand );
	}
	resDictProvideMissingRequiresEditMode(g_hChoiceTableDict);
}

void choice_Load( void )
{
	if( !IsClient() ) {
		resLoadResourcesFromDisk( g_hChoiceTableDict, CHOICE_TABLE_BASE_DIR, ".choice", NULL,
								  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
}

#include "AutoGen/ChoiceTable_common_h_ast.c"
