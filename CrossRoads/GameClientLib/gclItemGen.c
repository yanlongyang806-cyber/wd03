#include "itemGenCommon.h"
#include "stdtypes.h"
#include "earray.h"
#include "GraphicsLib.h"
#include "Entity.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/itemGenCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Previously, when you wanted to run ItemGen it would do most of the work on the server, 
// and if any of the items needed to generate icons, it would call a client command to run
// just that portion of the ItemGen process, then call another server command to finish. 
// This was problematic on large data sets, as sometimes not all itemgen referants made it
// to the client in time. 
// 
// Really, the only thing the client was doing was checking if certain textures exist. Rather
// than kick stuff back and forth and deal with the potential problems, it's better to just send
// up a complete list of all the texture names in the dictionary up to the server, and do all 
// the processing there. 
// 
// Since all the functions are essentially the same w.r.t. getting and cleaning up the texture names
// I just pulled it out into a macro. 

#define ItemGen_CommandWrapper(cmd) \
{ \
	ItemGenTextureNames *pTexNames = StructCreate(parse_ItemGenTextureNames); \
	texGetTexNames(&pTexNames->eapchTextureNames); \
	cmd; \
	eaClearFast(&pTexNames->eapchTextureNames); \
	StructDestroy(parse_ItemGenTextureNames, pTexNames); \
}

AUTO_COMMAND;
void ItemGen_GenerateDef(const char *pchItemGenDef)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateDef(pchItemGenDef, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateAllDefs(const char *pchItemGenDef)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateAllDefs(pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateByScope(const char *pchScope)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateByScope(pchScope, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateAllDefsWithPrefix(const char* pchPrefix)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateAllDefsWithPrefix(pchPrefix, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateDefsMatchingWildcardString(const char* pchWildcardString)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateDefsMatchingWildcardString(pchWildcardString, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateDefsFromFile(const char* pchFilePath)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateDefsFromFile(pchFilePath, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateRewardTables(ACMD_NAMELIST("ItemGenData", REFDICTIONARY) const char *pchItemGenDef)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateRewardTables(pchItemGenDef, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateAllRewardTables()
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateAllRewardTables(pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateDefNoSave(ACMD_NAMELIST("ItemGenData", REFDICTIONARY) const char *pchItemGen)
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateDefNoSave(pchItemGen, pTexNames));
}

AUTO_COMMAND;
void ItemGen_GenerateAllNoSave()
{
	ItemGen_CommandWrapper(ServerCmd_ItemGen_GenerateAllNoSave(pTexNames));
}

#undef ItemGen_CommandWrapper