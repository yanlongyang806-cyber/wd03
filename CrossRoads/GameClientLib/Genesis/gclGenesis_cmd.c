#define GENESIS_ALLOW_OLD_HEADERS
#include "Genesis.h"

#include "wlGenesis.h"
#include "WorldEditorUI.h"

AUTO_COMMAND ACMD_CLIENTCMD;
void genesisDisplayRuntimeStatus( GenesisRuntimeStatus* status )
{
#ifndef NO_EDITORS
	wleGenesisDisplayErrorDialog( status );
#endif
}
