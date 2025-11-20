#include "gslPowersAEDebug.h"
#include "PowersAEDebug.h"
#include "Entity.h"
#include "EntityMovementManager.h"
#include "Capsule.h"
#include "Character.h"
#include "Character_Combat.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// -------------------------------------------------------
typedef struct AEPowersDebugData
{
	EntityRef	erDebuggingEnt;
	EntityRef	erEntity;
} AEPowersDebugData; 

// -------------------------------------------------------
static AEPowersDebugData g_aePowersDebug = {0};


// ------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(PowersDebugAEEnt) ACMD_ACCESSLEVEL(7);
void PowersDebugAEEnt(Entity* e, const char* target)
{
	EntityRef debugRef = 0;
	U32 selected = !stricmp(target, "selected");
	U32 all = !stricmp(target, "all");

	if(!all && !entGetClientTarget(e, target, &debugRef) && !selected)
	{
		if (e)
		{
			ClientCmd_PowersAEDebug_TurnOffAEDebug(e);
		}

		g_aePowersDebug.erEntity = -1;
		g_powersDebugAEOn = false;
		g_aePowersDebug.erDebuggingEnt = -1;
		return;
	}
	
	g_powersDebugAEOn = true;
	if (all)
		g_aePowersDebug.erEntity = -1;
	else
		g_aePowersDebug.erEntity = debugRef;

	g_aePowersDebug.erDebuggingEnt = entGetRef(e);
}

// ------------------------------------------------------------------------------------------------
Entity* gslPowersAEDebug_GetDebuggingEnt()
{
	Entity *debugger;
	debugger = g_aePowersDebug.erDebuggingEnt != -1 ? entFromEntityRefAnyPartition(g_aePowersDebug.erDebuggingEnt) : NULL;
	return debugger;
}

S32 gslPowersAEDebug_ShouldSendForEnt(Entity *e)
{
	if (g_aePowersDebug.erEntity != -1 && g_aePowersDebug.erEntity != (e ? entGetRef(e) : 0))
	{	// debugging particular entity, not the right entity
		return false;
	}

	return true;
}
