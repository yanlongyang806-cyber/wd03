/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Character_target.h"

#include "entCritter.h"
#include "EntityIterator.h"
#include "LineDist.h"
#include "Player.h"
#include "rand.h"
#include "StringCache.h"
#include "Team.h"
#include "AutoTransDefs.h"
#include "cutscene_common.h"

#include "Character.h"
#include "CombatConfig.h"
#include "GameAccountDataCommon.h"
#include "PowerActivation.h"
#include "PowerAnimFX.h"
#include "PowerModes.h"

#include "WorldColl.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "SuperCritterPet.h"

#if GAMESERVER || GAMECLIENT
	#include "PowersMovement.h"
	#include "EntityMovementManager.h"
#endif

#if GAMESERVER 
	#include "gslInteractionManager.h"
	#include "aiLib.h"
	#include "aiStruct.h"
	#include "damagetracker.h"
	#include "gslEntity.h"
#elif GAMECLIENT
	#include "ClientTargeting.h"
	#include "cmdClient.h"
#endif

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "Autogen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

const char* neutralString;
const char* everyoneHatesMeString;

AUTO_RUN;
void critter_RegisterPooledStrings(void)
{
	neutralString = allocAddStaticString("Neutral");
	everyoneHatesMeString = allocAddStaticString("EveryoneHatesMe");
}

// Gets the relevant Entity for relation testing
SA_RET_NN_VALID Entity* entity_EntityGetRelationEnt(int iPartitionIdx, SA_PARAM_NN_VALID Entity *e)
{
	Entity *eOwner;

	if(e->pChar && e->pChar->erHeldBy)
	{
		eOwner = entFromEntityRef(iPartitionIdx, e->pChar->erHeldBy);
		if(eOwner)
			e = eOwner;
	}

	// Switch e out for owner, if it has one.  This
	//  means all pets are forced at all times to their owner's faction
	// JW: I've no idea why this is a while loop, as an entity should
	//  never be both an owner and owned at the same time...
	while(e->erOwner && (eOwner=entFromEntityRef(iPartitionIdx, e->erOwner)))
	{
		e = eOwner;
	}

	return e;
}

// Checks for relation based on Character gangID, returns Friend, Foe or Unknown
static EntityRelation EntityGetGangRelation(SA_PARAM_NN_VALID Entity *e1, SA_PARAM_NN_VALID Entity *e2, int bConfused)
{	
	if(e1->pChar && e2->pChar && e1->pChar->gangID && e2->pChar->gangID)
	{
		if(bConfused)
			return (e1->pChar->gangID == e2->pChar->gangID) ? kEntityRelation_Foe : kEntityRelation_Friend;
		else
			return (e1->pChar->gangID != e2->pChar->gangID) ? kEntityRelation_Foe : kEntityRelation_Friend;
	}

	return kEntityRelation_Unknown;
}

// Checks for relation based on faction, returns Friend or Foe
static EntityRelation EntityGetFactionRelation(SA_PARAM_NN_VALID Entity *e1, SA_PARAM_NN_VALID Entity *e2, int bConfused)
{
	EntityRelation eRelation;
	CritterFaction *f1, *f2;

	f1 = entGetFaction(e1);
	f2 = entGetFaction(e2);

	if( !f1 || !f2 )
		return kEntityRelation_Friend;   // Just default to friendly if unknown

	eRelation = faction_GetRelation(f1,f2);

	if(bConfused)
	{
		// Make "always neutral" or "always hated" factions not invert like the other ones
		// Kinda crappy, but since this is being rewritten at some point soon anyway (apparently)
		// and i don't want to do a lengthy reverse lookup, this'll have to do for now

		// neutral never hates anything, and is never hated by anything
		if(f2->pchName == neutralString || f1->pchName == neutralString)
			return kEntityRelation_Friend;

		// the only change for confused everyone hates me guys is that they hate their own
		// faction too now
		if(f2->pchName == everyoneHatesMeString)
			return kEntityRelation_Foe;

		// Confused just swaps Friend and Foe.  It doesn't affect the other possibilities.
		if(eRelation==kEntityRelation_Friend)
			return kEntityRelation_Foe;
		if(eRelation==kEntityRelation_Foe)
			return kEntityRelation_Friend;
	}

	return eRelation;
}

static S32 critter_IsConfused(Entity *e)
{
#ifdef GAMESERVER
	if(e->aibase && e->aibase->confused)
		return true;
#endif
	return SAFE_MEMBER2(e->pChar, pattrBasic, fConfuse)>0;
}

// Determine relation between two entities, optionally ignores confusion
EntityRelation entity_GetRelationEx(int iPartitionIdx, Entity *e1, Entity *e2, bool bIgnoreConfusion)
{
	int confused1;
	int confused2;
	int relation = kEntityRelation_Friend;

	PERFINFO_AUTO_START_FUNC_L2();

	// How this managed to compile without this check I have no idea
	if(!e1 || !e2)
	{
		PERFINFO_AUTO_STOP_L2();
		return kEntityRelation_Friend;
	}

	if(e1 == e2)
	{
		PERFINFO_AUTO_STOP_L2();
		return kEntityRelation_Friend;
	}

	// Switch e1 and e2 out for whomever determines their factions
	e1 = entity_EntityGetRelationEnt(iPartitionIdx, e1);
	e2 = entity_EntityGetRelationEnt(iPartitionIdx, e2);
	if(e1 == e2)
	{
		PERFINFO_AUTO_STOP_L2();
		return kEntityRelation_Friend;
	}

	// Make sure they're in the same partition
	if(entGetPartitionIdx(e1) != entGetPartitionIdx(e2))
		return kEntityRelation_Neutral;

	confused1 = !bIgnoreConfusion && critter_IsConfused(e1);
	confused2 = !bIgnoreConfusion && critter_IsConfused(e2);

#ifdef GAMESERVER
	if(e1->pChar && confused2)
	{
		EntityRef oppRef = entGetRef(e2);
		Entity* checkE = e1;
		int i;

		// check up the whole tree of possible owners for damage to allow you to attack back
		do 
		{
			for(i = eaSize(&checkE->pChar->ppDamageTrackers)-1; i >= 0; i--)
			{
				DamageTracker* dt = checkE->pChar->ppDamageTrackers[i];

				if(dt->erSource == oppRef)
				{
					PERFINFO_AUTO_STOP_L2();
					return kEntityRelation_Foe;
				}
			}
			checkE = checkE->erOwner ? entFromEntityRef(entGetPartitionIdx(e1), checkE->erOwner) : NULL;
		} while(checkE);
	}
#endif

	if(confused1 && confused2)
	{
		// If both are confused, they are treated as enemies
		PERFINFO_AUTO_STOP_L2();
		return kEntityRelation_Foe;
	}

	// Check pvp flag relation before gang
	relation = entity_PVP_GetRelation(e1, e2);
	if(relation!=kEntityRelation_Unknown)
	{
		PERFINFO_AUTO_STOP_L2();
		return relation;
	}

	relation = EntityGetGangRelation(e1, e2, confused1);
	if(relation!=kEntityRelation_Unknown)
	{
		PERFINFO_AUTO_STOP_L2();
		return relation;
	}

	relation = EntityGetFactionRelation(e1, e2, confused1);
	if(relation!=kEntityRelation_Unknown)
	{
		PERFINFO_AUTO_STOP_L2();
		return relation;
	}

	PERFINFO_AUTO_STOP_L2();
	return kEntityRelation_Friend;
}

// Wrapper for entity_GetRelationEx that converts neutral or unknown relations into friendly
EntityRelation critter_IsKOSEx(int iPartitionIdx, Entity *e1, Entity *e2, bool bIgnoreConfusion)
{
	EntityRelation relation = entity_GetRelationEx(iPartitionIdx, e1, e2, bIgnoreConfusion);
	if(relation==kEntityRelation_Neutral || relation==kEntityRelation_Unknown)
		return kEntityRelation_Friend;
	return relation;
}

// Check whether e1 considers e2 a Foe from gangs/factions.  Ignores confusion and PVP flag.
S32 critter_IsFactionKOS(int iPartitionIdx, Entity *e1, Entity *e2)
{
	EntityRelation relation;
	e1 = entity_EntityGetRelationEnt(iPartitionIdx, e1);
	e2 = entity_EntityGetRelationEnt(iPartitionIdx, e2);

	relation = EntityGetGangRelation(e1, e2, false);
	if(relation!=kEntityRelation_Unknown)
		return (relation==kEntityRelation_Foe);

	relation = EntityGetFactionRelation(e1, e2, false);
	if(relation!=kEntityRelation_Unknown)
		return (relation==kEntityRelation_Foe);

	return false;
}

// Returns the relation the given faction has towards the target faction.  Returns Friend if there is no target
//  faction, the faction doesn't specify a relation towards the target faction, or the relation is defined
//  as Unknown, otherwise returns the actual value.
EntityRelation faction_GetRelation(CritterFaction *pFaction, CritterFaction *pFactionTarget)
{
	EntityRelation eRelation = kEntityRelation_Friend;
	if(pFactionTarget)
	{
		int i;
		for(i=eaSize(&pFaction->relationship)-1; i>=0; i--)
		{
			CritterFaction *pFactionRel = GET_REF(pFaction->relationship[i]->hFactionRef);
			if (pFactionTarget == pFactionRel)
			{
				eRelation = pFaction->relationship[i]->eRelation;
				if(eRelation==kEntityRelation_Unknown)
					eRelation = kEntityRelation_Friend;
				break;
			}
		}
	}
	return eRelation;
}



static bool CharacterCanPerceiveCutscenePathList(const CutsceneDef *pCutScene, const Vec3 vecTarget, F32 fPerception)
{
	int i, j;
	F32 fDist;
	bool bPerceive = false;

	for( i=0; !bPerceive && i < eaSize(&pCutScene->pPathList->ppPaths); i++ )
	{
		const CutscenePath *pPath = pCutScene->pPathList->ppPaths[i];
		for( j=0; j < eaSize(&pPath->ppPositions); j++ )
		{
			fDist = distance3Squared(pPath->ppPositions[j]->pos,vecTarget);
			if(fDist <= SQR(fPerception))
			{
				bPerceive = true;
				break;
			}
		}
	}

	return bPerceive;
}

F32 character_DistApplyStealth(Entity *target, F32 fDist, F32 fStealth, F32 fStealthSight)
{
	if(fStealthSight <= 0)
		fStealthSight = 1.0f;
	
	fDist -= (fStealth / fStealthSight);
	fDist = MAX(fDist,0);

	return fDist;
}

F32 character_GetPerceptionDist(Character *pchar, Character *pcharTarget)
{
	if(!pchar)
		return 0;
	else if(!pcharTarget)
		return pchar->pattrBasic->fPerception;
	else
	{
		F32 fPerception = pchar->pattrBasic->fPerception;
		F32 fPerceptionStealth = pcharTarget->pattrBasic->fPerceptionStealth;
		F32 fStealthSight = pchar->pattrBasic->fStealthSight;

		
		if (g_CombatConfig.eaiPerceptionStealthDisabledAttribs)
		{
			S32 i;
			for (i = eaiSize(&g_CombatConfig.eaiPerceptionStealthDisabledAttribs) - 1; i >= 0; --i)
			{
				S32 attrib = g_CombatConfig.eaiPerceptionStealthDisabledAttribs[i];
				if (attrib >= 0 && IS_NORMAL_ATTRIB(attrib))
				{
					F32 fAttrib = *F32PTR_OF_ATTRIB(pcharTarget->pattrBasic, attrib);
					if (fAttrib > 0)
					{
						fPerceptionStealth = 0.f;
						break;
					}
				}
			}
		}

		return character_DistApplyStealth(pcharTarget->pEntParent, fPerception, 
											fPerceptionStealth, fStealthSight);
	}
}

bool character_CanPerceive(int iPartitionIdx, Character *pchar, Character *pcharTarget)
{
	bool bPerceive = true;

	if(!pcharTarget)
		return false;
	if(entIsProjectile(pcharTarget->pEntParent))
		return true;

	//Check hidden targets list
	if(pchar->perHidden && ea32Size(&pchar->perHidden) > 0)
	{
		if(ea32Find(&pchar->perHidden,pcharTarget->pEntParent->myRef) > -1)
			return false; //Cannot perceive, no matter the characters perception
	}

	if(pchar!=pcharTarget)
	{
		F32 fDist;
		F32 fPerceptionDist;
		F32 fStealthSight = pchar->pattrBasic->fStealthSight;
		EntityRelation eRelation = entity_GetRelationEx(iPartitionIdx,pchar->pEntParent,pcharTarget->pEntParent,true);

		if(eRelation==kEntityRelation_Friend)
		{
			return true;
		}

		if(fStealthSight <= 0)
			fStealthSight = 1.0f;

		fPerceptionDist = character_GetPerceptionDist(pchar, pcharTarget);
		
		fDist = entGetDistance(pchar->pEntParent, NULL, pcharTarget->pEntParent, NULL, NULL);

		if(pcharTarget->pEntParent->fEntityMinSeeAtDistance > fPerceptionDist)
		{
			fPerceptionDist = pcharTarget->pEntParent->fEntityMinSeeAtDistance;	
		}

		bPerceive = (fDist <= fPerceptionDist);

		if(!bPerceive && pchar->pEntParent->pPlayer && pchar->pEntParent->pPlayer->pCutscene)
		{
			int i;
			CutsceneDef *pCutScene = pchar->pEntParent->pPlayer->pCutscene;
			Vec3 vecTarget;
			
			entGetPos(pcharTarget->pEntParent, vecTarget);

			if(pCutScene->pPathList)
			{
				if(CharacterCanPerceiveCutscenePathList(pCutScene, vecTarget, fPerceptionDist))
					bPerceive = true;
			}
			else
			{
				for(i=0;i<eaSize(&pCutScene->ppCamPositions);i++)
				{
					fDist = distance3Squared(pCutScene->ppCamPositions[i]->vPos,vecTarget);
					if(fDist <= SQR(fPerceptionDist))
					{
						bPerceive = true;
						break;
					}
				}
			}
		}
	}
	return bPerceive;
}

// Returns Friend/Foe/Neutral target type data, factoring in confused state
static TargetType TargetTypeFFN(int iPartitionIdx, SA_PARAM_OP_VALID Entity *pentSource, SA_PARAM_NN_VALID Entity *pentTarget)
{
	TargetType eType = 0;

	// friend OR foe
	if(pentSource)
	{
		EntityRelation eRelation = entity_GetRelation(iPartitionIdx, pentSource, pentTarget);
		switch(eRelation)
		{
			xcase kEntityRelation_Foe: eType = kTargetType_Foe;
			xcase kEntityRelation_Friend: eType = kTargetType_Friend;
			xcase kEntityRelation_FriendAndFoe: eType = kTargetType_Foe | kTargetType_Friend;
			xcase kEntityRelation_Unknown:
			case kEntityRelation_Neutral: eType = kTargetType_Neutral;
		}

		// If the source is confused and the target is either Friend or Foe, it is both friend and foe
		if(pentSource->pChar
			&& pentSource!=pentTarget
			&& eType&(kTargetType_Friend | kTargetType_Foe)
			&& pentSource->pChar->pattrBasic->fConfuse > 0)
		{
			eType = kTargetType_Friend | kTargetType_Foe;
		}
	}
	else
	{
		// If pentSource is null, the target is both friend and foe
		eType = kTargetType_Friend | kTargetType_Foe;
	}

	return eType;
}

static TargetType TargetTypeTeammate(int iPartitionIdx, SA_PARAM_OP_VALID Entity *pentSource, SA_PARAM_NN_VALID Entity *pentTarget)
{
	TargetType eType = 0;

	if(pentSource)
	{
		Entity *pentSourceOwner = entity_EntityGetRelationEnt(iPartitionIdx, pentSource);
		Entity *pentTargetOwner = entity_EntityGetRelationEnt(iPartitionIdx, pentTarget);
		if(pentSourceOwner==pentTargetOwner || team_OnSameTeam(pentSourceOwner, pentTargetOwner))
			eType = kTargetType_Teammate;
	}

	return eType;
}

// Return the target mask relating the given characters.
TargetType character_MakeTargetType(int iPartitionIdx, SA_PARAM_OP_VALID Character *pcharSource, SA_PARAM_OP_VALID Character *pcharTarget)
{
	TargetType eRel = 0;
	TargetType eTemp;
	Entity *entSource,*entTarget;

	// If there's no character target, it's not anything we care about
	if(!pcharTarget)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	entSource = pcharSource ? pcharSource->pEntParent : NULL; // entSource MAY BE NULL
	entTarget = pcharTarget->pEntParent;

	// This code tries to be smart about the layout and relationships of target types
	//  Assumes a compare returns 1 for true and 0 for false

	// alive
	if(entIsAlive(entTarget))
		eRel |= kTargetType_Alive;

	// self
	if(pcharSource==pcharTarget)
		eRel |= kTargetType_Self;

	// player OR critter
	eTemp = kTargetType_Player;
	eTemp <<= (!entCheckFlag(entTarget,ENTITYFLAG_IS_PLAYER)); // critter test
	eRel |= eTemp;

	// friend/foe/neutral
	eTemp = TargetTypeFFN(iPartitionIdx,entSource,entTarget);
	eRel |= eTemp;

	// TODO(JW): Targeting: Make destructible check more accurate
	eTemp = IS_HANDLE_ACTIVE(entTarget->hCreatorNode) ? kTargetType_Destructible : 0;
	eRel |= eTemp;

	if(entTarget->pCritter && entTarget->pCritter->bPseudoPlayer)
		eRel |= kTargetType_PseudoPlayer;

	eTemp = entCheckFlag(entTarget, ENTITYFLAG_UNTARGETABLE)?kTargetType_Untargetable:0;
	eRel |= eTemp;

	eTemp = kTargetType_PrimaryPet;
	if (entSource && entSource->pChar && entSource->pChar->primaryPetRef && 
		entGetRef(entTarget) == entSource->pChar->primaryPetRef)
	{
		eRel |= eTemp;
	}
	else if (entTarget && entSource && entSource->pChar && scp_GetSummonedPetEntRef(entSource) == entGetRef(entTarget))
	{
		eRel |= eTemp;
	}
	eTemp = kTargetType_Ridable;
	if (entTarget->pCritter && entTarget->pCritter->bRidable)
	{
		eRel |= eTemp;
	}

	if (entCheckFlag(entTarget, ENTITYFLAG_CRITTERPET))
		eRel |= kTargetType_Critterpet;

	// teammate
	eTemp = TargetTypeTeammate(iPartitionIdx,entSource,entTarget);
	eRel |= eTemp;

	// owner and creator
	if(entSource && entSource->erOwner)
	{
		EntityRef erTarget = entGetRef(entTarget);
		if(entSource->erOwner==erTarget)
		{
			eRel |= kTargetType_Owner;
		}
		if(entSource->erCreator==erTarget)
		{
			eRel |= kTargetType_Creator;
		}
	}

	// owned and created
	if(entSource && entTarget->erOwner)
	{
		EntityRef erSource = entGetRef(entSource);
		if(erSource==entTarget->erOwner)
		{
			eRel |= kTargetType_Owned;
		}
		if(erSource==entTarget->erCreator)
		{
			eRel |= kTargetType_Created;
		}
	}

	// NearDeath
	if(pcharTarget && pcharTarget->pNearDeath)
		eRel |= kTargetType_NearDeath;

	PERFINFO_AUTO_STOP();

	return eRel;
}

// Return the target mask relating the source Character to target non-entity node
TargetType character_MakeTargetTypeNode(Character *pcharSource)
{
	// Very similar to character_MakeTargetType, but tweaked for non-instanced objects
	static TargetType eRelDefault = kTargetType_Alive | kTargetType_Critter | kTargetType_Foe | kTargetType_Destructible;
	TargetType eRel = eRelDefault;

	PERFINFO_AUTO_START_FUNC();

	// Check to see if the source in confused, in that case, it is his friend and foe
	if(pcharSource && pcharSource->pattrBasic->fConfuse > 0)
	{
		eRel |= kTargetType_Friend | kTargetType_Foe;
	}

	PERFINFO_AUTO_STOP();

	return eRel;
}





// Returns true if the target and character have all of the relationships specified in the target type
bool character_TargetMatchesTypeRequire(int iPartitionIdx, Character *pcharSource, Character *pcharTarget, TargetType eType)
{
	TargetType eRel = character_MakeTargetType(iPartitionIdx,pcharSource,pcharTarget);
	return ((eRel&eType)==eType);
}

// Returns true if the target and character have none of the relationships specified in the target type
bool character_TargetMatchesTypeExclude(int iPartitionIdx, Character *pcharSource, Character *pcharTarget, TargetType eType)
{
	TargetType eRel = character_MakeTargetType(iPartitionIdx,pcharSource,pcharTarget);
	return ((eRel&eType)==0);
}

//static unsigned int s_auiTargetReqFailTracker[kTargetType_Count+1] = {0};
//static unsigned int s_auiTargetExcFailTracker[kTargetType_Count+1] = {0};

// Returns true if the target matches the given type for the source
S32 character_TargetMatchesType(int iPartitionIdx, Character *pcharSource, Character *pcharTarget, TargetType eTypeRequire, TargetType eTypeExclude)
{
	TargetType eRel = character_MakeTargetType(iPartitionIdx,pcharSource,pcharTarget);
	// Final test.. could potentially short-circuit this at commonly missed stages, but we need to figure
	//  out what those are!
#if 1 // Normal mode
	return ((eTypeRequire & eRel)==eTypeRequire && !(eTypeExclude & eRel));
#else // Fail tracking mode
	{
		TargetType eReqMatch = eTypeRequire & eRel;
		TargetType eExcMatch = eTypeExclude & eRel;
		if(eReqMatch==eTypeRequire && !eExcMatch)
		{
			return true;
		}
		else
		{
			TargetType eReqFail = eTypeRequire & (~eReqMatch);
			if(eReqFail)
			{
				int i = 0;
				s_auiTargetReqFailTracker[0]++; // Total failures
				while(eReqFail)
				{
					i++;
					if(eReqFail&1)
					{
						s_auiTargetReqFailTracker[i]++; // Failures of each type
					}
					eReqFail >>= 1;
				}
			}
			if(eExcMatch)
			{
				int i = 0;
				s_auiTargetExcFailTracker[0]++; // Total failures
				while(eExcMatch)
				{
					i++;
					if(eExcMatch&1)
					{
						s_auiTargetExcFailTracker[i]++; // Failures of each type
					}
					eExcMatch >>= 1;
				}
			}
			return false;
		}
	}
#endif
}

static S32 RelationMatchesPowerType(TargetType eRel, SA_PARAM_NN_VALID PowerTarget *ptarget)
{
	S32 i, bMatch = ((ptarget->eRequire & eRel)==ptarget->eRequire && !(ptarget->eExclude & eRel));
	if(!bMatch && (i=eaSize(&ptarget->ppOrPairs)))
	{
		for(i=i-1; i>=0 && !bMatch; i--)
			bMatch = ((ptarget->ppOrPairs[i]->eRequire & eRel)==ptarget->ppOrPairs[i]->eRequire && !(ptarget->ppOrPairs[i]->eExclude & eRel));
	}
	return bMatch;
}

// Returns true if the target matches the given power target type for the source
S32 character_TargetMatchesPowerType(int iPartitionIdx, Character *pcharSource, Character *pcharTarget, PowerTarget *ptarget)
{
	TargetType eRel = character_MakeTargetType(iPartitionIdx,pcharSource,pcharTarget);
	return RelationMatchesPowerType(eRel,ptarget);
}

// Returns true if the target node matches the given type for the source
S32 character_TargetMatchesTypeNode(Character *pcharSource, U32 eTypeRequire, U32 eTypeExclude)
{
	TargetType eRel = character_MakeTargetTypeNode(pcharSource);
	return ((eTypeRequire & eRel)==eTypeRequire && !(eTypeExclude & eRel));
}

// Returns true if the target node matches the given power target type for the source
S32 character_TargetMatchesPowerTypeNode(Character *pcharSource, PowerTarget *ptarget)
{
	TargetType eRel = character_MakeTargetTypeNode(pcharSource);
	return RelationMatchesPowerType(eRel,ptarget);
}

// Returns true if the target is considered a Foe.  Faster than passing kTargetType_Foe to the normal
//  target type functions.
S32 character_TargetIsFoe(int iPartitionIdx, Character *pcharSource, Character *pcharTarget)
{
	TargetType eType;

	// If there's no character target, it's not a foe
	if(!pcharTarget)
		return false;

	eType = TargetTypeFFN(iPartitionIdx,pcharSource?pcharSource->pEntParent:NULL,pcharTarget->pEntParent);
	return eType&kTargetType_Foe;
}



// Sends rays in the six cardinal directions. If any of them hit a backfacing
//  polygon first, then we are inside some object. That's a no-no. We also
//  require that we can find some front-facing polygon in all directions
//  but upwards. If we can't then we must be off the edge somewhere.
static S32 TargetOutOfBounds(int iPartitionIdx, Vec3 vecTarget)
{
	static Vec3 s_avecTests[] =
	{
		{       0.0f,   20000.0f,       0.0f }, // up
		{       0.0f,  -20000.0f,       0.0f }, // down
		{   20000.0f,       0.0f,       0.0f }, // left
		{  -20000.0f,       0.0f,       0.0f }, // right
		{       0.0f,       0.0f,   20000.0f }, // forward
		{       0.0f,       0.0f,  -20000.0f }  // back
	};

	// Corresponds to the list above. Specifies which direction MUST hit
	//   something front-facing for this to be a valid teleport location.
	static S32 s_abRequiredHits[] =
	{
		false, // up
		true,  // down
		true,  // left
		true,  // right
		true,  // forward
		true,  // back
	};

	S32 abHits[6] = {0};
	S32 bOutOfBounds = false;
	S32 i;

	for(i=0; i<ARRAY_SIZE(s_avecTests); i++)
	{
		Vec3 vecTest;
		addVec3(vecTarget, s_avecTests[i], vecTest);
		if(worldCollideRay(iPartitionIdx, vecTarget, vecTest, WC_QUERY_BITS_WORLD_ALL | WC_QUERY_BITS_ENTITY_MOVEMENT,NULL))
		{
			abHits[i] = true;
			/*
			if(coll.backside)
			{
				CollInfo coll2;
				if(collGrid(NULL, vTarget, vTest, &coll2, 0, COLL_NOTSELECTABLE|COLL_DISTFROMSTART))
				{
					if(distance3Squared(coll2.mat[3], coll.mat[3]) < 0.01)
					{
						// This is really close to another polygon facing the
						// other way; they might be part of a coplanar
						// pair. Coplanar pairs are found in world objects
						// like trashcans and the like.
						// So, we're going to ignore this possible
						// hit, assuming that one of the other rays will
						// also hit a backside triangle if we really are
						// inside something.
						// I doubt that this will allow sticking
						// people inside of trashcans and such since the
						// item needs to be big enough to contain them as
						// well.
					}
					else
					{
						bOutOfBounds = true;
						break;
					}
				}
			}
			*/
		}
	}

	for(i=0; i<ARRAY_SIZE(s_abRequiredHits); i++)
	{
		if(s_abRequiredHits[i] && !abHits[i])
		{
			bOutOfBounds = true;
			break;
		}
	}

	return bOutOfBounds;
}

// Returns true if the target location is a valid place to teleport the entity
S32 entity_LocationValid(Entity *pent, Vec3 vecTarget)
{
	S32 bValid = false;
	int iPartitionIdx = entGetPartitionIdx(pent);

	// Check and see if a standard capsule hits the world here
	// TODO(JW): Use proper mm collision check with proper mm collision data
	if(!wcCapsuleCollide(worldGetActiveColl(iPartitionIdx),vecTarget,vecTarget,WC_QUERY_BITS_WORLD_ALL | WC_QUERY_BITS_ENTITY_MOVEMENT,NULL))
	{
		if(!TargetOutOfBounds(iPartitionIdx, vecTarget))
		{
			bValid = true;
		}
	}

	return bValid;
}
int entity_TargetNodeInArc(Entity *pent,
						   WorldInteractionNode *pNodeTarget,
						   Vec3 vecTarget,
						   F32 fArc,
						   F32 fYawOffset)
{
	Vec3 vecPos, vecPYR, vecDir;
	Vec3 vecTargetActual;
	Mat3 mDir;
	F32 fAngle;
	F32 dp;

	if(!pNodeTarget && !vecTarget)
	{
		return false;
	}

	entGetCombatPosDir(pent,NULL,vecPos,NULL);

	if(pNodeTarget)
	{
#ifdef GAMECLIENT
		clientTarget_GetNearestPointForTargetNode(pent,pNodeTarget,vecTargetActual);
#else
		character_FindNearestPointForObject(pent->pChar,NULL,pNodeTarget,vecTargetActual,true);
#endif
		subVec3(vecTargetActual,vecPos,vecTargetActual);
	}
	else
	{
		subVec3(vecTarget,vecPos,vecTargetActual);
	}
	normalVec3(vecTargetActual);

	entGetFacePY(pent,vecPYR);
	vecPYR[2] = 0;

	createMat3YPR(mDir,vecPYR);
	yawMat3(fYawOffset,mDir);

	copyVec3(mDir[2],vecDir);
	normalVec3(vecDir);

	dp = dotVec3(vecDir,vecTargetActual);
	fAngle = acos(MINMAX(dp, -1.0f, 1.0f)); // dot sometimes tiny bit higher than 1.0

	return (fabs(fAngle)<=fArc/2.f);
}

static int TargetCapsuleInArc(Vec3 vecPos, Vec3 vecDir, Quat rotTarget, Vec3 vecTargetActual, const Capsule* capTarget, F32 fArc)
{
	const F32 fConeLen = 10000.0f;
	Vec3 vCapStart, vCapEnd, vCapDir, vecEnd, vTargetDir;

	quatRotateVec3(rotTarget, capTarget->vStart, vCapStart);
	addVec3(vCapStart, vecTargetActual, vCapStart);
	quatRotateVec3(rotTarget, capTarget->vDir, vCapDir);
			
	if (g_CombatConfig.bTargetArcIgnoresVertical)
	{
		vCapStart[1] = 0.0f;
		vCapEnd[1] = 0.0f;
	}
	scaleAddVec3(vCapDir, capTarget->fLength, vCapStart, vCapEnd);
	scaleAddVec3(vecDir, fConeLen, vecPos, vecEnd);

	if (fArc > PI - 0.0001f)
	{
		// Early out if the one of the capsule endpoints is in front of the player
		subVec3(vCapStart, vecPos, vTargetDir);
		if (dotVec3(vecDir, vTargetDir) >= 0.0f)
		{
			return true;
		}
		subVec3(vCapEnd, vecPos, vTargetDir);
		if (dotVec3(vecDir, vTargetDir) >= 0.0f)
		{
			return true;
		}
		if (fArc > PI + 0.0001f)
		{
			F32 fHalfArc = fArc * 0.5f;
			Vec3 vConeDir;
			F32 fTanConeAngle = tanf(PI - fHalfArc);
			setVec3(vConeDir, -vecDir[0], -vecDir[1], -vecDir[2]);

			// If the capsule is not entirely contained in the negative-cone, then the target is in the specified arc
			if (!isSphereInsideCone(vecPos, vConeDir, fConeLen, fTanConeAngle, vCapStart, capTarget->fRadius) ||
				!isSphereInsideCone(vecPos, vConeDir, fConeLen, fTanConeAngle, vCapEnd, capTarget->fRadius))
			{
				return true;
			}
		}
	}
	else
	{
		Vec3 vC1, vC2;
		F32 fS, fT, fDistSqr, fConeRadius;
		F32 fHalfArc;

		fDistSqr = LineSegLineSegDistSquared(vecPos, vecEnd, vCapStart, vCapEnd, &fS, &fT, vC1, vC2);

		// Early out if the closest point is behind the player
		subVec3(vC2, vecPos, vTargetDir);	
		if (dotVec3(vecDir, vTargetDir) < 0.0f)
		{
			return false;
		}

		fHalfArc = fArc * 0.5f;
		fConeRadius = fS * fConeLen * tanf(fHalfArc);
			
		// If the closest point on the cone's axis to the capsule's segment is less than the radius of the cone
		// then this is within the specified arc
		if (fDistSqr < fConeRadius * fConeRadius)
		{
			return true;
		}
		else
		{
			Vec3 vDC, vDCE, vConeEdge;
			subVec3(vC2, vC1, vDC);
			normalVec3(vDC);
			scaleByVec3(vDC, fConeRadius);
			addVec3(vC1, vDC, vConeEdge);
			subVec3(vConeEdge, vC2, vDCE);

			// If the distance from vConeEdge (which is the point from the cone's apex along its 
			// surface which is between the cone's axis and closest point on the capsule's segment)
			// to vC2 (the closest point on the capsule's segment) is less than the capsule radius,
			// then this is within the specified arc
			if (lengthVec3(vDCE) < capTarget->fRadius)
			{
				return true;
			}
		}
	}
	return false;
}

// Returns true if the entity or vector in question is in the given arc of the primary entity at the yaw offset
int entity_TargetInArc(Entity *pent,
					   Entity *pentTarget,
					   Vec3 vecTarget,
					   F32 fArc,
					   F32 fYawOffset)
{
	Vec3 vecPos, vecPYR, vecDir;
	Vec3 vecTargetActual;
	F32 fAngle;
	F32 dp;
	
	if(!pentTarget && !vecTarget)
	{
		return false;
	}

	entGetCombatPosDir(pent,NULL,vecPos,NULL);

	entGetFacePY(pent,vecPYR);
	vecPYR[2] = 0;

	if (g_CombatConfig.bTargetArcIgnoresVertical)
	{
		vecPYR[0] = 0.0f;
		vecPYR[1] = addAngle(vecPYR[1], fYawOffset);
	
		vecDir[1] = 0.0f;
		sincosf(vecPYR[1], &vecDir[0], &vecDir[2]);

		vecPos[1] = 0.0f;
	}
	else
	{
		Mat3 xMat;
		createMat3YP(xMat, vecPYR);
		if (!nearSameF32(fYawOffset, 0.0f))
		{
			yawMat3(fYawOffset, xMat);
		}
		copyVec3(xMat[2], vecDir);
	}

#if GAMESERVER || GAMECLIENT
	// If bUseCapsuleForPowerArcChecks is set, 
	// check the arc against the target's capsule instead of the target's position
	if (SAFE_MEMBER2(pentTarget, pCritter, bUseCapsuleForPowerArcChecks))
	{
		const Capsule*const* capsules;
		if (mmGetCapsules(pentTarget->mm.movement, &capsules))
		{
			int i;
			Quat rotTarget;
			entGetCombatPosDir(pentTarget,NULL,vecTargetActual,NULL);
			entGetRot(pentTarget,rotTarget);
			
			for (i = eaSize(&capsules)-1; i >= 0; i--)
			{
				if (TargetCapsuleInArc(vecPos, vecDir, rotTarget, vecTargetActual, capsules[i], fArc))
				{
					return true;
				}
			}
			return false;
		}
	}
#endif

	if(pentTarget)
	{
		entGetCombatPosDir(pentTarget,NULL,vecTargetActual,NULL);
		subVec3(vecTargetActual,vecPos,vecTargetActual);
	}
	else
	{
		subVec3(vecTarget,vecPos,vecTargetActual);
	}
	
	if(g_CombatConfig.bTargetArcIgnoresVertical)
		vecTargetActual[1] = 0;

	normalVec3(vecTargetActual);

	dp = dotVec3(vecDir,vecTargetActual);
	fAngle = acos(MINMAX(dp, -1.0f, 1.0f)); // dot sometimes tiny bit higher than 1.0

	return (fabs(fAngle)<=fArc*0.5f);
}

// Returns true if the target is within range of the power. Uses vTargetPos if pentTarget is NULL.
bool character_TargetInPowerRangeEx(Character *pchar, 
									Power *pPower, 
									PowerDef *pdef, 
									Entity *pentTarget,
									Vec3 vTargetPos,
									S32 *piFailureOut)
{
	if(pchar && pdef)
	{
		if(pdef->eType==kPowerType_Combo)
		{
			// Instead of doing this, we could enforce that the range of combo powers is set correctly [RMARR - 10/7/11]
			int i;
			for(i=0;i<eaSize(&pdef->ppCombos);i++)
			{
				PowerDef * pChildDef = GET_REF(pdef->ppCombos[i]->hPower);
				if (character_TargetInPowerRangeEx(pchar,pPower,pChildDef,pentTarget,vTargetPos,piFailureOut))
				{
					return true;
				}
			}
		}
		else
		{
			F32 fDist, fTotalRange;
			PowerAnimFX *pafx = GET_REF(pdef->hFX);

			if (pentTarget)
				fDist = entGetDistance(pchar->pEntParent, NULL, pentTarget, NULL, NULL);
			else
				fDist = entGetDistance(pchar->pEntParent, NULL, NULL, vTargetPos, NULL);

			// Check min range
			if(fDist < pdef->fRangeMin)
			{
				if(piFailureOut)
					*piFailureOut = kActivationFailureReason_TargetOutOfRangeMin;
				return false;
			}

			
			// Get the total range, adjusted for lunge distance.
			fTotalRange = power_GetRange(pPower, pdef) + ((pafx && pafx->pLunge && pafx->pLunge->eDirection != kPowerLungeDirection_Away) ? pafx->pLunge->fRange : 0);

			if(fDist > fTotalRange)
			{
				if(piFailureOut)
					*piFailureOut = kActivationFailureReason_TargetOutOfRange;
				return false;
			}

			return true;
		}
	}
	return false;
}

// Returns true if the target is within range of the power.
bool character_TargetInPowerRange(Character *pchar, 
								  Power *pPower, 
								  PowerDef *pdef, 
								  Entity *pentTarget,
								  WorldInteractionNode *pnodeTarget)
{
	Vec3 vNodeTarget;

	if (!pchar)
	{
		return false;
	}
	if (pentTarget)
	{
		zeroVec3(vNodeTarget); 
	}
	else if (pnodeTarget)
	{
#ifdef GAMECLIENT
		clientTarget_GetNearestPointForTargetNode(pchar->pEntParent, pnodeTarget, vNodeTarget);
#else
		character_FindNearestPointForObject(pchar, NULL, pnodeTarget, vNodeTarget, true);
#endif
	}
	else
	{
		return true;
	}	
	return character_TargetInPowerRangeEx(pchar, pPower, pdef, pentTarget, vNodeTarget, NULL);
}

// Finds valid targets for the Character, given the PowerDef and
//  an optional different Character for the TargetType, and optional
//  EntityRef to exclude.  Optionally only returns the closest.
static EntityRef** CharacterFindTargetsForPowerDef(int iPartitionIdx,
												   Character *pchar,
												   PowerDef *pdef,
												   Character *pcharTargetType,
												   EntityRef erExclude,
												   EntityRef erExclude2,
												   S32 bClosest)
{
	static EntityRef *s_perTargets = NULL;

	Vec3 vecSource;
	Entity *pentSource = pchar->pEntParent, *pentExclude = entFromEntityRef(iPartitionIdx, erExclude), *pentExclude2 = entFromEntityRef(iPartitionIdx, erExclude2);
	bool bLoS = (pdef->eTargetVisibilityMain == kTargetVisibility_LineOfSight);
	F32 fRangeSqr = SQR(pdef->fRange);	
	F32 fRangeMinSqr = SQR(pdef->fRangeMin);
	PowerTarget *ppowtarget = GET_REF(pdef->hTargetMain);

	if(!pcharTargetType)
		pcharTargetType = pchar;

	eaiClearFast(&s_perTargets);

	if(ppowtarget)
	{
		Entity *pentTarget;
		EntityIterator *piter = entGetIteratorAllTypes(iPartitionIdx,0,ENTITYFLAG_IGNORE|ENTITYFLAG_DONOTDRAW);
		F32 fClosestDistSqr = FLT_MAX;
		entGetCombatPosDir(pentSource,NULL,vecSource,NULL);
		while(NULL != (pentTarget=EntityIteratorGetNext(piter)))
		{
			F32 fDistSqr;
			Vec3 vecTarget,vecDelta;

			entGetCombatPosDir(pentTarget,NULL,vecTarget,NULL);
			subVec3(vecSource,vecTarget,vecDelta);
			fDistSqr = lengthVec3Squared(vecDelta);

			if(pentTarget!=pentExclude
				&& pentTarget!=pentExclude2
				&& pentTarget->pChar
				&& !(entGetFlagBits(pentTarget) & ENTITYFLAG_UNTARGETABLE)
				&& fDistSqr <= fRangeSqr
				&& fDistSqr >= fRangeMinSqr
				&& (!bClosest || fDistSqr < fClosestDistSqr)
				&& (!bLoS || combat_CheckLoS(iPartitionIdx,vecSource,vecTarget,pentSource,pentTarget,NULL,false,false,NULL))
				&& character_TargetMatchesPowerType(iPartitionIdx,pcharTargetType, pentTarget->pChar, ppowtarget)
				&& (!pdef->bAffectedRequiresPerceivance || character_CanPerceive(iPartitionIdx,pchar,pentTarget->pChar)))
			{
				if(bClosest)
				{
					eaiSet(&s_perTargets,entGetRef(pentTarget),0);
					fClosestDistSqr = fDistSqr;
				}
				else
				{
					eaiPush(&s_perTargets,entGetRef(pentTarget));
				}
			}
		}
		EntityIteratorRelease(piter);
	}

	return &s_perTargets;
}

// Finds a random valid target for the Character, given the PowerDef and
//  an optional different Character for the TargetType, and optional EntityRef
//  to exclude.
EntityRef character_FindRandomTargetForPowerDef(int iPartitionIdx,
												Character *pchar,
												PowerDef *pdef,
												Character *pcharTargetType,
												EntityRef erExclude)
{
	int s;
	EntityRef erReturn = 0;

	EntityRef **pperTargets = CharacterFindTargetsForPowerDef(iPartitionIdx,pchar,pdef,pcharTargetType,erExclude,0,false);

	s = eaiSize(pperTargets);
	if(s>0)
	{
		int i = randomIntRange(0,s-1);
		erReturn = (*pperTargets)[i];
	}
	return erReturn;
}

// Finds the closest valid target for the Character, given the PowerDef and
//  an optional different Character for the TargetType, and optional EntityRefs
//  to exclude.
EntityRef character_FindClosestTargetForPowerDef(int iPartitionIdx,
												 Character *pchar,
												 PowerDef *pdef,
												 Character *pcharTargetType,
												 EntityRef erExclude,
												 EntityRef erExclude2)
{
	EntityRef erReturn = 0;

	EntityRef **pperTargets = CharacterFindTargetsForPowerDef(iPartitionIdx,pchar,pdef,pcharTargetType,erExclude,erExclude2,true);

	if(eaiSize(pperTargets))
		erReturn = (*pperTargets)[0];

	return erReturn;
}

// Finds the Character's dual target, and ensure's it's valid, and if not returns the Character
EntityRef character_GetTargetDualOrSelfRef(int iPartitionIdx, Character *pchar)
{
	EntityRef erTargetDual = pchar->erTargetDual;
	if(erTargetDual && entFromEntityRef(iPartitionIdx, erTargetDual))
		return erTargetDual;
	return entGetRef(pchar->pEntParent);
}





//The client believes his target is hidden or missing
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void character_CheckTargetObject(Entity *e)
{
#ifdef GAMESERVER 
	Entity *eTarget = (e->pChar && IS_HANDLE_ACTIVE(e->pChar->currentTargetHandle)) ? im_FindCritterforObject(entGetPartitionIdx(e), REF_STRING_FROM_HANDLE(e->pChar->currentTargetHandle)) : NULL;

	entity_SetTarget(e,eTarget?entGetRef(eTarget):0);
#endif
}


static void CharacterSetTargetObjectKey(Character *p, const char *pchKey)
{
	REMOVE_HANDLE(p->currentTargetHandle);
	entity_SetDirtyBit(p->pEntParent, parse_Character, p, false);

	if(pchKey)
		SET_HANDLE_FROM_STRING(INTERACTION_DICTIONARY, pchKey, p->currentTargetHandle);
}

// Sets then entity's target to the entref.  If called on client it propagates to server.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_SetTarget(Entity *e, U32 er)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(e && e->pChar)
	{
		S32 bDual = false;
		Entity *eMount = entGetMount(e);
		int iPartitionIdx = entGetPartitionIdx(e);
		Entity *eTarget = entFromEntityRef(iPartitionIdx, er);

		if (eMount && eMount->pChar)
		{
			CharacterSetTargetObjectKey(eMount->pChar,NULL);
			eMount->pChar->currentTargetRef = (EntityRef)er;
			entity_SetDirtyBit(eMount, parse_Character, eMount->pChar, false);
			pmUpdateSelectedTarget(eMount,false);
		}
		
		// If we've got dual targeting, see if we're setting this to the dual target
		if(gConf.bTargetDual && entIsPlayer(e))
		{
			if(!(eTarget && character_TargetIsFoe(iPartitionIdx,e->pChar,eTarget->pChar)) && (eTarget || !e->pChar->currentTargetRef))
			{
				CharacterSetTargetObjectKey(e->pChar,NULL);
				e->pChar->erTargetDual = (EntityRef)er;
				bDual = true;
			}
		}

		// Set to the main target
		if(!bDual)
		{
			CharacterSetTargetObjectKey(e->pChar,NULL);
			e->pChar->currentTargetRef = (EntityRef)er;
		}

		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
		pmUpdateSelectedTarget(e,false);	

#ifdef GAMECLIENT
		clientTarget_ChangedClientTarget();
		ServerCmd_entity_SetTarget(er);
		playVoiceSetSoundForTarget(er, __FILE__);
#endif

#ifdef GAMESERVER
		if(entIsPlayer(e))
		{
			e->pChar->targetChangeID++;
			if(eTarget)
			{
				if(eTarget && (eTarget->pCritter || eTarget->pSaved))
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
					aiMessageProcessTarget(eTarget, e);
				}
			}
		}
#endif

	}
#endif
}

// Sets then entity's target to the entref.  If called on client it propagates to server.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_SetFocusTarget(Entity *e, U32 er)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(e && e->pChar)
	{
		int iPartitionIdx = entGetPartitionIdx(e);

		CharacterSetTargetObjectKey(e->pChar,NULL);
		e->pChar->erTargetFocus = (EntityRef)er;

		entity_SetDirtyBit(e, parse_Character, e->pChar, false);

#ifdef GAMECLIENT
		clientTarget_ChangedClientFocusTarget();
		ServerCmd_entity_SetFocusTarget(er);
		playVoiceSetSoundForTarget(er, __FILE__);
#endif
	}
#endif
}

// Clears the entity's focus target.  If called on client it propagates to server.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_ClearFocusTarget(Entity *e)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(e && e->pChar)
	{
		e->pChar->erTargetFocus = 0;

		entity_SetDirtyBit(e, parse_Character, e->pChar, false);

#ifdef GAMECLIENT
		clientTarget_ChangedClientFocusTarget();
		ServerCmd_entity_ClearFocusTarget();
#endif
	}
#endif
}

// Clears the entity's dual target.  If called on client it propagates to server.
// NOTE: This is only when you must clear the dual target without affecting the main target.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_ClearTargetDual(Entity *e)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(e && e->pChar)
	{
		e->pChar->erTargetDual = 0;

		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
		pmUpdateSelectedTarget(e,false);	

#ifdef GAMECLIENT
		clientTarget_ChangedClientTarget();
		ServerCmd_entity_ClearTargetDual();
#endif

#ifdef GAMESERVER
		if(entIsPlayer(e))
		{
			e->pChar->targetChangeID++;
		}
#endif

	}
#endif
}

// Sets then entity's target to the object key.  If called on client it propagates to server.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_SetTargetObject(Entity *e, const ACMD_SENTENCE pchKey)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(e && e->pChar)
	{
		Entity *eMount = entGetMount(e);
		if (eMount && eMount->pChar)
		{
			eMount->pChar->currentTargetRef = 0;
			entity_SetDirtyBit(eMount, parse_Character, eMount->pChar, false);
			pmUpdateSelectedTarget(eMount,false);
			CharacterSetTargetObjectKey(eMount->pChar,pchKey);
		}

		// For now, objects are always "foe" targets

		e->pChar->currentTargetRef = 0;
		entity_SetDirtyBit(e, parse_Character, e->pChar, false);
		pmUpdateSelectedTarget(e,false);
		CharacterSetTargetObjectKey(e->pChar,pchKey);

#ifdef GAMECLIENT
		clientTarget_ChangedClientTarget();
		ServerCmd_entity_SetTargetObject(pchKey);
#endif

#ifdef GAMESERVER
		if (entIsPlayer(e))
		{
			e->pChar->targetChangeID++;
		}		
#endif
	}
#endif
}

// Makes selected facing pretend the entity has no target.Shut  Used for offscreen ents.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void entity_FaceSelectedIgnoreTarget(Entity *e, bool bIgnore)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if(e && e->pChar && (bool)e->pChar->bFaceSelectedIgnoreTarget != bIgnore)
	{
		e->pChar->bFaceSelectedIgnoreTarget = bIgnore;

#ifdef GAMECLIENT
		ServerCmd_entity_FaceSelectedIgnoreTarget(bIgnore);
#endif

		pmUpdateSelectedTarget(e, false);
	}
#endif
}

// The entity matches the target of the erToAssist, if erToAssist is an entity with a target
void entity_AssistTarget(Entity *e, EntityRef erToAssist)
{
	if(e && e->pChar)
	{
		Entity *eToAssist = entFromEntityRef(entGetPartitionIdx(e), erToAssist);
		if(eToAssist && eToAssist->pChar)
		{
			if(eToAssist->pChar->currentTargetRef)
			{
				entity_SetTarget(e,eToAssist->pChar->currentTargetRef);
			}
			else if(IS_HANDLE_ACTIVE(eToAssist->pChar->currentTargetHandle))
			{
				entity_SetTargetObject(e,REF_STRING_FROM_HANDLE(eToAssist->pChar->currentTargetHandle));
			}
		}
	}
}


F32 character_FindNearestPointForTarget(int iPartitionIdx, Character *pChar, Vec3 vTargetOut)
{
	Vec3 vPos;

	zeroVec3(vTargetOut);

	if(!pChar)
		return 0.0f;

	entGetCombatPosDir(pChar->pEntParent,NULL,vPos,NULL);

	if(IS_HANDLE_ACTIVE(pChar->currentTargetHandle))
	{
		WorldInteractionNode *pTarget = GET_REF(pChar->currentTargetHandle);

		if(pTarget)
			return wlInterationNode_FindNearestPoint(vPos,pTarget,vTargetOut); 
	}
	else if(pChar->currentTargetRef)
	{
		Entity *eTarget = entFromEntityRef(iPartitionIdx, pChar->currentTargetRef);
		WorldInteractionNode *pTarget;

		if(eTarget && (pTarget = GET_REF(eTarget->hCreatorNode)))
			return wlInterationNode_FindNearestPoint(vPos,pTarget,vTargetOut); 
	}

	return 0.0f;
}

//if bAccurate is set then an extremely slow, brute force algorithm will be used 
//!!only use if you need an exact point on an object instead of an approximate, i.e. targeting
F32 character_FindNearestPointForObject(Character *pChar, 
										const Vec3 vSourcePos, 
										WorldInteractionNode *pTarget, 
										Vec3 vTargetOut, 
										bool bAccurate)
{
	Vec3 vPos;
	F32 fRet;

	if(!pTarget)
		return 0.0f;
	PERFINFO_AUTO_START_FUNC();

	zeroVec3(vTargetOut);

	if(pChar)
		entGetCombatPosDir(pChar->pEntParent,NULL,vPos,NULL);
	else
		copyVec3(vSourcePos,vPos);

	if ( bAccurate )
		fRet = wlInterationNode_FindNearestPoint(vPos,pTarget,vTargetOut); 
	else
		fRet = wlInterationNode_FindNearestPointFast(vPos,pTarget,vTargetOut);

	PERFINFO_AUTO_STOP();
	return fRet;
}

#include "AutoGen/Character_target_h_ast.c"
