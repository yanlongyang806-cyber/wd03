#include "ChoiceTable.h"
#include"ChoiceTable_common.h"

#include "StringCache.h"

typedef struct Entity Entity;
#include"GlobalTypes.h"
#include"AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

/// Server Command to request a roll of a choice table
AUTO_COMMAND ACMD_SERVERCMD;
void choice_ChooseValueForEditor( Entity* ent, ChoiceTable* table, const char* name, U32 seed )
{
	name = allocAddString( name );
	ClientCmd_choice_ChooseValueRecieveForEditor( ent, table, name, choice_ChooseValue( table, name, 0, seed, 0));
}
