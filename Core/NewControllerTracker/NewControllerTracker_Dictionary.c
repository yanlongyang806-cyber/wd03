#include "NewControllerTracker.h"
#include "ResourceInfo.h"
#include "autogen/newcontrollertracker_pub_h_ast.h"



void NewControllerTracker_InitShardDictionary(void)
{
	resRegisterDictionaryForStashTable("Shards", RESCATEGORY_OTHER, 0, gShardsByName, parse_ShardInfo_Full);
}