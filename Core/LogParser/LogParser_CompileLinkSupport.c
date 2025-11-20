//stuff in here is included in order to make LogParser compile and link, because of the way it has to include
//tons of files to include the parse tables for all the things that dangle off of ParseLogs
#define PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"



#include "cmdparse.h"
#include "entity.h"

#include "ObjPath.h"

#include "GlobalTypes.h"
#include "LoginCommon.h"
//#include "LoginCommon_h_ast.c"

#include "InventoryCommon.h"
#include "itemCommon.h"


#include "Survey.h"
#include "survey_h_ast.c"

#include "rdrEnums.h"
#include "rdrEnums_h_ast.c"

#include "GraphicsLib.h"
#include "graphicslib_h_ast.c"

#include "../GameServerLib/entity/gslCostume.h"
#include "../GameServerLib/AutoGen/gslCostume_h_ast.c"

#include "../GameServerLib/pub/gslEntity.h"
#include "../GameServerLib/AutoGen/gslEntity_h_ast.c"


#include "../GameServerLib/gslmail.h"
#include "../GameServerLib/AutoGen/gslmail_h_ast.c"

#include "AuctionLot_Transact.h"
#include "../GameServerLib/AutoGen/AuctionLot_Transact_h_ast.c"

Entity *entExternGetCommandEntity(CmdContext *context)
{
	return NULL;
}


void LogParser_DoCrossroadsStartupStuff(void)
{	
	invIDs_Load();
	itemtags_Load();
	inv_Load();
}
