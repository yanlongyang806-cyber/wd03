/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityLib.h"

#include "EntityIterator.h"
#include "EntitySystemInternal.h"
#include "file.h"
#include "Guild.h"
#include "objContainer.h"
#include "TimedCallback.h"
#include "WorldLib.h"
#include "CostumeCommonEntity.h"
#include "Team.h"
#include "mission_common.h"
#include "chatCommonStructs.h"
#include "Player.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
#endif

#if GAMECLIENT
	#include "EntityClient.h"
	#include "GameClientLib.h"
	#include "gclEntity.h"
	#include "gclVisionModeEffects.h"
#endif

#define CHECK_FOR_CORRUPTION 1


void CorruptionCheckTimedCB(TimedCallback *callback, F32 time, void *userData)
{
	PERFINFO_AUTO_START_FUNC();
	EntSystem_CheckForCorruption();
	PERFINFO_AUTO_STOP();
}

AUTO_STARTUP(Entity);
void entityStartup(void)
{
#ifdef GAMESERVER
	gbAmGameServer = 1;
	
	mmSetIsServer();
#endif
	EntSystem_Init(gbAmGameServer,0);
	worldLibSetCheckEntityExistsFunc(entFromEntityRefAnyPartition);
	worldLibSetForceCostumeReloadFunc(costumeEntity_ForceGlobalReload);
	worldLibSetCheckEntityHasExistedFunc(entHasRefExistedRecently);

	if (isDevelopmentMode())
	{
		TimedCallback_Add(CorruptionCheckTimedCB,NULL,10.0);
	}
}

void entityLibResetState(void)
{
	EntSystem_Init(gbAmGameServer,1);
}

void entityLibOncePerFrame(F32 fFrameTime)
{
	PERFINFO_AUTO_START_FUNC();
#ifdef GAMECLIENT
	entClientCostumeSkeletonCreationsThisFrame = 0;
	gclFixEntCostumes();

	gclVisionModeEffects_OncePerFrame(fFrameTime);
#endif
	PERFINFO_AUTO_STOP_FUNC();
}


Entity *entCreateNew(GlobalType type, char *pComment)
{
	Entity *e;

	assert(gbAmGameServer);

	e = EntSystem_LowLevelGetEmptyEntity(type, pComment);

	if (!e)
	{
		return NULL;
	}

	return e;
}


Entity *entCreateNewFromEntityRef(GlobalType type, EntityRef ref, char *pComment)
{
	Entity* e;

	assert(!gbAmGameServer);

	e = EntSystem_LowLevelGetEmptyEntity_SpecifyReference(ref,type, pComment);

	if (!e)
	{
		return NULL;
	}

	return e;
}


// Destroys an existing entity
int entDestroyEx(Entity *pEnt, const char* file, int line)
{
	if (!pEnt)
	{
		return 0;
	}
	
	EntSystem_LowLevelDeleteEntityEx(pEnt, file, line);
	
	return 1;
}


Container *entGetContainer(Entity *ent)
{
	return objGetContainer(entGetType(ent),entGetContainerID(ent));
}

Entity *entFromContainerID(int iPartitionIdx, GlobalType type, ContainerID id)
{
	if (GlobalTypeParent(type) == GLOBALTYPE_ENTITY)
	{	
		Container *con = objGetContainer(type,id);
		if (!con || (iPartitionIdx != entGetPartitionIdx((Entity*)con->containerData)))
		{
			return NULL;
		}
		return con->containerData;
	}
	return NULL;
}

#if GAMESERVER
#include "gslGatewaySession.h"

Entity *gateway_entFromContainerID(GlobalType type, ContainerID id)
{
	// TODO: This is ludicrously inefficient.
	if(GlobalTypeParent(type) == GLOBALTYPE_ENTITY)
	{
		GatewaySession *psess = wgsFindOwningSessionForDBContainer(type, id);
		if(psess)
		{
			return session_GetLoginEntity(psess);
		}
	}

	return NULL;
}

#endif
Entity *entFromContainerIDAnyPartition(GlobalType type, ContainerID id)
{
	if (GlobalTypeParent(type) == GLOBALTYPE_ENTITY)
	{	
		Container *con = objGetContainer(type,id);
		if (!con)
		{
			return NULL;
		}
		return con->containerData;
	}
	return NULL;
}

Entity *entForClientCmd(ContainerID id, Entity *pEnt)
{
	if(pEnt)
		return pEnt;

#if GAMESERVER
	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		return gateway_entFromContainerID(GLOBALTYPE_ENTITYPLAYER,id);
#endif

	return NULL;
}

Entity* entFromAccountID(ContainerID id)
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity *e;

	while(e = EntityIteratorGetNext(iter))
	{
		if(entGetAccountID(e)==id)
		{
			EntityIteratorRelease(iter);
			return e;
		}
	}

	EntityIteratorRelease(iter);
	return NULL;
}

Entity *entSubscribedCopyFromContainerID(GlobalType type, ContainerID id)
{
	DictionaryHandle hDict;
	if (hDict = RefSystem_GetDictionaryHandleFromNameOrHandle(GlobalTypeToCopyDictionaryName(type)))
	{	
		char idBuf[128];
		Entity *pEnt = RefSystem_ReferentFromString(hDict, ContainerIDToString(id, idBuf));
		return pEnt;
	}
	return NULL;
}


void entRegisterExisting(Entity *pEnt)
{
	assert(pEnt);
	EntSystem_LowLevelRegisterExisting(pEnt);
}

// Get the primary mission if this entity has one, if in team only use team mission otherwise use primary solo mission
// Returns a pooled string
const char *entGetPrimaryMission(Entity *pEnt)
{
	const char *pcPrimaryMission = NULL;
	
	if(team_IsMember(pEnt))
	{
		Team *pTeam = GET_REF(pEnt->pTeam->hTeam);
		if(pTeam)
		{
			pcPrimaryMission = pTeam->pchPrimaryMission;
		}
	}
	else if(pEnt)
	{
		// primary solo mission?
		MissionInfo *pMInfo = mission_GetInfoFromPlayer(pEnt);
		if(pMInfo)
		{
			pcPrimaryMission = pMInfo->pchPrimarySoloMission;
		}
	}
	
	return pcPrimaryMission;
}

//Returns false if to's specified whitelist is enabled and from is not on to's specified whitelist (blacklisted).  Otherwise returns true.
bool entIsWhitelistedEx(Entity* to, U32 uFromID, U32 uFromAcctID, S32 eWhitelistFlag)
{
	//Check to see if whitelist is enabled
	if(to && to->pPlayer && (to->pPlayer->eWhitelistFlags & eWhitelistFlag)) 
	{	
		//Check to see if players are teammates
		if(to->pTeam && (!team_IsMember(to) || !team_OnSameTeamID(to, uFromID))) 
		{
			//Check to see if the players are in the same guild
			if(!guild_IsMember(to) || !guild_FindMemberInGuildEntID(uFromID,guild_GetGuild(to))) 
			{
				//Check to see if players are friends
				if(to->pPlayer->pUI->pChatState) 
				{
					int i;
					ChatPlayerStruct** friends = to->pPlayer->pUI->pChatState->eaFriends;

					for (i=0; i < eaSize(&friends); i++) 
					{
						ChatPlayerStruct *pPlayer = eaGet(&friends, i);
						if (pPlayer && pPlayer->accountID == uFromAcctID && ChatFlagIsFriend(pPlayer->flags))
						{
							return true;
						}
					}
					return false;
				}
				else
				{
					return false;
				}
			}
		}
	}
	return true;
}

//Returns false if to's specified whitelist is enabled and from is not on to's specified whitelist (blacklisted).  Otherwise returns true.
bool entIsWhitelistedWithPreCalculatedFriendStatus(Entity* to, U32 uFromID, U32 uFromAcctID, S32 eWhitelistFlag, bool bAreFriends)
{
	//Check to see if whitelist is enabled
	if(to && to->pPlayer && (to->pPlayer->eWhitelistFlags & eWhitelistFlag)) 
	{	
		//Check to see if players are teammates
		if(to->pTeam && (!team_IsMember(to) || !team_OnSameTeamID(to, uFromID))) 
		{
			//Check to see if the players are in the same guild
			if(!guild_IsMember(to) || !guild_FindMemberInGuildEntID(uFromID,guild_GetGuild(to))) 
			{
				return bAreFriends;
			}
		}
	}
	return true;
}

bool entIsWhitelisted(Entity* pTo, Entity* pFrom, S32 eWhitelistFlag)
{
#ifdef GAMECLIENT
	//This function does NOT work on the client if pTo is not yourself.  You do not have information about 
	// other players' guilds, teams or friends.  For now, predict this will succeed.
	if(!pTo || pTo!=entActivePlayerPtr())
		return true;
#endif

	if (pFrom && pFrom->pPlayer)
	{
		U32 uFromID = entGetContainerID(pFrom);
		U32 uAcctID = pFrom->pPlayer->accountID;
		return entIsWhitelistedEx(pTo, uFromID, uAcctID, eWhitelistFlag);
	}
	return true;
}
