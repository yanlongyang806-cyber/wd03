#include "ChoiceTableEditor.h"

#include "WorldVariable.h"
#include "ChoiceTable_common.h"
#include "StringCache.h"
#include "EString.h"

AUTO_COMMAND ACMD_CLIENTCMD;
void choice_ChooseValueRecieveForEditor( ChoiceTable* table, const char* name, WorldVariable* var )
{
	char* str = NULL;

	var->eType = choice_ValueType( table, allocAddString( name ));
	worldVariableToEString( var, &str );
	printf( "ChoicePreview, Name: %s (%s) -- %s\n",
			name, worldVariableTypeToString( var->eType ), str );
	estrDestroy( &str );
}
