/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerAnimFX.h"

#include "dynFxInfo.h"
#include "../../WorldLib/AutoGen/dynFxInfo_h_ast.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "ExpressionPrivate.h"	// For size of Expression structure for manual PowerFXParam parsetable
#include "estring.h"
#include "GameAccountDataCommon.h"
#include "HashFunctions.h"
#include "logging.h"
#include "MemoryPool.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TriCube/vec.h"
#include "WorldGrid.h"
#include "dynAnimGraphPub.h"
#include "allegiance.h" // included late because the stupid thing doesn't include all the headers it needs itself


#include "AttribModFragility.h"
#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterAttribs.h"
#include "Character_combat.h"
#include "Character_target.h"
#include "CombatConfig.h"
#include "Combat_DD.h"
#include "inventoryCommon.h"
#include "itemArt.h"
#include "itemCommon.h"
#include "PowerActivation.h"
#include "AutoGen/PowerActivation_h_ast.h"
#include "PowerApplication.h"
#include "AutoGen/PowerApplication_h_ast.h"
#include "PowerTree.h"
#include "dynAnimGraph.h"

#if GAMESERVER || GAMECLIENT
	#include "EntityMovementManager.h"
	#include "EntityMovementDead.h"
	#include "EntityMovementDefault.h"
	#include "EntityMovementDragon.h"
	#include "EntityMovementGrab.h"
	#include "AutoGen/EntityMovementGrab_h_ast.h"
	#include "EntityMovementTactical.h"
	#include "EntityMovementProjectile.h"
	#include "PowersMovement.h"
	#include "dynAnimInterface.h"
#endif

#if GAMESERVER
	#include "gslEntity.h"
	#define getTransportEnt(iPartitionIdx) gslGetTransportEnt(iPartitionIdx)
#else
	#include "GfxCamera.h"
	#define getTransportEnt(iPartitionIdx) NULL
#endif

#if GAMECLIENT
	#include "gclControlScheme.h"
	#include "ClientTargeting.h"
	#include "gclCombatAdvantage.h"
#endif

#include "AutoGen/PowerAnimFX_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define PAFX_HITFX_SHIFT 24
#define PAFX_PERSIST_STANCE_MASK 0x10000000
#define PAFX_INACTIVE_PERSIST_STANCE_MASK 0x20000000

// Uncomment this to enable some (expensive) logging to the "Movement" log file
#define PAFX_ENTLOG 0

// Empty stance
static U32 s_uiEmptyStanceID = 0;

// Empty persist stance
static U32 s_uiEmptyPersistStanceID = 0;

// Empty FramesBeforeHit
static S32 *s_piFramesBeforeHitZero = NULL;

// Column indices for structure USEDFIELD and SpeedPenalties
static S32 s_iUsedFieldsIndex = 0;
static S32 s_iSpeedPenaltyDuringChargeIndex = 0;
static S32 s_iSpeedPenaltyDuringActivateIndex = 0;

// StringPool pointers for some internally-generated bit names
static const char *s_pchBitActivateOutOfRange = NULL;
static const char *s_pchBitActivateNoTarget = NULL;

// Stances are set & cleared based on the movement of a lunge or lurch
static const char **eaLungeWords = NULL;
static const char **eaLurchWords = NULL;

DictionaryHandle g_hPowerAnimFXDict;

int g_bPowerFXNodePrint = 0; //Print information for PowerFXNode command

AUTO_CMD_INT(g_bPowerFXNodePrint, EnablePowerFXNodePrint) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE;

// Completely StructParam'd version of the parse table for PowerFXParam, used like parse_Expression_StructParam
ParseTable parse_PowerFXParam_StructParam[] =
{
	{ "PowerFXParam", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(PowerFXParam), 0, NULL, 0, NULL },
	{ "cpchParam",		TOK_STRUCTPARAM | TOK_POOL_STRING | TOK_STRING(PowerFXParam, cpchParam, 0), NULL },
	{ "Type",			TOK_STRUCTPARAM | TOK_AUTOINT(PowerFXParam, eType, 0), PowerFXParamTypeEnum },
	{ "expr",			TOK_STRUCTPARAM | TOK_OPTIONALSTRUCT(PowerFXParam, expr, parse_Expression_StructParam) },
	{ "\n",				TOK_END, 0 },
	{ "", 0, 0 }
};

static const Vec3 powerAnimImpactDirection[] = {
	{  0,  0,  0}, // PADID_Default
	{  0,  0,  1}, // PADID_Front_to_Back
	{  0,  0, -1}, // PADID_Back_to_Front
	{ -1,  0,  0}, // PADID_Right_to_Left
	{  1,  0,  0}, // PADID_Left_to_Right
	{  0, -1,  0}, // PADID_Up_to_Down
	{  0,  1,  0}, // PADID_Down_to_Up
};

// MemoryPool initialization
MP_DEFINE(PowerAnimFXRef);

AUTO_RUN;
void initPowerAnimFX(void)
{
	if(entIsServer())
	{
		MP_CREATE(PowerAnimFXRef,40);
	}
	else
	{
		MP_CREATE(PowerAnimFXRef,4);
	}

	ParserSetTableInfo(parse_PowerFXParam_StructParam, sizeof(PowerFXParam), "PowerFXParam_StructParam", NULL, __FILE__, false, true);
	ParserSetTableNotLegalForWriting(parse_PowerFXParam_StructParam, "You are trying to write something out with parse_Expression_StructParam. This is not legal. You need to set up your parent struct with a REDUNDANT_STRUCT. Talk to an appropriate programmer. Programmers: talk to Raoul or Alex or Ben Z");
}


// Returns a consistent ID number for powers that don't always have
//  usable activation ID numbers
U32 power_AnimFXID(Power *ppow)
{
	PowerDef *pdef = GET_REF(ppow->hDef);

	return powerdef_AnimFXID(pdef, ppow->uiID);
}

U32 powerref_AnimFXID(PowerRef *ppowRef)
{
	PowerDef *pdef = GET_REF(ppowRef->hdef);

	return powerdef_AnimFXID(pdef, ppowRef->uiID);
}

// if bGenerateIDForAnyPower is true, do not error if the power is not of the expected type
U32 powerdef_AnimFXID(PowerDef *pdef, U32 uiPowerID)
{
#define PMPOWERID_INNATE	0x10000000
#define PMPOWERID_PASSIVE	0x20000000
#define PMPOWERID_TOGGLE	0x30000000
#define PMPOWERID_CLICK		0x40000000

	// ID consists of the id of the power...
	U32 uiID = uiPowerID;
	
	// ... bitwise or'd with the above defines
	if (pdef)
	{
		switch(pdef->eType)
		{
		case kPowerType_Innate:
			uiID = uiID | PMPOWERID_INNATE;
			break;
		case kPowerType_Passive:
			uiID = uiID | PMPOWERID_PASSIVE;
			break;
		case kPowerType_Toggle:
			uiID = uiID | PMPOWERID_TOGGLE;
			break;
		case kPowerType_Click:
			if(pdef->bAutoAttackServer)
			{
				uiID = uiID | PMPOWERID_CLICK;
				break;
			}
			// Intentional fallthrough
		default:
			ErrorDetailsf("%s",pdef->pchName);
			Errorf("Jered REALLY needs to know: powerdef_AnimFXID: requested id of non-innate/passive/toggle power");
		}
	}

	return uiID;
}

static PowerAnimNodeSelectionType PowerAnimFX_GetNodeSelectionType(PowerAnimFX* pPowerAnimFX)
{
	if (!pPowerAnimFX || pPowerAnimFX->eNodeSelection == kPowerAnimNodeSelectionType_Default)
	{
		if (g_CombatConfig.bPowerAnimFXChooseNodesInRangeAndArc)
		{
			return kPowerAnimNodeSelectionType_RandomInRangeAndArc;
		}
		else
		{
			return kPowerAnimNodeSelectionType_Random;
		}
	}
	return pPowerAnimFX->eNodeSelection;
}

// Finds all the PCBones used by the Entity's Powers and pushes them into the earray
void entity_FindPowerFXBones(Entity *pent, const char ***pppchBones)
{
	if(pent->pChar)
	{
		Character *pchar = pent->pChar;

		// Look through the PowerTrees
		int i;
		for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
		{
			PowerTree *pTree = pchar->ppPowerTrees[i];
			int j;
			for(j=eaSize(&pTree->ppNodes)-1; j>=0; j--)
			{
				PTNode *pNode = pTree->ppNodes[j];
				int k;

				if ( pNode->bEscrow )
					continue;

				for(k=eaSize(&pNode->ppPowers)-1; k>=0; k--)
				{
					Power *ppow = pNode->ppPowers[k];
					PowerDef *pdef = GET_REF(ppow->hDef);
					PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
					if(pafx && pafx->cpchPCBoneName)
					{
						eaPushUnique(pppchBones,pafx->cpchPCBoneName);
					}
					else if(eaSize(&ppow->ppSubPowers))
					{
						int l;
						for(l=eaSize(&ppow->ppSubPowers)-1; l>=0; l--)
						{
							Power *ppowChild = ppow->ppSubPowers[l];
							pdef = GET_REF(ppowChild->hDef);
							pafx = pdef ? GET_REF(pdef->hFX) : NULL;
							if(pafx && pafx->cpchPCBoneName)
							{
								eaPushUnique(pppchBones,pafx->cpchPCBoneName);
							}
						}
					}
				}
			}
		}
	}
}



// Sets the character's root status due to powers
// TODO(JW): Optimize: Feels like these could be #defines
void character_PowerActivationRootStart(Character *pchar, 
										U8 uchID, PowerAnimFXType eType, U32 uiTimestamp,
										char *pchCause)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(pchar);
	pmIgnoreStart(pchar, pchar->pPowersMovement,uchID,eType,uiTimestamp,pchCause);

#if PAFX_ENTLOG	
	if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		entLog(LOG_MOVEMENT,pchar->pEntParent,"IGIN START","%d %d %d %s",uchID,eType,uiTimestamp,pchCause);
	}
#endif
#endif
}

void character_PowerActivationRootStop(Character *pchar, 
									   U8 uchID, PowerAnimFXType eType, U32 uiTimestamp)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(pchar);
	pmIgnoreStop(pchar, pchar->pPowersMovement,uchID,eType,uiTimestamp);

#if PAFX_ENTLOG	
	if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		entLog(LOG_MOVEMENT,pchar->pEntParent,"IGIN STOP","%d %d %d",uchID,eType,uiTimestamp);
	}
#endif
#endif
}

void character_PowerActivationRootCancel(Character *pchar, 
										 U8 uchID, PowerAnimFXType eType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(pchar);
	pmIgnoreCancel(pchar->pPowersMovement,uchID,eType);

#if PAFX_ENTLOG	
	if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		entLog(LOG_MOVEMENT,pchar->pEntParent,"IGIN CANCEL","%d %d (%d)",uchID,eType,pmTimestamp(0));
	}
#endif
#endif
}

// Sets the character's root status due to generic combat
void character_GenericRoot(Character *p, bool bRooted, U32 uiTimestamp)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(p);
	if(bRooted)
	{
		pmIgnoreStart(p, p->pPowersMovement,PMOVE_ROOT,kPowerAnimFXType_None,uiTimestamp,NULL);
		
		if (p->pEntParent->mm.mrTactical)
		{
			TacticalRequesterRollDef* pRollDef = mrRequesterDef_GetRollDefForEntity(p->pEntParent, NULL);
			if (pRollDef && pRollDef->bRollDisableDuringRootAttrib)
				mrTacticalNotifyPowersStart(p->pEntParent->mm.mrTactical, TACTICAL_ROOT_UID, TDF_ROLL, p->uiScheduledRootTime);
		}
	}
	else
	{
		pmIgnoreStop(p, p->pPowersMovement,PMOVE_ROOT,kPowerAnimFXType_None,uiTimestamp);
		
		if (p->pEntParent->mm.mrTactical)
		{
			TacticalRequesterRollDef* pRollDef = mrRequesterDef_GetRollDefForEntity(p->pEntParent, NULL);
			if (pRollDef && pRollDef->bRollDisableDuringRootAttrib)
				mrTacticalNotifyPowersStop(p->pEntParent->mm.mrTactical, TACTICAL_ROOT_UID, uiTimestamp);
		}
	}
#endif
}

// Sets the character's hold status due to generic combat
void character_GenericHold(Character *p, bool bHeld, U32 uiTimestamp)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(p);
	if(bHeld)
	{
		pmIgnoreStart(p, p->pPowersMovement,PMOVE_HOLD,kPowerAnimFXType_None,uiTimestamp,NULL);

		if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib || g_CombatConfig.tactical.bRollDisableDuringPowerDisableAttrib)
		{
			TacticalDisableFlags flags = 0;
			if (g_CombatConfig.tactical.bRollDisableDuringPowerDisableAttrib)
				flags |= TDF_ROLL;
			if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib)
				flags |= TDF_AIM;

			mrTacticalNotifyPowersStart(p->pEntParent->mm.mrTactical, TACTICAL_HELD_UID, flags, p->uiScheduledHoldTime);
		}
	}
	else
	{
		pmIgnoreStop(p, p->pPowersMovement,PMOVE_HOLD,kPowerAnimFXType_None,uiTimestamp);

		if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib || g_CombatConfig.tactical.bRollDisableDuringPowerDisableAttrib)
			mrTacticalNotifyPowersStop(p->pEntParent->mm.mrTactical, TACTICAL_HELD_UID, uiTimestamp);
	}
#endif
}

// Resets the character's movement state and errorfs with useful data
void character_MovementReset(Character *pchar)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(pchar);
	pmReset(pchar->pPowersMovement);
#endif
}

// TODO(JW): Optimize: It feels like these could be #defines
void character_FlashBitsOn(Character *pchar,
						   U32 uiID,
						   U32 uiSubID,
						   PowerAnimFXType eType,
						   EntityRef erSource,
						   const char **ppchBits,
						   U32 uiTime,
						   S32 bTrigger,
						   S32 bTriggerIsEntityID,
						   S32 bTriggerMultiHit,
						   S32 bNeverCancel)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (gConf.bNewAnimationSystem)
		return;
	if(!eaSize(&ppchBits))
		return;

	PM_CREATE_SAFE(pchar);
	pmBitsStartFlash(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,uiTime,ppchBits,bTrigger,bTriggerIsEntityID,bTriggerMultiHit,false,false,bNeverCancel,false,false);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		int i;
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d %d:",uiID,uiSubID,eType,erSource,uiTime);
		for(i=0; i<eaSize(&ppchBits); i++)
		{
			estrConcatf(&pchLog," %s",ppchBits[i]);
		}
		entLog(LOG_MOVEMENT,pchar->pEntParent,"FBIT START","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif
#endif
}

void character_SendAnimKeywordOrFlag(Character *pchar,
									 U32 uiID,
									 U32 uiSubID,
									 PowerAnimFXType eType,
									 EntityRef erSource,
									 const char* pchKeyword,
									 PowerActivation* pact,
									 U32 uiTime,
									 S32 bTrigger,
									 S32 bTriggerIsEntityID,
									 S32 bTriggerMultiHit,
									 S32 bIsKeyword,
									 S32 bAssumeOwnership,
									 S32 bForceDetailFlag)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	static const char** ppchBitsWrapper = NULL;
	if (!gConf.bNewAnimationSystem)
		return;
	if (!pchKeyword)
		return;
	if (pact && bIsKeyword)
	{
		if (pact->bStartedAnimGraph)
			return;
		pact->bStartedAnimGraph = true;
	}
	eaPush(&ppchBitsWrapper, pchKeyword);
	PM_CREATE_SAFE(pchar);
	pmBitsStartFlash(	pchar->pPowersMovement,
						uiID,
						uiSubID,
						eType,
						erSource,
						uiTime,
						ppchBitsWrapper,
						bTrigger,
						bTriggerIsEntityID,
						bTriggerMultiHit,
						bIsKeyword,
						!bIsKeyword,
						false,
						bAssumeOwnership,
						bForceDetailFlag);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		int i;
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d %d:",uiID,uiSubID,eType,erSource,uiTime);
		estrConcatf(&pchLog," %s",pchKeyword);
		if (bIsKeyword)
			entLog(LOG_MOVEMENT,pchar->pEntParent,"START ANIM","%s",pchLog);
		else
			entLog(LOG_MOVEMENT,pchar->pEntParent,"SENT ANIM FLAG","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif

	eaClear(&ppchBitsWrapper);
#endif
}

void character_StickyBitsOn(Character *pchar, 
							U32 uiID,
							U32 uiSubID,
							PowerAnimFXType eType,
							EntityRef erSource,
							const char **ppchBits, 
							U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (gConf.bNewAnimationSystem)
		return;
	if(!eaSize(&ppchBits))
		return;

	PM_CREATE_SAFE(pchar);
	pmBitsStartSticky(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,uiTime,ppchBits,false,false,false);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		int i;
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d %d:",uiID,uiSubID,eType,erSource,uiTime);
		for(i=0; i<eaSize(&ppchBits); i++)
		{
			estrConcatf(&pchLog," %s",ppchBits[i]);
		}
		entLog(LOG_MOVEMENT,pchar->pEntParent,"SBIT START","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif
#endif
}

void character_StickyBitsOff(Character *pchar, 
							 U32 uiID,
							 U32 uiSubID,
							 PowerAnimFXType eType,
							 EntityRef erSource,
							 const char **ppchBits, 
							 U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (gConf.bNewAnimationSystem)
		return;
	if(!eaSize(&ppchBits))
		return;

	PM_CREATE_SAFE(pchar);
	pmBitsStop(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,uiTime,false);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		int i;
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d %d:",uiID,uiSubID,eType,erSource,uiTime);
		for(i=0; i<eaSize(&ppchBits); i++)
		{
			estrConcatf(&pchLog," %s",ppchBits[i]);
		}
		entLog(LOG_MOVEMENT,pchar->pEntParent,"SBIT STOP","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif
#endif
}

void character_StanceWordOn(Character *pchar, 
							U32 uiID,
							U32 uiSubID,
							PowerAnimFXType eType,
							EntityRef erSource,
							const char **ppchStanceWord, 
							U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (!gConf.bNewAnimationSystem)
		return;
	if(eaSize(&ppchStanceWord) <= 0)
		return;

	PM_CREATE_SAFE(pchar);
	pmBitsStartSticky(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,uiTime,ppchStanceWord,false,false,false);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		int i;
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d %d:",uiID,uiSubID,eType,erSource,uiTime);
		for(i=0; i<eaSize(&ppchStanceWord); i++)
		{
			estrConcatf(&pchLog," %s",ppchStanceWord[i]);
		}
		entLog(LOG_MOVEMENT,pchar->pEntParent,"SBIT START","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif
#endif
}

void character_StanceWordOff(Character *pchar, 
							 U32 uiID,
							 U32 uiSubID,
							 PowerAnimFXType eType,
							 EntityRef erSource,
							 const char **ppchStanceWord, 
							 U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (!gConf.bNewAnimationSystem)
		return;
	if(eaSize(&ppchStanceWord) <= 0)
		return;

	PM_CREATE_SAFE(pchar);
	pmBitsStop(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,uiTime,false);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		int i;
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d %d:",uiID,uiSubID,eType,erSource,uiTime);
		for(i=0; i<eaSize(&ppchStanceWord); i++)
		{
			estrConcatf(&pchLog," %s",ppchStanceWord[i]);
		}
		entLog(LOG_MOVEMENT,pchar->pEntParent,"SBIT STOP","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif
#endif
}

// Cancels the given bits on the character
void character_BitsCancel(Character *pchar, 
						  U32 uiID,
						  U32 uiSubID,
						  PowerAnimFXType eType,
						  EntityRef erSource)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(pchar);
	pmBitsCancel(pchar->pPowersMovement,uiID,uiSubID,eType,erSource);

#if PAFX_ENTLOG	
	if(entIsServer() && entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
	{
		char *pchLog = NULL;
		estrPrintf(&pchLog,"%d %d %d %d",uiID,uiSubID,eType,erSource);
		entLog(LOG_MOVEMENT,pchar->pEntParent,"BIT CANCEL","%s",pchLog);
		estrDestroy(&pchLog);
	}
#endif
#endif
}

// helper function, given and entity and a target location, 
// returns a direction from the entity to the position with the pitch component clamped
static void powerAnim_GetPowerMoveDirectionFromTargetPos(	Entity *pEnt, 
															SA_PARAM_OP_VALID Entity *pTargetEnt,
															const Vec3 vTargetPos, 
															Vec3 vDirOut)
{
	Vec3 vEntPos;

	entGetPos(pEnt,vEntPos);
	
	if (pTargetEnt && pTargetEnt->pChar && pTargetEnt->pChar->bSpecialLargeMonster)
	{
		Vec3 vTargetCombatPos;
		entGetCombatPosDir(pTargetEnt, NULL, vTargetCombatPos, NULL);
		subVec3(vTargetCombatPos, vEntPos, vDirOut);
	}
	else
	{
		subVec3(vTargetPos, vEntPos, vDirOut);
	}

	vDirOut[1] = 0;
	
	// if we don't have a valid direction, use the current combat direction
	if (dotVec3XZ(vDirOut, vDirOut) < 0.00001f)
	{
		entGetCombatPosDir(pEnt, NULL, NULL, vDirOut);
		vDirOut[1] = 0;
	}
	
	normalVec3XZ(vDirOut);
}

// Movement

// Starts a turn-to-face movement
void character_MoveFaceStart(int iPartitionIdx,
							 Character *pchar,
							 PowerActivation *pact,
							 PowerAnimFXType eType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PowerDef *pdef = GET_REF(pact->hdef);
	PowerTarget *ppowTarget = pdef ? GET_REF(pdef->hTargetMain) : NULL;
	PowerAnimFX * pFX = GET_REF(pdef->hFX);
	bool bFaceActivateStickyOverride = (ppowTarget && ppowTarget->bFaceActivateSticky);
	bool bFXDisableFaceActivateOverride = (pFX && pFX->bDisableFaceActivate);
	bool bPreactivate = eType==kPowerAnimFXType_PreactivateSticky;

	// Don't face if you have it disabled
	if(!bFXDisableFaceActivateOverride && (!pchar->bDisableFaceActivate || bFaceActivateStickyOverride))
	{
		U32 uiTimeStop;
		U32 uiTimeStart = (!pFX || !pFX->pLunge) ? pact->uiTimestampActivate : pact->uiTimestampCurrented;
		F32 fDuration = 0.5f; // Default amount of time to spend facing the target
		Entity *e = entFromEntityRef(iPartitionIdx, pact->erTarget);
		bool bFaceActivateSticky = (g_CombatConfig.bFaceActivateSticky || bFaceActivateStickyOverride);

		// Optional sticky facing
		if(bFaceActivateSticky)
		{			
			if(pdef && pdef->eTracking==kTargetTracking_Full)
			{
				if (eType==kPowerAnimFXType_PreactivateSticky)
				{
					uiTimeStart = pact->uiTimestampActivate;
					fDuration = pdef->fTimePreactivate;

					// this should really be done elsewhere, earlier
					pact->uiTimestampActivate = pmTimestampFrom(pact->uiTimestampActivate, pdef->fTimePreactivate);
				}
				else
				{
					if((!pdef->fTimeCharge || eType==kPowerAnimFXType_ActivateSticky) && (pdef->uiPeriodsMax || !POWERTYPE_PERIODIC(pdef->eType)))
					{
						if (!pFX || !pFX->bFacingOnlyDuringBaseActivateTime)
						{	
							Power *ppow = character_ActGetPower(pchar, pact);
							F32 fSpeed = ppow ? character_GetSpeedPeriod(iPartitionIdx, pchar, ppow) : 1;
							fDuration = pdef->fTimeActivate + ((pdef->fTimeActivatePeriod * pdef->uiPeriodsMax) / fSpeed) + pdef->fTimePostMaintain;
						}
						else
						{	// non-default- we only want to face during the activate and not during the periods of the power.
							fDuration = pdef->fTimeActivate;
						}
					}
					else
						fDuration = -1;
				}
			}
		}

		uiTimeStop = (fDuration!=-1) ? pmTimestampFrom(uiTimeStart,fDuration) : 0;

		ANALYSIS_ASSUME(pchar->pEntParent);
				
		if(pFX && ((pFX->bLockFacingDuringActivate && !bPreactivate) || (pFX->bLockFacingDuringPreactivate && bPreactivate)))
		{
			PM_CREATE_SAFE(pchar); 
			
			pmFaceStart(pchar->pPowersMovement, 
						pact->uchID, 
						uiTimeStart, 
						uiTimeStop, 
						0, 
						NULL, 
						bFaceActivateSticky, 
						false);
		}
		else if(e && e!=pchar->pEntParent)
		{	// Don't face yourself, or someone that doesn't exist
			PM_CREATE_SAFE(pchar); 

			pmFaceStart(pchar->pPowersMovement, 
						pact->uchID, 
						uiTimeStart, 
						uiTimeStop, 
						pact->erTarget,
						pact->vecTarget, 
						bFaceActivateSticky, 
						false);

#ifdef GAMECLIENT
			clientTarget_ResetFaceTimeout();
#endif
		}
		else if( !ISZEROVEC3(pact->vecTarget) && // Face target point
				((!e || IS_HANDLE_ACTIVE(e->hCreatorNode)) || 
					((pchar->bUseCameraTargeting && (!ppowTarget || !ppowTarget->bDoNotTargetUnlessRequired)) 
						|| !character_PowerRequiresValidTarget(pchar, pdef))))
		{
			Vec3 vFaceDirection = {0};

			PM_CREATE_SAFE(pchar);
					
			// face the direction to the vecTarget now and lock in this direction instead of always trying to face the given point
			// note: this is a slight change to behavior but the previous' was undesired for our cases
			// the issues have been with lurch/lunges that move and can sometimes rotate the character in an undesired way
			powerAnim_GetPowerMoveDirectionFromTargetPos(pchar->pEntParent, NULL, pact->vecTarget, vFaceDirection);
			
			pmFaceStart(pchar->pPowersMovement, 
						pact->uchID, 
						uiTimeStart, 
						uiTimeStop, 
						pact->erProximityAssistTarget,
						vFaceDirection, 
						bFaceActivateSticky,
						true);
		}
		else if(!pdef->bHasTeleportAttrib && !pdef->bHasProjectileCreateAttrib && !ISZEROVEC3(pact->vecTargetSecondary)) //Face the secondary target
		{
			PM_CREATE_SAFE(pchar);

			pmFaceStart(pchar->pPowersMovement, 
						pact->uchID, 
						uiTimeStart, 
						uiTimeStop, 
						0,
						pact->vecTargetSecondary, 
						bFaceActivateSticky, 
						false);
		}

	}
#endif
}

// Stops a face movement (by sending a Face with the same ID with the given stop time)
void character_MoveFaceStop(Character *pchar,
							PowerActivation *pact,
							U32 uiTimeStop)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PowerDef *pdef = GET_REF(pact->hdef);
	PowerTarget *ppowTarget = pdef ? GET_REF(pdef->hTargetMain) : NULL;
	PowerAnimFX * pFX = pdef ? GET_REF(pdef->hFX) : NULL;
	bool bFaceActivateStickyOverride = (ppowTarget && ppowTarget->bFaceActivateSticky);
	bool bFXDisableFaceActivateOverride = (pFX && pFX->bDisableFaceActivate);
	
	if(!bFXDisableFaceActivateOverride && (!pchar->bDisableFaceActivate || bFaceActivateStickyOverride))
	{
		// Optional sticky facing
		if(g_CombatConfig.bFaceActivateSticky || bFaceActivateStickyOverride)
		{
			if(pdef && pdef->eTracking==kTargetTracking_Full)
			{
				PM_CREATE_SAFE(pchar); 
				pmFaceStart(pchar->pPowersMovement, 
							pact->uchID, 
							pact->uiTimestampActivate,
							uiTimeStop,
							pact->erTarget,
							pact->vecTarget,
							true,
							false);

			}
		}
	}
#endif
}

// Starts a lunging movement
void character_MoveLungeStart(Character *pchar,
							  PowerActivation *pact)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(pact->eLungeMode==kLungeMode_Pending)
	{
		PowerDef *pdef = GET_REF(pact->hdef);
		PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
		if(pafx && pafx->pLunge)
		{
			F32 fDistNotify = pafx->pLunge->fSpeed * (pafx->pLunge->iFramesOfActivate/PAFX_FPS);
			F32 fDistStop = 0.f;
			EntityRef erTarget = 0;
			Vec3 vTargetPos = {0};
			S32 bHorizontalLunge = false;
						
			if (pafx->pLunge->eDirection==kPowerLungeDirection_Target || pafx->pLunge->eDirection==kPowerLungeDirection_TargetChase) 
			{
				if (pdef->fRangeSecondary == 0.f)
					copyVec3(pact->vecTarget, vTargetPos);
				else 
					copyVec3(pact->vecTargetSecondary, vTargetPos);
			}
			else
			{ 
				copyVec3(pact->vecSourceLunge, vTargetPos);
			}

			if (pafx->pLunge->eDirection != kPowerLungeDirection_Down)
			{
				Power *ppow = character_FindPowerByRef(pchar,&pact->ref);
			
				if (pafx->pLunge->eDirection == kPowerLungeDirection_TargetChase) 
					erTarget = pact->erTarget;

				fDistStop = pafx->pLunge->fStopDistance;
				if (fDistStop < 0.f)
					fDistStop = g_CombatConfig.fLungeDefaultStopDistance;

				if (!pafx->pLunge->bUseRootDistance)
				{
					F32 fCapsuleRadius = entGetPrimaryCapsuleRadius(pchar->pEntParent);

					if (pact->erTarget != entGetRef(pchar->pEntParent))
					{
						Entity *eTarget = entFromEntityRef(entGetPartitionIdx(pchar->pEntParent), pact->erTarget);
						if (eTarget)
						{
							fCapsuleRadius += entGetPrimaryCapsuleRadius(eTarget);

							// see if the target has a special combat position. In this case we can only 
							// go to a position, and make the lunge a horizontal direction towards the combat postion 
							if (eTarget->pChar && eTarget->pChar->bSpecialLargeMonster)
							{
								Vec3 vCasterPos;
								erTarget = 0;
								entGetPos(pchar->pEntParent, vCasterPos);
								entGetCombatPosDir(eTarget, NULL, vTargetPos, NULL);
								vTargetPos[1] = vCasterPos[1];
								bHorizontalLunge = true;
							}
						}
					}

					fDistStop += fCapsuleRadius;
				}
			}

			PM_CREATE_SAFE(pchar);
			pmLungeStart(	pchar->pPowersMovement,
							pact->uchID, 
							pact->uiTimestampLungeMoveStart,
							pact->uiTimestampLungeMoveStop,
							fDistStop, 
							(pact->fLungeSpeed ? pact->fLungeSpeed : pafx->pLunge->fSpeed),
							pact->uiTimestampActivate,
							fDistNotify,
							erTarget,
							vTargetPos, 
							bHorizontalLunge,
							(pafx->pLunge->eDirection==kPowerLungeDirection_Away));

			character_StanceWordOn (pchar,pact->uchID,0,kPowerAnimFXType_MoveLungeSticky,entGetRef(pchar->pEntParent),eaLungeWords,pact->uiTimestampLungeMoveStart);
			character_StanceWordOff(pchar,pact->uchID,0,kPowerAnimFXType_MoveLungeSticky,entGetRef(pchar->pEntParent),eaLungeWords,pact->uiTimestampLungeMoveStop );
		}
	}
#endif
}

static int IsCharacterAirborne(Character *pchar)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
	return 0;
#else
	return ((pchar->pattrBasic->fFlight > 0) || !mrSurfaceGetOnGround(pchar->pEntParent->mm.mrSurface));
#endif
}

// Starts a sliding movement
void character_MoveLurchStart(int iPartitionIdx,
							  Character *pchar,
							  U8 uchID,
							  PowerActivation *pact,
							  PowerAnimFX *pafx,
							  EntityRef erTarget,
							  const Vec3 vecTarget,
							  U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	DynAnimGraph *pAnimGraph = GET_REF(pafx->lurch.hMovementGraph);
	S32 iPowerMovementFrameCount = 0;

	PM_CREATE_SAFE(pchar);
	if (((pAnimGraph && pAnimGraph->pPowerMovementInfo && 
		pAnimGraph->pPowerMovementInfo->pDefaultMovement && 
		(iPowerMovementFrameCount = eaSize(&pAnimGraph->pPowerMovementInfo->pDefaultMovement->eaFrameList)) > 0) || 
		pafx->lurch.iSlideFrameCount) &&
		!g_CombatConfig.lurch.bDisable && !(character_IsRooted(pchar) || character_IsHeld(pchar)))
	{
		bool bReverseLurch = false;
		Vec3 vDirection;
		F32 fSlideDistance = pafx->lurch.fSlideDistance;
		Entity *pTargetEnt = entFromEntityRef(iPartitionIdx,erTarget);
		F32 fAddedCapsuleRadius = g_CombatConfig.lurch.fAddedCapsuleRadius;
		F32 fMoveYawOffset = 0.f;
		
		// if there is a valid radius on the powerArt, use that one
		if (pafx->lurch.fLurchCapsuleBufferRadius > 0.f)
			fAddedCapsuleRadius = pafx->lurch.fLurchCapsuleBufferRadius;

		if (pAnimGraph == NULL && fSlideDistance < 0.f)
		{
			fSlideDistance = -fSlideDistance;
			bReverseLurch = true;
		}

		if((!erTarget || (pTargetEnt && IS_HANDLE_ACTIVE(pTargetEnt->hCreatorNode))) && vecTarget)
		{
			// Correction for lurches without an in-range target
			powerAnim_GetPowerMoveDirectionFromTargetPos(pchar->pEntParent, NULL, vecTarget, vDirection);

			if (bReverseLurch)
				negateVec3(vDirection, vDirection);
		}
		else
		{
			Vec3 vecSource;

			entGetPos(pchar->pEntParent,vecSource);

			if (pTargetEnt)
			{
				Vec3 vTargetEntPos;

				entGetPos(pTargetEnt,vTargetEntPos);

				if (!bReverseLurch)
				{
					F32 fDist;
					fDist = entGetDistanceXZ(pchar->pEntParent, NULL, pTargetEnt, NULL, NULL) - 3.f;
					if (fDist <= 0.00001f)
						return; // too close to lurch

					if (fSlideDistance > fDist)
						fSlideDistance = fDist;
				}

				powerAnim_GetPowerMoveDirectionFromTargetPos(pchar->pEntParent, pTargetEnt, vTargetEntPos, vDirection);

				if (pTargetEnt->pChar && pTargetEnt->pChar->bSpecialLargeMonster)
				{
					erTarget = 0;
				}
			}
			else
			{
				powerAnim_GetPowerMoveDirectionFromTargetPos(pchar->pEntParent, NULL, vecTarget, vDirection);
			}

			if (bReverseLurch)
				negateVec3(vDirection, vDirection);
		}

		if (pact && pafx->lurch.bLurchSlideInMovementDirection)
		{
			Vec3 vVec = {0};
			
			if (pact->eInputDirectionBits & kMovementInputBits_Back)
				vVec[2] += -1.f;
			
			if (pact->eInputDirectionBits & kMovementInputBits_Forward)
				vVec[2] += 1.f;

			if (pact->eInputDirectionBits & kMovementInputBits_Left)
				vVec[0] += 1.f;

			if (pact->eInputDirectionBits & kMovementInputBits_Right)
				vVec[0] -= 1.f;
			
			if (!vec3IsZero(vVec))
			{
				fMoveYawOffset = getVec3Yaw(vVec);
			}
		}

		if (pAnimGraph)
		{
			// Animation data based lurch
			DynPowerMovement *pPowerMovement = pAnimGraph->pPowerMovementInfo->pDefaultMovement;			
			U32 stops = pmTimestampFrom(uiTime, (pPowerMovement->eaFrameList[iPowerMovementFrameCount - 1]->iFrame + 1) / PAFX_FPS);

			pmLurchAnimStart(	pchar->pPowersMovement, 
								uchID,
								pAnimGraph->pcName,
								uiTime,
								stops, 
								fMoveYawOffset,
								erTarget,
								vDirection, 
								fAddedCapsuleRadius,
								g_CombatConfig.bFaceActivateSticky,
								pafx->lurch.bLurchIgnoreCollision);

			character_StanceWordOn (pchar,uchID,0,kPowerAnimFXType_MoveLurchSticky,entGetRef(pchar->pEntParent),eaLurchWords,uiTime);
			character_StanceWordOff(pchar,uchID,0,kPowerAnimFXType_MoveLurchSticky,entGetRef(pchar->pEntParent),eaLurchWords,stops);
		}
		else
		{
			// Parametric lurch
			U32 start = pmTimestampFrom(uiTime,pafx->lurch.iSlideFrameStart/PAFX_FPS);
			U32 stops = pmTimestampFrom(uiTime,(pafx->lurch.iSlideFrameStart+pafx->lurch.iSlideFrameCount)/PAFX_FPS);			
			F32 fMovementSpeed = pafx->lurch.fMovementSpeed;			

			if (!fMovementSpeed && pafx->lurch.iSlideFrameCount)
			{
				fMovementSpeed = fSlideDistance / (pafx->lurch.iSlideFrameCount/PAFX_FPS);
			}

			pmLurchStart(	pchar->pPowersMovement, 
							uchID, 
							start,
							stops,
							fSlideDistance,
							fMovementSpeed, 
							fMoveYawOffset,
							erTarget,
							vDirection,
							fAddedCapsuleRadius,
							g_CombatConfig.bFaceActivateSticky, 
							bReverseLurch,
							pafx->lurch.bLurchIgnoreCollision);

			character_StanceWordOn (pchar,uchID,0,kPowerAnimFXType_MoveLurchSticky,entGetRef(pchar->pEntParent),eaLurchWords,start);
			character_StanceWordOff(pchar,uchID,0,kPowerAnimFXType_MoveLurchSticky,entGetRef(pchar->pEntParent),eaLurchWords,stops);
		}
	}
#endif
}

// Cancels all unstarted movements with the given id (0 will cancel all movements)
void character_MoveCancel(Character *pchar, U32 uiID, PowerMoveType eType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PM_CREATE_SAFE(pchar);
	pmMoveCancel(pchar->pPowersMovement,uiID,eType);

	if (gConf.bNewAnimationSystem) {
		EntityRef er = entGetRef(pchar->pEntParent);
		character_BitsCancel(pchar,uiID,0,kPowerAnimFXType_MoveLungeSticky,er);
		character_BitsCancel(pchar,uiID,0,kPowerAnimFXType_MoveLurchSticky,er);
	}
#endif
}



#define PAFXSTATICDEFINE(name) static const char *s_pch##name = NULL; static int s_h##name = 0;
PAFXSTATICDEFINE(Source);
PAFXSTATICDEFINE(Target);
PAFXSTATICDEFINE(Activation);
PAFXSTATICDEFINE(Application);
PAFXSTATICDEFINE(Miss);
PAFXSTATICDEFINE(EquipSlot);

// Static data for PowerIcon CFX
static const char *s_pchPowerIcon = NULL;
static char *s_pchPowerIconParam = NULL;
static PowerFXParam *s_pPowerIconParam = NULL;

static const char *s_pchFlagAbort;
static const char *s_pchFlagHitPause;
static const char *s_pchFlagInterrupt;

AUTO_RUN;
void PowerAnimFX_InitStrings(void)
{
	s_pchSource = allocAddStaticString("Source");
	s_pchTarget = allocAddStaticString("Target");
	s_pchActivation  = allocAddStaticString("Activation");
	s_pchApplication = allocAddStaticString("Application");
	s_pchMiss = allocAddStaticString("Miss");
	s_pchEquipSlot = allocAddStaticString("EquipSlot");
	
	s_pchPowerIcon = allocAddStaticString("PowerIcon");

	s_pchFlagAbort     = allocAddStaticString("Abort");
	s_pchFlagHitPause  = allocAddStaticString("HitPause");
	s_pchFlagInterrupt = allocAddStaticString("Interrupt");

	eaPush(&eaLungeWords,allocAddString("Lunge"));

	eaPush(&eaLurchWords,allocAddString("Lurch"));
}

static ExprContext *ParamBlockContext(void)
{
	static ExprContext *s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable,"CEFuncsSelf");
		exprContextAddFuncsToTableByTag(stTable,"powerart");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext,stTable);

		exprContextSetAllowRuntimePartition(s_pContext);

		exprContextSetPointerVarPooledCached(s_pContext,s_pchSource,NULL,parse_Character,false,true,&s_hSource);
		exprContextSetPointerVarPooledCached(s_pContext,s_pchTarget,NULL,parse_Character,false,true,&s_hTarget);
		exprContextSetPointerVarPooledCached(s_pContext,s_pchActivation,NULL,parse_PowerActivation,false,true,&s_hActivation);
		exprContextSetPointerVarPooledCached(s_pContext,s_pchApplication,NULL,parse_PowerApplication,false,true,&s_hApplication);
		exprContextSetIntVarPooledCached(s_pContext,s_pchMiss,false,&s_hMiss);
		exprContextSetIntVarPooledCached(s_pContext,s_pchEquipSlot,false,&s_hEquipSlot);
	}

	return s_pContext;
}

// Creates a param based on the power's area effect type 
// Will create a DynParamBlock if it is not already valid
static void DynParamBlock_AddPowerScaleParams(	DynParamBlock **ppBlock,
												Power *pPower, 
												PowerDef *pPowerDef)
{
	static const char *s_pchPowerScaleParam = NULL;
	
	if (!s_pchPowerScaleParam)
		s_pchPowerScaleParam = allocAddString("powerScale");

	
	if (s_pchPowerScaleParam && pPower && pPowerDef)
	{
		Vec3 vScale = {0};
		
		switch (pPowerDef->eEffectArea)
		{
			xcase kEffectArea_Cylinder:
			{
				// fCachedAreaOfEffectExprValue should be the radius
				setVec3(vScale, pPower->fCachedAreaOfEffectExprValue, 
								pPower->fCachedAreaOfEffectExprValue, 
								power_GetRange(pPower, pPowerDef));
			}

			xcase kEffectArea_Cone:
			{
				F32 fPowerRange = power_GetRange(pPower, pPowerDef);
				// fCachedAreaOfEffectExprValue  should be the arc
				setVec3(vScale, fPowerRange, fPowerRange, fPowerRange);
			}

			xcase kEffectArea_Sphere:
			{
				// fCachedAreaOfEffectExprValue should be the radius
				setVec3(vScale, pPower->fCachedAreaOfEffectExprValue, 
								pPower->fCachedAreaOfEffectExprValue, 
								pPower->fCachedAreaOfEffectExprValue);
			}

		}
		
		if (!vec3IsZero(vScale))
		{
			DynDefineParam *pParam = StructAlloc(parse_DynDefineParam);

			if (!(*ppBlock))
			{
				(*ppBlock) = dynParamBlockCreate(); 
			}
			
			pParam->pcParamName = s_pchPowerScaleParam;
			MultiValSetVec3(&pParam->mvVal, &vScale);
			eaPush(&(*ppBlock)->eaDefineParams, pParam);
		}
		
	}
}

// Create a DynParamBlock for use by the character_FlashFX and character_StickyFX calls
static DynParamBlock *CreateParamBlock(	int iPartitionIdx, 
										Character *pSrc, 
										Character *pTarget, 
										PowerApplication *pApp, 
										PowerActivation *pAct, 
										PowerFXParam **ppParams,
										bool bMiss,
										U32 uiEquipSlot)
{
	static MultiVal *s_pMV = NULL;
	DynParamBlock *pBlock = NULL;
	
	if(eaSize(&ppParams))
	{
		int i;
		ExprContext *pContext = ParamBlockContext();

		if(!s_pMV)
		{
			s_pMV = MultiValCreate();
		}

		PERFINFO_AUTO_START_FUNC();

		exprContextSetSelfPtr(pContext,pSrc ? pSrc->pEntParent : NULL);
		exprContextSetPartition(pContext, iPartitionIdx);
		exprContextSetPointerVarPooledCached(pContext,s_pchSource,pSrc,parse_Character,false,true,&s_hSource);
		exprContextSetPointerVarPooledCached(pContext,s_pchTarget,pTarget,parse_Character,false,true,&s_hTarget);
		exprContextSetPointerVarPooledCached(pContext,s_pchActivation,pAct,parse_PowerActivation,false,true,&s_hActivation);
		exprContextSetPointerVarPooledCached(pContext,s_pchApplication,pApp,parse_PowerApplication,false,true,&s_hApplication);
		exprContextSetIntVarPooledCached(pContext,s_pchMiss,bMiss,&s_hMiss);
		exprContextSetIntVarPooledCached(pContext,s_pchEquipSlot,uiEquipSlot,&s_hEquipSlot);

		pBlock = dynParamBlockCreate();
		
		// Go through all the params, evaluate them, and if they work out, add them to the block
		for(i=eaSize(&ppParams)-1; i>=0; i--)
		{
			if(ppParams[i]==s_pPowerIconParam)
			{
				if(s_pchPowerIconParam && *s_pchPowerIconParam)
				{
					DynDefineParam *pParam = StructAlloc(parse_DynDefineParam);
					MultiValSetString(&pParam->mvVal,s_pchPowerIconParam);
					pParam->pcParamName = ppParams[i]->cpchParam;
					eaPush(&pBlock->eaDefineParams,pParam);
				}
				continue;
			}

			exprEvaluateDeepCopyAnswer(ppParams[i]->expr,pContext,s_pMV);
			
			// Early fixup/validation
			if(s_pMV->type==MULTI_INT)
			{
				// Convert Int to Float
				MultiValSetFloat(s_pMV,MultiValGetFloat(s_pMV,NULL));
			}
			else if(s_pMV->type==MULTI_STRING_F && (!s_pMV->str || !s_pMV->str[0]))
			{
				// String doesn't have valid data
				MultiValClear(s_pMV);
				continue;
			}
			
			if(s_pMV->type==ppParams[i]->eType)
			{
				DynDefineParam *pParam = StructAlloc(parse_DynDefineParam);
				if(s_pMV->type == MULTIOP_LOC_MAT4_F)
					MultiValSetVec3(&pParam->mvVal, &(s_pMV->vecptr[3]));
				else
					MultiValCopy(&pParam->mvVal,s_pMV);
				pParam->pcParamName = ppParams[i]->cpchParam;
				eaPush(&pBlock->eaDefineParams,pParam);
			}
			else if(s_pMV->type==kPowerFXParamType_VEC && ppParams[i]->eType==kPowerFXParamType_VC4)
			{
				// Automatically convert a Vec3 to a Vec4 with 0 w
				DynDefineParam *pParam = StructAlloc(parse_DynDefineParam);
				Vec4 v;
				copyVec3(s_pMV->vecptr[3],v);
				v[3] = 0;
				MultiValSetVec4(&pParam->mvVal, &v);
				pParam->pcParamName = ppParams[i]->cpchParam;
				eaPush(&pBlock->eaDefineParams,pParam);
			}
			MultiValClear(s_pMV);
		}


		PERFINFO_AUTO_STOP();
	}
	
	return pBlock;
}


// "powerart" expression functions and related support functions

// Returns the first part found in the costume that is attached to the given bone
static PCPart *GetCostumePartByBone(PlayerCostume *pCostume, PCBoneDef *pBone)
{
	int i;
	for(i=eaSize(&pCostume->eaParts)-1; i>=0; i--)
	{
		PCPart *pPart = pCostume->eaParts[i];
		if(GET_REF(pPart->hBoneDef)==pBone)
		{
			return pPart;
		}
	}
	return NULL;
}

// Returns the fx geo name associated with the costume part that is currently selected for the given bone
// Do not change the name of this function without reviewing GenerateAnimFXParam
AUTO_EXPR_FUNC(powerart);
char *GetCostumePartFxGeo(ExprContext* pContext, const char *bone, const char *defaultGeo)
{
	static char *s_pchCostumeGeo = NULL;
	Entity *pent = exprContextGetSelfPtr(pContext);
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);

	if(!s_pchCostumeGeo)
	{
		estrCreate(&s_pchCostumeGeo);
	}
	estrClear(&s_pchCostumeGeo);

	if(pact)
	{
		if(pact->pRefAnimFXMain)
		{
			PowerAnimFX *pafx = GET_REF(pact->pRefAnimFXMain->hFX);
			if(pafx && pafx->pCostumePartOverride && pafx->pCostumePartOverride->cpchFxGeo)
			{
				estrCopy2(&s_pchCostumeGeo,pafx->pCostumePartOverride->cpchFxGeo);
			}
		}
		
		if(!(*s_pchCostumeGeo) && eaSize(&pact->ppRefAnimFXEnh))
		{
			int i,s=eaSize(&pact->ppRefAnimFXEnh);
			for(i=0; i<s; i++)
			{
				PowerAnimFX *pafx = GET_REF(pact->ppRefAnimFXEnh[i]->hFX);
				if(pafx && pafx->pCostumePartOverride && pafx->pCostumePartOverride->cpchFxGeo)
				{
					estrCopy2(&s_pchCostumeGeo,pafx->pCostumePartOverride->cpchFxGeo);
					break;
				}
			}
		}
	}

	if(!(*s_pchCostumeGeo) && pent && bone)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pent);
		PCBoneDef *pBone = RefSystem_ReferentFromString(g_hCostumeBoneDict,bone);
		if(pCostume && pBone && pBone->bPowerFX)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)
			{
				PCGeometryDef *pGeoDef = GET_REF(pPart->hGeoDef);
				if(pGeoDef)
				{
					estrCopy2(&s_pchCostumeGeo,pGeoDef->pcModel);
				}
			}
		}
	}

	if(!(*s_pchCostumeGeo) && defaultGeo)
	{
		estrCopy2(&s_pchCostumeGeo,defaultGeo);
	}

	return s_pchCostumeGeo;
}

// Get the costume
static PCBoneDef *GetBoneWithGender(Entity *pent, const char *boneName)
{
	PCBoneDef *pBone = NULL;
	if(boneName)
	{
		pBone = RefSystem_ReferentFromString(g_hCostumeBoneDict,boneName);
		if(!pBone)
		{
			// try again with gender prefix
			S32 i;
			for(i = 0; i < eaSize(&g_CostumeConfig.eaCostumeGenderPrefixes); ++i)
			{
				if(g_CostumeConfig.eaCostumeGenderPrefixes[i]->eGender == pent->eGender && g_CostumeConfig.eaCostumeGenderPrefixes[i]->pcBonePrefix)
				{
					static char *eBoneName = NULL;	// reuse this on next call, loop etc

					estrPrintf(&eBoneName, "%s%s", g_CostumeConfig.eaCostumeGenderPrefixes[i]->pcBonePrefix, boneName);
					pBone = RefSystem_ReferentFromString(g_hCostumeBoneDict, eBoneName);
					break;

				}
			}
		}
	}
	
	return pBone;
}

static bool GetActualCostumePartColor(Entity *pent, ACMD_EXPR_LOC_MAT4_OUT matOut, const char *bone, int index, bool bTryGender, bool bPreferClothLayer)
{
	S32 bSet = false;

	if(!bSet && pent && bone)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pent);
		PCBoneDef *pBone;

		if(bTryGender)
		{
			// try again with gender prefix
			pBone = GetBoneWithGender(pent, bone);
		}
		else
		{
			pBone = RefSystem_ReferentFromString(g_hCostumeBoneDict,bone);
		}

		if(pCostume && pBone)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)	// ignore color linking as its an editor only field
			{
				PCPart *pUsePart = (bPreferClothLayer && pPart->pClothLayer) ? pPart->pClothLayer : pPart;
				// Color on costume is rgb color required for the vec4 out is rgb
				switch(index)
				{
				case 1:
					setVec3(matOut[3],pUsePart->color1[0], pUsePart->color1[1], pUsePart->color1[2]);
					break;
				case 2:
					setVec3(matOut[3],pUsePart->color2[0], pUsePart->color2[1], pUsePart->color2[2]);
					break;
				case 3:
					setVec3(matOut[3],pUsePart->color3[0], pUsePart->color3[1], pUsePart->color3[2]);
					break;
				default:
					setVec3(matOut[3],pUsePart->color0[0], pUsePart->color0[1], pUsePart->color0[2]);
				}
				bSet = true;
			}
		}
	}

	return bSet;
}


// Returns the index-th Vec3 color associated with the costume part that is currently selected for the given bone
//  Needs to switch to Vec4 once we have support for that.
// Do not change the name of this function without reviewing GenerateAnimFXParam
AUTO_EXPR_FUNC(powerart);
void GetCostumePartColor(ExprContext* pContext, ACMD_EXPR_LOC_MAT4_OUT matOut, const char *bone, int index, F32 defaultR, F32 defaultG, F32 defaultB, F32 defaultA)
{
	S32 bSet = false;
	Entity *pent = exprContextGetSelfPtr(pContext);
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);

	if(pact)
	{
		if(pact->pRefAnimFXMain)
		{
			PowerAnimFX *pafx = GET_REF(pact->pRefAnimFXMain->hFX);
			F32 *pfVec = NULL;
			if(pafx && pafx->pCostumePartOverride)
			{
				switch(index)
				{
				case 1:
					pfVec = pafx->pCostumePartOverride->vecColor1;
					break;
				case 2:
					pfVec = pafx->pCostumePartOverride->vecColor2;
					break;
				case 3:
					pfVec = pafx->pCostumePartOverride->vecColor3;
					break;
				default:
					pfVec = pafx->pCostumePartOverride->vecColor;
				}
			}

			if(pfVec && !ISZEROVEC4(pfVec))
			{
				copyVec3(pfVec,matOut[3]);
				bSet = true;
			}
		}

		if(!bSet && eaSize(&pact->ppRefAnimFXEnh))
		{
			int i,s=eaSize(&pact->ppRefAnimFXEnh);
			for(i=0; i<s; i++)
			{
				PowerAnimFX *pafx = GET_REF(pact->ppRefAnimFXEnh[i]->hFX);
				F32 *pfVec = NULL;
				if(pafx && pafx->pCostumePartOverride)
				{
					switch(index)
					{
					case 1:
						pfVec = pafx->pCostumePartOverride->vecColor1;
						break;
					case 2:
						pfVec = pafx->pCostumePartOverride->vecColor2;
						break;
					case 3:
						pfVec = pafx->pCostumePartOverride->vecColor3;
						break;
					default:
						pfVec = pafx->pCostumePartOverride->vecColor;
					}
				}

				if(pfVec && !ISZEROVEC4(pfVec))
				{
					copyVec3(pfVec,matOut[3]);
					bSet = true;
					break;
				}
			}
		}
	}

	if(!bSet && pent && bone)
	{
		bSet = GetActualCostumePartColor(pent, matOut, bone, index, false, 0);
	}

	if(!bSet)
	{
		setVec3(matOut[3],defaultR,defaultG,defaultB);
	}
}

// Returns the index-th Vec3 color associated with the costume part that is currently selected for the given bone
// This only checks the actual costume part, not any power effects 
// This does not record th bone index in GenerateAnimFXParam. Use only when the bone index is not required
AUTO_EXPR_FUNC(powerart);
void UseCostumePartColor(ExprContext* pContext, ACMD_EXPR_LOC_MAT4_OUT matOut, const char *bone, int index, F32 defaultR, F32 defaultG, F32 defaultB, F32 defaultA, bool bTryGender)
{
	S32 bSet = false;
	Entity *pent = exprContextGetSelfPtr(pContext);

	if(pent && bone)
	{
		bSet = GetActualCostumePartColor(pent, matOut, bone, index, bTryGender, 0);
	}

	if(!bSet)
	{
		setVec3(matOut[3],defaultR,defaultG,defaultB);
	}
}

//Slight modification to UseCostumePartColor that grabs from the back side for use with capes
AUTO_EXPR_FUNC(powerart);
void UseCostumePartColorFromInsideProperties(ExprContext* pContext, ACMD_EXPR_LOC_MAT4_OUT matOut, const char *bone, int index, F32 defaultR, F32 defaultG, F32 defaultB, F32 defaultA, bool bTryGender)
{
	S32 bSet = false;
	Entity *pent = exprContextGetSelfPtr(pContext);

	if(pent && bone)
	{
		bSet = GetActualCostumePartColor(pent, matOut, bone, index, bTryGender, 1);
	}

	if(!bSet)
	{
		setVec3(matOut[3],defaultR,defaultG,defaultB);
	}
}

static PCTextureDef * PowerArtGetTexture(ExprContext* pContext, const char *bone, const char *pcPowerAnimCostumeTexture, bool bPreferClothLayer)
{
	Entity *pEntity = exprContextGetSelfPtr(pContext);
	PCTextureDef *pTextDef = NULL;
	if(pEntity && pcPowerAnimCostumeTexture)
	{
		PowerAnimCostumeTexture eType = StaticDefineIntGetInt(PowerAnimCostumeTextureEnum, pcPowerAnimCostumeTexture);
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pEntity);
		PCBoneDef *pBone = GetBoneWithGender(pEntity, bone);

		if(eType <= kPowerAnimCostumeTexture_None)
		{
			Errorf("PowerArtGetCostumePartTextureName called with invalid texture type %s.", pcPowerAnimCostumeTexture);
		}

		if(pCostume && pBone)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)
			{
				PCPart *pUsePart = (bPreferClothLayer && pPart->pClothLayer) ? pPart->pClothLayer : pPart;
				switch(eType)
				{
				case kPowerAnimCostumeTexture_DetailTexture:
					{
						pTextDef = GET_REF(pUsePart->hDetailTexture);
						break;
					}
				case kPowerAnimCostumeTexture_PatternTexture:
					{
						pTextDef = GET_REF(pUsePart->hPatternTexture);
						break;
					}
				case kPowerAnimCostumeTexture_DiffuseTexture:
					{
						pTextDef = GET_REF(pUsePart->hDiffuseTexture);
						break;
					}
				case kPowerAnimCostumeTexture_SpecularTexture:
					{
						pTextDef = GET_REF(pUsePart->hSpecularTexture);
						break;
					}
				}
			}
		}
	}

	return pTextDef;

}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartTextureName(ExprContext* pContext, const char *bone, const char *pcPowerAnimCostumeTexture, U32 bNew)
{
	PCTextureDef *pTextDef = PowerArtGetTexture(pContext, bone, pcPowerAnimCostumeTexture, 0);
	if(pTextDef)
	{
		if (bNew) {
			return pTextDef->pcNewTexture;
		} else {
			return pTextDef->pcOrigTexture;
		}
	}

	return "";
}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartTextureNameFromInsideProperties(ExprContext* pContext, const char *bone, const char *pcPowerAnimCostumeTexture, U32 bNew)
{
	PCTextureDef *pTextDef = PowerArtGetTexture(pContext, bone, pcPowerAnimCostumeTexture, 1);
	if(pTextDef)
	{
		if (bNew) {
			return pTextDef->pcNewTexture;
		} else {
			return pTextDef->pcOrigTexture;
		}
	}

	return "";
}

// find extra swap new texture that has last characters matching passed in value, otherwise return default
AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartTextureNameExtra(ExprContext* pContext, const char *bone, const char *pcPowerAnimCostumeTexture,const char *pcExtraExtension,const char *pcDefaultName, U32 bNew)
{
	PCTextureDef *pTextDef = PowerArtGetTexture(pContext, bone, pcPowerAnimCostumeTexture, 0);
	if(pTextDef && pcExtraExtension)
	{
		S32 i;
		S32 len = (S32)strlen(pcExtraExtension);
		if(len > 0)
		{
			for(i = 0; i < eaSize(&pTextDef->eaExtraSwaps); ++i)
			{
				if(pTextDef->eaExtraSwaps[i]->pcNewTexture)
				{
					S32 lenName = (S32)strlen(pTextDef->eaExtraSwaps[i]->pcNewTexture);
					S32 j;

					if(lenName > len)
					{
						S32 k = len - 1;
						for(j = lenName-1; k >= 0; --j,--k)
						{
							if(pTextDef->eaExtraSwaps[i]->pcNewTexture[j] != pcExtraExtension[k])
							{
								// not the same
								break;
							}
							else if(k == 0)
							{
								// end matches
								if (bNew) {
									return pTextDef->eaExtraSwaps[i]->pcNewTexture;
								} else {
									return pTextDef->eaExtraSwaps[i]->pcOrigTexture;
								}
							}
						}
					}
				}
			}
		}
	}

	return pcDefaultName;
}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartTextureNameExtraFromInsideProperties(ExprContext* pContext, const char *bone, const char *pcPowerAnimCostumeTexture,const char *pcExtraExtension,const char *pcDefaultName, U32 bNew)
{
	PCTextureDef *pTextDef = PowerArtGetTexture(pContext, bone, pcPowerAnimCostumeTexture, 1);
	if(pTextDef && pcExtraExtension)
	{
		S32 i;
		S32 len = (S32)strlen(pcExtraExtension);
		if(len > 0)
		{
			for(i = 0; i < eaSize(&pTextDef->eaExtraSwaps); ++i)
			{
				if(pTextDef->eaExtraSwaps[i]->pcNewTexture)
				{
					S32 lenName = (S32)strlen(pTextDef->eaExtraSwaps[i]->pcNewTexture);
					S32 j;

					if(lenName > len)
					{
						S32 k = len - 1;
						for(j = lenName-1; k >= 0; --j,--k)
						{
							if(pTextDef->eaExtraSwaps[i]->pcNewTexture[j] != pcExtraExtension[k])
							{
								// not the same
								break;
							}
							else if(k == 0)
							{
								// end matches
								if (bNew) {
									return pTextDef->eaExtraSwaps[i]->pcNewTexture;
								} else {
									return pTextDef->eaExtraSwaps[i]->pcOrigTexture;
								}
							}
						}
					}
				}
			}
		}
	}

	return pcDefaultName;
}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartGeoName(ExprContext* pContext, const char *bone)
{
	Entity *pEntity = exprContextGetSelfPtr(pContext);
	if(pEntity)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pEntity);
		PCBoneDef *pBone = GetBoneWithGender(pEntity, bone);

		if(pCostume && pBone)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)
			{
				PCGeometryDef *pGeoDef = GET_REF(pPart->hGeoDef);
				if(pGeoDef)
				{
					return pGeoDef->pcName;
				}
			}
		}
	}

	return "";
}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartMatName(ExprContext* pContext, const char *bone)
{
	Entity *pEntity = exprContextGetSelfPtr(pContext);
	if(pEntity)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pEntity);
		PCBoneDef *pBone = GetBoneWithGender(pEntity, bone);

		if(pCostume && pBone)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)
			{
				PCMaterialDef *pMatDef = GET_REF(pPart->hMatDef);
				if(pMatDef)
				{
					return pMatDef->pcMaterial;
				}
			}
		}
	}

	return "";
}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetCostumePartMatNameFromInsideProperties(ExprContext* pContext, const char *bone)
{
	Entity *pEntity = exprContextGetSelfPtr(pContext);
	if(pEntity)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pEntity);
		PCBoneDef *pBone = GetBoneWithGender(pEntity, bone);

		if(pCostume && pBone)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)
			{
				PCPart *pUsePart = pPart->pClothLayer ? pPart->pClothLayer : pPart;
				PCMaterialDef *pMatDef = GET_REF(pUsePart->hMatDef);
				if(pMatDef)
				{
					return pMatDef->pcMaterial;
				}
			}
		}
	}

	return "";
}

AUTO_EXPR_FUNC(powerart);
const char *PowerArtGetMatNameWithExt(const char *pcMatName, const char *pcExtension)
{
	if(pcMatName && pcExtension)
	{
		char *estrName = NULL;
		Material *pMaterial;

		estrPrintf(&estrName, "%s%s", pcMatName, pcExtension);
		if(materialExists(estrName))
		{
			pMaterial = materialFind(estrName, 0);
			return pMaterial->material_name;
		}
		else
		{
			Errorf("Power Art error: Couldn't find material %s",estrName);
		}

		estrDestroy(&estrName);
	}
	else
	{
		if(pcMatName)
		{
			Errorf("Power Art error: No extension for %s",pcMatName);
		}
		else
		{
			Errorf("Power Art error: No mat name.");
		}
	}

	return "";
}


// Returns 1 if the the costume part that is currently selected for the given bone has a subskeleton
//  This is basically a stripped-down version of GetCostumePartFxGeo that doesn't support
//  CostumePartOverrides and does not require the bone be tagged with bPowerFX.  It also doesn't
//  add the bone requested to the list of bones the powerart uses, which thus avoids adding the
//  bone to the list of customizable bones the player has due to owned powers.
// Do not change the name of this function without reviewing GenerateAnimFXParam
AUTO_EXPR_FUNC(powerart);
S32 HasCostumePartWithSubSkeleton(ExprContext* pContext, const char *bone)
{
	S32 ret = 0;
	Entity *pent = exprContextGetSelfPtr(pContext);

	if(pent && bone)
	{
		PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pent);
		PCBoneDef *pBone = RefSystem_ReferentFromString(g_hCostumeBoneDict,bone);
		if(pCostume && pBone)
		{
			PCPart *pPart = GetCostumePartByBone(pCostume,pBone);
			if(pPart)
			{
				PCGeometryDef *pGeoDef = GET_REF(pPart->hGeoDef);
				if(pGeoDef && pGeoDef->pOptions && pGeoDef->pOptions->pcSubSkeleton)
				{
					ret = 1;
				}
			}
		}
	}

	return ret;
}

// Returns the fx geo name associated with the item being used for the activation
AUTO_EXPR_FUNC(powerart);
char *GetItemFxGeo(ExprContext* pContext, const char *defaultGeo)
{
	static char *s_pchGeo = NULL;
	Entity *pent = exprContextGetSelfPtr(pContext);
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);

	if(!s_pchGeo)
	{
		estrCreate(&s_pchGeo);
	}
	estrClear(&s_pchGeo);

	if(pent && pent->pChar && pact)
	{
		PowerDef *pdef = GET_REF(pact->ref.hdef);
		if(pdef && pdef->bWeaponBased)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
			Item *pitem = character_DDWeaponPickSlot(pent->pChar, pdef, pExtract, 0);
			if(pitem)
			{
				ItemDef *pitemdef = GET_REF(pitem->hItem);
				if(pitemdef)
				{
					ItemArt *pitemart = GET_REF(pitemdef->hArt);
					if(pitemart)
					{
						estrCopy2(&s_pchGeo,pitemart->pchGeo);
					}
				}
			}
		}
	}

	if(!(*s_pchGeo) && defaultGeo)
	{
		estrCopy2(&s_pchGeo,defaultGeo);
	}

	return s_pchGeo;
}

// Returns the fx geo name associated with the item being used for the activation
AUTO_EXPR_FUNC(powerart);
F32 GetItemFxOptionalParamForSlot(ExprContext* pContext, int slot)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);

	if(pent && pent->pChar && pact)
	{
		PowerDef *pdef = GET_REF(pact->ref.hdef);
		if(pdef && pdef->bWeaponBased)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
			Item *pitem = character_DDWeaponPickSlot(pent->pChar, pdef, pExtract, slot);
			if(pitem)
			{
				ItemDef *pitemdef = GET_REF(pitem->hItem);
				if(pitemdef)
				{
					ItemArt *pitemart = GET_REF(pitemdef->hArt);
					if(pitemart)
					{
						return pitemart->fOptionalParam;
					}
				}
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(powerart);
const char *GetItemFxGeoForSlot(ExprContext* pContext, int slot)
{
	const char* pchGeo = NULL;
	Entity *pent = exprContextGetSelfPtr(pContext);
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);

	if(pent && pent->pChar && pact)
	{
		PowerDef *pdef = GET_REF(pact->ref.hdef);
		if(pdef && pdef->bWeaponBased)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pent);
			Item *pitem = character_DDWeaponPickSlot(pent->pChar, pdef, pExtract, slot);
			if(pitem)
			{
				ItemDef *pitemdef = GET_REF(pitem->hItem);
				if(pitemdef)
				{
					ItemArt *pitemart = GET_REF(pitemdef->hArt);
					if(pitemart)
					{
						pchGeo = slot > 0 ? pitemart->pchGeoSecondary : pitemart->pchGeo;
					}
				}
			}
		}
	}
	return pchGeo;
}

// Returns the geo name associated with an ItemArt in its bag's first slot
//  No support for active/inactive, bag, or default geo
AUTO_EXPR_FUNC(powerart);
const char* ItemArtGeoPrimary(ExprContext *pContext)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pEquippedArt)
	{
		int i;
		for(i=eaSize(&pent->pEquippedArt->ppState)-1; i>=0; i--)
		{
			if(pent->pEquippedArt->ppState[i]->iSlot==0)
			{
				ItemArt *pItemArt = GET_REF(pent->pEquippedArt->ppState[i]->hItemArt);
				if(pItemArt)
				{
					return pItemArt->pchGeo;
				}
				break;
			}
		}
	}
	return "";
}

// Returns the geo name associated with an ItemArt in its bag's second slot
//  No support for active/inactive, bag, or default geo
AUTO_EXPR_FUNC(powerart);
const char* ItemArtGeoSecondary(ExprContext *pContext)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	if(pent && pent->pEquippedArt)
	{
		int i;
		for(i=eaSize(&pent->pEquippedArt->ppState)-1; i>=0; i--)
		{
			if(pent->pEquippedArt->ppState[i]->iSlot==1)
			{
				ItemArt *pItemArt = GET_REF(pent->pEquippedArt->ppState[i]->hItemArt);
				if(pItemArt)
				{
					return pItemArt->pchGeo;
				}
				break;
			}
		}
	}
	return "";
}

static void PowerFXNode_AppendFrontBackString(Character *pSource, Character *pTarget, const char* pchFront, const char* pchBack, char **estrBuffer)
{
	Vec3 vTargetPos;
	Vec3 vSourcePos;
	Vec3 vForwardDir;
	Vec3 vTargetDir;
	Quat qDir;
	F32 fDot;

	entGetPos(pSource->pEntParent,vSourcePos);
	entGetRot(pSource->pEntParent,qDir);

	quatRotateVec3Inline(qDir,forwardvec,vForwardDir);

	entGetPos(pTarget->pEntParent,vTargetPos);

	subVec3(vTargetPos,vSourcePos,vTargetDir);

	fDot = dotVec3(vTargetDir,vForwardDir);

	if(fDot >= 0)
	{
		estrAppend2(estrBuffer,pchFront);
	}
	else
	{
		estrAppend2(estrBuffer,pchBack);
	}
}

static void PowerFXNode_AddHorizontalSuffix(Character *pSource, Character *pTarget, char **estrBuffer)
{
	Vec3 vTargetPos;
	Vec3 vSourcePos;
	Vec3 vSideDir;
	Quat qDir;
	F32 fDot;

	entGetPos(pSource->pEntParent,vSourcePos);
	entGetRot(pSource->pEntParent,qDir);

	quatRotateVec3Inline(qDir,sidevec,vSideDir);

	entGetPos(pTarget->pEntParent,vTargetPos);

	subVec3(vSourcePos,vTargetPos,vTargetPos);

	fDot = dotVec3(vTargetPos,vSideDir);

	if(fDot >= 0)
	{
		estrAppend2(estrBuffer,"L");
	}
	else
	{
		estrAppend2(estrBuffer,"R");
	}
}

static void PowerFXNode_AddVerticalSuffix(Character *pSource, Character *pTarget, char **estrBuffer)
{
	Vec3 vTargetPos;
	Vec3 vSourcePos;
	Vec3 vUpDir;
	Quat qDir;
	F32 fDot;

	entGetPos(pSource->pEntParent,vSourcePos);
	entGetRot(pSource->pEntParent,qDir);

	quatRotateVec3Inline(qDir,upvec,vUpDir);

	entGetPos(pTarget->pEntParent,vTargetPos);

	subVec3(vSourcePos,vTargetPos,vTargetPos);

	fDot = dotVec3(vTargetPos,vUpDir);

	if(fDot >= 0)
	{
		estrAppend2(estrBuffer,"B");
	}
	else
	{
		estrAppend2(estrBuffer,"T");
	}
}

static bool character_GetPowerFXNodeNameFromItemID(Character* pChar, 
												   S64 iItemID, 
												   char** pestrBuffer, 
												   int* piItemIdx)
{
	int i;
	
	if(!SAFE_MEMBER2(pChar,pEntParent,pInventoryV2))
		return false;

	for (i = 0; i < eaSize(&pChar->pEntParent->pInventoryV2->ppInventoryBags); i++)
	{
		int iItemIdx;
		InventoryBag* pBag = pChar->pEntParent->pInventoryV2->ppInventoryBags[i];
		const InvBagDef *pBagDef = invbag_def(pBag);

		if(!pBag)
			continue;

		for (iItemIdx = 0; iItemIdx < eaSize(&pBag->ppIndexedInventorySlots); iItemIdx++)
		{
			InventorySlot* pSlot = pBag->ppIndexedInventorySlots[iItemIdx];
			if(pSlot->pItem && pSlot->pItem->id == (U64)iItemID)
			{
				
				if(g_bPowerFXNodePrint)
					printf("\tPower found in inventory: %s-%d\n",pBagDef->fname,iItemIdx);

				if(pBagDef->pFXNodeName)
				{
					estrCreate(pestrBuffer);
					estrCopy2(pestrBuffer,pBagDef->pFXNodeName);
					(*piItemIdx) = iItemIdx;
					return true;
				}
			}
		}
	}
	return false;
}

static void PowerFXNodeNameAppendSuffix(ExprContext* pContext,
										Character* pSource,
										const char* pchWildcardSuffix,
										int iItemIdx,
										bool bIncludeSlotNumber, 
										bool bAddZSuffix, 
										bool bAddXSuffix,
										char** pestrBuffer)
{
	if(pestrBuffer && *pestrBuffer)
	{
		Character *pTarget = exprContextGetVarPointerPooled(pContext,s_pchTarget,parse_Character);

		if((pchWildcardSuffix && strcmp(pchWildcardSuffix,"")!=0)
			|| bIncludeSlotNumber
			|| (bAddZSuffix && pTarget)
			|| (bAddXSuffix && pTarget))
		{
			//Append wild card suffix
			if(pchWildcardSuffix && strcmp(pchWildcardSuffix,"")!= 0)
			{
				estrAppend2(pestrBuffer,pchWildcardSuffix);
			}

			if(bIncludeSlotNumber && iItemIdx != -1)
			{
				estrConcatf(pestrBuffer, "%d", iItemIdx+1);
			}

			if(pTarget && bAddZSuffix)
			{
				PowerFXNode_AddVerticalSuffix(pSource,pTarget,pestrBuffer);
			}

			if(pTarget && bAddXSuffix)
			{
				PowerFXNode_AddHorizontalSuffix(pSource,pTarget,pestrBuffer);
			}
		}
	}
}

// Get a power FX node from an item power
AUTO_EXPR_FUNC(powerart);
const char *PowerFXNode(ExprContext* pContext, 
						const char* pchWildcardSuffix, 
						bool bIncludeSlotNumber, 
						bool bAddZSuffix, 
						bool bAddXSuffix)
{
	PowerApplication *papp = exprContextGetVarPointerPooled(pContext,s_pchApplication,parse_PowerApplication);
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);
	Character *pchar = exprContextGetVarPointerPooled(pContext,s_pchSource,parse_Character);
	char* estrBuffer = NULL;
	const char* pchReturn = NULL;
	int iItemIdx = -1;
	S64 iItemID = 0;

	if(g_bPowerFXNodePrint)
	{
		printf("PowerFXNode request from power activation %d\n",pact?pact->uchID:-1);
		printf("\tInputs:\n\t\tWildcard: %s\n\t\tInclude Slot Number: %s\n\t\tAdd Vertical Suffix: %s\n\t\tAdd Horizontal Suffix: %s\n",pchWildcardSuffix ? pchWildcardSuffix : "<NULL>", bIncludeSlotNumber ? "TRUE" : "FALSE", bAddZSuffix ? "TRUE" : "FALSE", bAddXSuffix ? "TRUE" : "FALSE");
		printf("\tSource Entity: %s\n",pchar ? pchar->pEntParent->debugName : "<NULL>");
	}

	if (papp)
	{
		iItemID = papp->iSrcItemID;
	}
	else if (pact && pchar)
	{
		Power *ppow;

		ANALYSIS_ASSUME(pchar != NULL); 
		ppow = character_FindPowerByRef(pchar, &pact->ref);
		if(!ppow)
		{
			if(g_bPowerFXNodePrint)
			{
				printf("No power found, returning <NULL>\n\n");
			}
			return NULL;
		} 
		else if(g_bPowerFXNodePrint)
		{
			printf("\tSource Power: [%d]%s\n",ppow->uiID,REF_STRING_FROM_HANDLE(ppow->hDef));	
		}

		if(ppow->pParentPower)
			ppow = ppow->pParentPower;

		if(ppow && ppow->eSource == kPowerSource_Item && ppow->pSourceItem)
		{
			iItemID = ppow->pSourceItem->id;
		}
	}

	if (iItemID)
	{
		character_GetPowerFXNodeNameFromItemID(pchar, iItemID, &estrBuffer, &iItemIdx);
		PowerFXNodeNameAppendSuffix(pContext, 
									pchar, 
									pchWildcardSuffix, 
									iItemIdx,
									bIncludeSlotNumber, 
									bAddZSuffix, 
									bAddXSuffix,
									&estrBuffer);
	}
	else if(g_bPowerFXNodePrint)
	{
		printf("\tERROR: Source power was not granted to you from an item!");
	}

	if(estrBuffer)
	{
		pchReturn = exprContextAllocString(pContext,estrBuffer);
		estrDestroy(&estrBuffer);
	}

	if(g_bPowerFXNodePrint)
	{
		printf("PowerFXNode result - '%s'\n\n", pchReturn ? pchReturn : "<NULL>");
	}

	return pchReturn;
}

// Get the closest FX node on the source character to the target character
// Note that iNodeIndex is 1-based
AUTO_EXPR_FUNC(powerart);
const char *GetClosestPowerFXNodeToTarget(ExprContext* pContext, 
										  const char* pchWildcardSuffix, 
										  const char* pchFrontPrefix, 
										  const char* pchBackPrefix,
										  int iNodeIndex,
										  bool bAddVerticalSuffix, 
										  bool bAddHorizontalSuffix)
{
	Character* pSource = exprContextGetVarPointerPooled(pContext,s_pchSource,parse_Character);
	Character* pTarget = exprContextGetVarPointerPooled(pContext,s_pchTarget,parse_Character);
	const char* pchReturn = NULL;
	char* estrBuffer = NULL;

	if (pSource && pTarget)
	{
		estrStackCreate(&estrBuffer);

		if (pchFrontPrefix && pchFrontPrefix[0] && pchBackPrefix && pchBackPrefix[0])
		{
			PowerFXNode_AppendFrontBackString(pSource, pTarget, pchFrontPrefix, pchBackPrefix, &estrBuffer);
		}
		if (pchWildcardSuffix && pchWildcardSuffix[0])
		{
			estrAppend2(&estrBuffer, pchWildcardSuffix);
		}
		if (iNodeIndex)
		{
			estrConcatf(&estrBuffer, "%d", iNodeIndex);
		}
		if (bAddVerticalSuffix)
		{
			PowerFXNode_AddVerticalSuffix(pSource, pTarget, &estrBuffer);
		}
		if (bAddHorizontalSuffix)
		{
			PowerFXNode_AddHorizontalSuffix(pSource, pTarget, &estrBuffer);
		}
		pchReturn = exprContextAllocString(pContext,estrBuffer);
		estrDestroy(&estrBuffer);
	}
	return pchReturn;
}

AUTO_EXPR_FUNC(powerart);
bool SourceItemEquippedInSlot(ExprContext* pContext, S32 iSlot)
{
	PowerActivation *pact = exprContextGetVarPointerPooled(pContext,s_pchActivation,parse_PowerActivation);
	Character *pchar = exprContextGetVarPointerPooled(pContext,s_pchSource,parse_Character);
	GameAccountDataExtract* pExtract = entity_CreateLocalGameAccountDataExtract(entity_GetGameAccount(pchar->pEntParent));
	bool bRet = false;
	
	if (pchar && pchar->pEntParent)
	{
		Item** pItems = NULL;
		PowerDef *pDef = GET_REF(pact->hdef);
		if (pDef)
		{
			ANALYSIS_ASSUME(pDef);
			character_WeaponPick(pchar, pDef, &pItems, pExtract);
			FOR_EACH_IN_EARRAY(pItems, Item, pItem)
			{
				BagIterator* pIter = inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity,pchar->pEntParent), pItem->id);
				if (pIter->i_cur == iSlot && !bagiterator_Stopped(pIter))
					bRet = true;
			}
			FOR_EACH_END;
		}
	}
	entity_DestroyLocalGameAccountDataExtract(&pExtract);
	return bRet;
}

S32 TestShieldHitHelper(Character *pcharSource, Character *pcharTarget)
{
	if(pcharSource && pcharTarget && pcharSource!=pcharTarget)
	{
		int i;
		Vec3 vecSource, vecTarget, vecToSource;
		Vec2 pyFace;
		F32 fAngle;
		
		// Get positions and facing of target
		entGetCombatPosDir(pcharSource->pEntParent,NULL,vecSource,NULL);
		entGetCombatPosDir(pcharTarget->pEntParent,NULL,vecTarget,NULL);
		entGetFacePY(pcharTarget->pEntParent, pyFace);

		// Calculate angle from target to source, relative to target's current facing
		subVec3(vecSource,vecTarget,vecToSource);
		fAngle = atan2(vecToSource[0],vecToSource[2]);
		fAngle = subAngle(fAngle,pyFace[1]);

		// Check each shield to see if might block this
		for(i=eaSize(&pcharTarget->ppModsNet)-1; i>=0; i--)
		{
			AttribModNet *pNet = pcharTarget->ppModsNet[i];
			if(ATTRIBMODNET_VALID(pNet) && pNet->iHealth > 0)
			{
				AttribModDef *pdef = modnet_GetDef(pNet);
				F32 fAngleFromShield;

				// If we don't know what it is, or it's not a Shield, continue
				if(!pdef || pdef->offAttrib!=kAttribType_Shield)
					continue;
				
				// If the def isn't arc limited, we'll assume it blocks
				if(!pdef->fArcAffects)
					break;

				// Essentially same code as mod_AffectsModFromDirection
				fAngleFromShield = pdef->fYaw ? subAngle(fAngle,RAD(pdef->fYaw)) : fAngle;
				if(fabs(fAngleFromShield) <= RAD(pdef->fArcAffects)/2.f)
					break;
			}
		}

		if(i>=0)
			return 1;
	}
	
	return 0;
}

// Simple function to check if the source is likely to hit an active Shield of the target.  Returns 1 on a hit.
AUTO_EXPR_FUNC(powerart);
S32 TestShieldHit(ExprContext *pContext)
{
	Character *pcharSource = exprContextGetVarPointerPooled(pContext,s_pchSource,parse_Character);
	Character *pcharTarget = exprContextGetVarPointerPooled(pContext,s_pchTarget,parse_Character);
	return TestShieldHitHelper(pcharSource, pcharTarget);
}

AUTO_EXPR_FUNC(powerart);
S32 TestShieldHitSource(ExprContext *pContext)
{
	Character *pcharSource = exprContextGetVarPointerPooled(pContext,s_pchSource,parse_Character);
	Character *pcharTarget = exprContextGetVarPointerPooled(pContext,s_pchTarget,parse_Character);
	// switch the source and target
	return TestShieldHitHelper(pcharTarget, pcharSource);
}

AUTO_EXPR_FUNC(powerart);
S32 SourceAllegiance(ExprContext *pContext,const char *pchAllegiance)
{
	Entity *pent = exprContextGetSelfPtr(pContext);
	AllegianceDef *pdef = pent ? GET_REF(pent->hAllegiance) : NULL;
	AllegianceDef *psubdef = pent ? GET_REF(pent->hSubAllegiance) : NULL;
	return (pdef && !stricmp_safe(pdef->pcName,pchAllegiance)) || (psubdef && !stricmp_safe(psubdef->pcName,pchAllegiance));
}

AUTO_EXPR_FUNC(powerart);
S32 SourceFaction(ExprContext *pContext, const char *pchFaction)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	CritterFaction *pFaction = pEnt ? GET_REF(pEnt->hFaction) : NULL;
	CritterFaction *pSubFaction = pEnt ? GET_REF(pEnt->hSubFaction) : NULL;
	return (pFaction && !stricmp_safe(pFaction->pchName,pchFaction)) || (pSubFaction && !stricmp_safe(pSubFaction->pchName,pchFaction));
}

// iFXNumber probably won't be > 7
// uchPulse probably won't be very big either (<63?)
static U32 GetFXID(U8 uchClientActID, U8 uchPulse, int iFXNumber, PowerAnimFXType eType)
{
	return (eType<<17) | (uchClientActID<<9) | (uchPulse<<3) | iFXNumber;
}

// Flashes the given fx from the source character to the target character/location
static void character_FlashFXEx(int iPartitionIdx,
								Character *pcharSource,
								Vec3 vecSource,
								U32 uiID,
							    U32 uiSubID,
							    PowerAnimFXType eType,
							    Character *pcharTarget,
							    const Vec3 vecTarget,
								PowerApplication *papp,
							    PowerActivation *pact,
								Power *ppow,
							    const char **ppchFXNames,
							    PowerFXParam **ppParams,
							    F32 fHue,
							    U32 uiTime,
								EPowerAnimFXFlag ePowerAnimFlags,
								PowerAnimNodeSelectionType eNodeSelectType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	DynParamBlock *pParams;
	EntityRef erSource, erTarget;
	PowerDef* pPowDef = pact ? GET_REF(pact->hdef) : (papp ? papp->pdef : NULL);
	PowerTarget* pPowTarget = pPowDef ? GET_REF(pPowDef->hTargetMain) : NULL;
	F32 fArc, fYaw;
	EPMFXStartFlags eFlags;
	F32 fPowerRange = 0.f;

	if(!eaSize(&ppchFXNames))
		return;

	erSource = entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget ? entGetRef(pcharTarget->pEntParent) : 0;
	pParams = CreateParamBlock(iPartitionIdx,pcharSource,pcharTarget,papp,pact,ppParams,(ePowerAnimFlags&EPowerFXFlags_MISS)!=0, 0);
	fArc = pPowDef ? RAD(pPowDef->fTargetArc) : 0.0f;
	fYaw = ppow ? ppow->fYaw : (pPowDef ? RAD(pPowDef->fYaw) : 0.0f);
	
	if (CHECK_FLAG(ePowerAnimFlags, EPowerAnimFXFlag_ADD_POWER_AREA_EFFECT_PARAMS))
	{
		DynParamBlock_AddPowerScaleParams(&pParams, ppow, pPowDef);
	}
	
	if(CHECK_FLAG(ePowerAnimFlags, EPowerAnimFXFlag_GLOBAL))
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pcharSource->pEntParent));
		if(!(e && e->pChar))
			return;
		pcharSource = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pcharSource);

	eFlags = EPowerAnimFXFlag_To_EPMFXStartFlags(ePowerAnimFlags);

	//Always use the source vec for destructible objects
	if(IS_HANDLE_ACTIVE(pcharSource->pEntParent->hCreatorNode))
		eFlags |= EPowerFXFlags_FROM_SOURCE_VEC; 

	eFlags |= EPMFXStartFlags_FLASH;
	if(SAFE_MEMBER(pPowTarget, bDoNotTargetUnlessRequired))
		eFlags |= EPMFXStartFlags_USE_TARGET_NODE;
	if (!ppow || !pPowDef || pPowDef->fTimeActivate == 0.f) 
		eFlags |= EPowerFXFlags_DO_NOT_TRACK_FLASHED;

	if (pPowDef)
		fPowerRange = power_GetRange(ppow, pPowDef);
			
	pmFxStart(	pcharSource->pPowersMovement,
				uiID,uiSubID,eType,
				erSource,
				erTarget,
				uiTime,
				ppchFXNames,
				pParams,
				fHue,
				fPowerRange,
				fArc,
				fYaw,
				vecSource,
				vecTarget,
				NULL,
				eFlags,
				eNodeSelectType); 
#endif
}

void character_FlashFX(int iPartitionIdx,
					   Character *pcharSource,
					   U32 uiID,
					   U32 uiSubID,
					   PowerAnimFXType eType,
					   Character *pcharTarget,
					   const Vec3 vecTarget,
					   PowerApplication *papp,
					   PowerActivation *pact,
					   Power *ppow,
					   const char **ppchFXNames,
					   PowerFXParam **ppParams,
					   F32 fHue,
					   U32 uiTime,
					   EPowerAnimFXFlag ePowerAnimFlags,
					   PowerAnimNodeSelectionType eNodeSelectType)
{
	character_FlashFXEx(iPartitionIdx,
						pcharSource,
						NULL,
						uiID,
						uiSubID,
						eType,
						pcharTarget,
						vecTarget,
						papp,
						pact,
						ppow,
						ppchFXNames,
						ppParams,
						fHue,
						uiTime,
						ePowerAnimFlags,
						eNodeSelectType);					
}

// Cancels the given fx flash from the source character to the target character/location
void character_FlashFXCancel(Character *pcharSource,
							 U32 uiID,
							 U32 uiSubID,
							 PowerAnimFXType eType,
							 Character *pcharTarget,
							 const char **ppchFXNames,
							 int bGlobal)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	EntityRef erSource, erTarget;

	if(!eaSize(&ppchFXNames))
		return;

	erSource = entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget ? entGetRef(pcharTarget->pEntParent) : 0;

	if(bGlobal)
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pcharSource->pEntParent));
		if(!(e && e->pChar))
			return;
		pcharSource = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pcharSource);

	pmFxCancel(	pcharSource->pPowersMovement,
				uiID,uiSubID,eType,
				erSource,
				erTarget);
#endif
}

// Flashes the given fx from the source character to the target character/location
// TODO(JW): Hack: Temp ActivateFX version
void character_FlashActivateFX(int iPartitionIdx,
							   Character *pcharSource,
							   U32 uiID,
							   U32 uiSubID,
							   PowerAnimFXType eType,
							   Character *pcharTarget,
							   Vec3 vecTarget,
							   PowerApplication *papp,
							   PowerActivation *pact,
							   Power *ppow,
							   PowerActivateFX **ppFX,
							   PowerFXParam **ppParams,
							   F32 fHue,
							   U32 uiTime,
							   EPowerAnimFXFlag ePowerAnimFlags,
							   PowerAnimNodeSelectionType eNodeSelectType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	int i,s;
	DynParamBlock *pParams;
	EntityRef erSource, erTarget;
	PowerDef* pPowDef;
	F32 fArc, fYaw;
	static const char **s_ppchFxNames = NULL;
	EPMFXStartFlags eFlags;
	F32 fPowerRange = 0.f;
	
	if(!eaSize(&ppFX))
		return;

	erSource = entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;
	pParams = CreateParamBlock(iPartitionIdx,pcharSource,pcharTarget,papp,pact,ppParams,(ePowerAnimFlags&EPowerFXFlags_MISS)!=0, 0);
	pPowDef = pact ? GET_REF(pact->hdef) : (papp ? papp->pdef : NULL);
	fArc = pPowDef ? RAD(pPowDef->fTargetArc) : 0.0f;
	fYaw = ppow ? ppow->fYaw : (pPowDef ? RAD(pPowDef->fYaw) : 0.0f);

	if (ppow && CHECK_FLAG(ePowerAnimFlags, EPowerAnimFXFlag_ADD_POWER_AREA_EFFECT_PARAMS))
	{
		DynParamBlock_AddPowerScaleParams(&pParams, ppow, pPowDef);
	}

	if(CHECK_FLAG(ePowerAnimFlags, EPowerAnimFXFlag_GLOBAL))
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pcharSource->pEntParent));
		if(!(e && e->pChar))
			return;
		pcharSource = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pcharSource);

	eFlags = EPowerAnimFXFlag_To_EPMFXStartFlags(ePowerAnimFlags);
	eFlags |= EPMFXStartFlags_FLASH;
	if (!ppow || !pPowDef || pPowDef->fTimeActivate == 0.f) 
		eFlags |= EPowerFXFlags_DO_NOT_TRACK_FLASHED;
	
	if (pPowDef)
		fPowerRange = power_GetRange(ppow, pPowDef);

	s = eaSize(&ppFX);
	for(i=0; i<s; i++)
	{
		if (ppFX[i]->pchActivateFX)
		{
			// create a temp earray with one element, because pmFxStart needs it
			eaClearFast(&s_ppchFxNames);
			eaPush(&s_ppchFxNames,ppFX[i]->pchActivateFX);
			pmFxStart(pcharSource->pPowersMovement,
				uiID,uiSubID+(i<<8),eType,
				erSource,
				erTarget,
				pmTimestampFrom(uiTime,ppFX[i]->iActivateFrameOffset/PAFX_FPS),
				s_ppchFxNames,
				dynParamBlockCopy(pParams),
				fHue,
				fPowerRange,
				fArc,
				fYaw,
				NULL,
				vecTarget,
				NULL,
				eFlags,
				eNodeSelectType);
		}
	}

	dynParamBlockFree(pParams);
#endif
}

// Cancels the given fx flash from the source character to the target character/location
// TODO(JW): Hack: Temp ActivateFX version
void character_FlashActivateFXCancel(Character *pcharSource,
									 U32 uiID,
									 U32 uiSubID,
									 PowerAnimFXType eType,
									 Character *pcharTarget,
									 PowerActivateFX **ppFX,
									 int bGlobal)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	int i,s;
	EntityRef erSource, erTarget;

	if(!eaSize(&ppFX))
		return;

	erSource = entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;

	if(bGlobal)
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pcharSource->pEntParent));
		if(!(e && e->pChar))
			return;
		pcharSource = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pcharSource);

	s = eaSize(&ppFX);
	for(i=0; i<s; i++)
	{
		pmFxCancel(pcharSource->pPowersMovement,
			uiID,uiSubID+(i<<8),eType,
			erSource,
			erTarget);
	}
#endif
}

// Turns on the FX from source to target
void location_StickyFXOn(Vec3 vecSource,
						 int iPartitionIdx,
						 U32 uiID,
						 U32 uiSubID,
						 PowerAnimFXType eType,
						 Character *pcharTarget,
						 Vec3 vecTarget,
						 PowerActivation *pact,
						 Power *ppow,
						 const char **ppchFXNames,
						 PowerFXParam **ppParams,
						 F32 fHue,
						 U32 uiTime,
						 EPowerAnimFXFlag ePowerAnimFlags)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	DynParamBlock *pParams;
	EntityRef erSource, erTarget;
	Entity* e = getTransportEnt(iPartitionIdx);
	Character *pcharSource = e ? e->pChar : NULL;
	PowerDef* pPowDef;
	F32 fArc, fYaw;
	EPMFXStartFlags eFlags;
	F32 fPowerRange = 0.f;

	if(!pcharSource || !eaSize(&ppchFXNames))
		return;

	PM_CREATE_SAFE(pcharSource);

	erSource = 0; // Location based FX use 0 source anyways.
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;
	pParams = CreateParamBlock(iPartitionIdx,pcharSource,pcharTarget,NULL,pact,ppParams,(ePowerAnimFlags&EPowerFXFlags_MISS)!=0, 0);
	pPowDef = pact ? GET_REF(pact->hdef) : NULL;
	fArc = pPowDef ? RAD(pPowDef->fTargetArc) : 0.0f;
	fYaw = ppow ? ppow->fYaw : (pPowDef ? RAD(pPowDef->fYaw) : 0.0f);

	eFlags = EPowerAnimFXFlag_To_EPMFXStartFlags(ePowerAnimFlags);
	eFlags |= EPowerFXFlags_FROM_SOURCE_VEC;
	// should we turn off any flags that aren't expected to be here ?
	if (pPowDef)
		fPowerRange = power_GetRange(ppow, pPowDef);

	pmFxStart(pcharSource->pPowersMovement,
				uiID,uiSubID,eType,
				erSource,
				erTarget,
				uiTime,
				ppchFXNames,
				pParams,
				fHue,
				fPowerRange,
				fArc,
				fYaw,
				vecSource,
				vecTarget,
				NULL,
				eFlags,
				0);
#endif
}

// Turns off the FX
void location_StickyFXOff(int iPartitionIdx,
						  U32 uiID,
						  U32 uiSubID,
						  PowerAnimFXType eType,
						  Character *pcharTarget,
						  U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	EntityRef erSource, erTarget;
	Entity* e = getTransportEnt(iPartitionIdx);
	Character *pcharSource = e ? e->pChar : NULL;

	if(!pcharSource)
		return;

	PM_CREATE_SAFE(pcharSource);

	erSource = 0; //entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;
	
	pmFxStop(pcharSource->pPowersMovement,
				uiID,uiSubID,eType,
				erSource, 
				erTarget, 
				uiTime, 
				NULL);
#endif
}

// Flashes the given fx from the source vector to the target character/location
static void location_FlashFX(Vec3 vecSource,
							 int iPartitionIdx,
							 U32 uiID,
							 U32 uiSubID,
							 PowerAnimFXType eType,
							 Character *pcharTarget,
							 Vec3 vecTarget,
							 PowerActivation *pact,
							 Power *ppow,
							 const char **ppchFXNames,
							 PowerFXParam **ppParams,
							 F32 fHue,
							 U32 uiTime,
							 EPowerAnimFXFlag ePowerAnimFlags)						 
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	DynParamBlock *pParams;
	EntityRef erSource, erTarget;
	Entity* e = getTransportEnt(iPartitionIdx);
	Character *pcharSource = e ? e->pChar : NULL;
	PowerDef* pPowDef;
	F32 fArc, fYaw;
	EPMFXStartFlags eFlags;
	F32 fPowerRange = 0.f;

	if(!pcharSource || !eaSize(&ppchFXNames))
		return;

	PM_CREATE_SAFE(pcharSource);

	erSource = entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;
	pParams = CreateParamBlock(iPartitionIdx,pcharSource,pcharTarget,NULL,pact,ppParams,(ePowerAnimFlags&EPowerFXFlags_MISS)!=0, 0);
	pPowDef = pact ? GET_REF(pact->hdef) : NULL;
	fArc = pPowDef ? RAD(pPowDef->fTargetArc) : 0.0f;
	fYaw = ppow ? ppow->fYaw : (pPowDef ? RAD(pPowDef->fYaw) : 0.0f);
	
	eFlags = EPowerAnimFXFlag_To_EPMFXStartFlags(ePowerAnimFlags);
	eFlags |= (EPMFXStartFlags_FLASH | EPowerFXFlags_FROM_SOURCE_VEC);
	if (!ppow || !pPowDef || pPowDef->fTimeActivate == 0.f) 
		eFlags |= EPowerFXFlags_DO_NOT_TRACK_FLASHED;
	if (pPowDef)
		fPowerRange = power_GetRange(ppow, pPowDef);

	// should we turn off any flags that aren't expected to be here ?

	pmFxStart(pcharSource->pPowersMovement,
		uiID,uiSubID,eType,
		erSource,
		erTarget,
		uiTime,
		ppchFXNames,
		pParams,
		fHue,
		fPowerRange,
		fArc,
		fYaw,
		vecSource,
		vecTarget,
		NULL,
		eFlags,
		0);
#endif
}

// Flashes the given fx from the source vector to the target character/location
// TODO(JW): Hack: Temp ActivateFX version
static void location_FlashActivateFX(SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 vecSource,
									 int iPartitionIdx,
									 U32 uiID,
									 U32 uiSubID,
									 PowerAnimFXType eType,
									 SA_PARAM_OP_VALID Character *pcharTarget,
									 SA_PRE_OP_ELEMS(3) SA_POST_OP_VALID Vec3 vecTarget,
									 SA_PRE_OP_ELEMS(4) SA_POST_OP_VALID Quat quatTarget,
									 PowerActivation *pact,
									 Power *ppow,
									 PowerActivateFX **ppFX,
									 PowerFXParam **ppParams,
									 F32 fHue,
									 U32 uiTime,
									 EPowerAnimFXFlag ePowerAnimFlags,
									 PowerAnimNodeSelectionType eNodeSelectType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	int i,s;
	DynParamBlock *pParams;
	EntityRef erSource, erTarget;
	Entity* e = getTransportEnt(iPartitionIdx);
	Character *pcharSource = e ? e->pChar : NULL;
	PowerDef* pPowDef;
	F32 fArc, fYaw;
	EPMFXStartFlags eFlags;
	static const char **s_ppchFxNames = NULL;
	F32 fPowerRange = 0.f;

	if(!pcharSource || !eaSize(&ppFX))
		return;

	PM_CREATE_SAFE(pcharSource);

	erSource = entGetRef(pcharSource->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;
	pParams = CreateParamBlock(iPartitionIdx,pcharSource,pcharTarget,NULL,pact,ppParams,(ePowerAnimFlags&EPowerFXFlags_MISS)!=0, 0);
	pPowDef = pact ? GET_REF(pact->hdef) : NULL;
	fArc = pPowDef ? RAD(pPowDef->fTargetArc) : 0.0f;
	fYaw = ppow ? ppow->fYaw : (pPowDef ? RAD(pPowDef->fYaw) : 0.0f);

	eFlags = EPowerAnimFXFlag_To_EPMFXStartFlags(ePowerAnimFlags);
	eFlags |= (EPMFXStartFlags_FLASH | EPowerFXFlags_FROM_SOURCE_VEC);
	if (!ppow || !pPowDef || pPowDef->fTimeActivate == 0.f) 
		eFlags |= EPowerFXFlags_DO_NOT_TRACK_FLASHED;
	// should we turn off any flags that aren't expected to be here ?
	if (pPowDef)
		fPowerRange = power_GetRange(ppow, pPowDef);

	s = eaSize(&ppFX);
	for(i=0; i<s; i++)
	{
		eaClearFast(&s_ppchFxNames);
		eaPush(&s_ppchFxNames,ppFX[i]->pchActivateFX);
		pmFxStart(pcharSource->pPowersMovement,
			uiID,uiSubID+(i<<8),eType,
			erSource,
			erTarget,
			pmTimestampFrom(uiTime,ppFX[i]->iActivateFrameOffset/PAFX_FPS),
			s_ppchFxNames,
			dynParamBlockCopy(pParams),
			fHue,
			fPowerRange,
			fArc,
			fYaw,
			vecSource,
			vecTarget,
			quatTarget,
			eFlags,
			eNodeSelectType);
	}

	dynParamBlockFree(pParams);
#endif
}




// Turns on the given fx on the character
void character_StickyFXOn(int iPartitionIdx, 
							Character *pchar, 
							U32 uiID, 
							U32 uiSubID, 
							PowerAnimFXType eType, 
							Character *pcharTarget,
							const Vec3 vecTarget, 
							PowerApplication *papp, 
							PowerActivation *pact, 
							Power *ppow, 
							const char **ppchFXName, 
							PowerFXParam **ppParams, 
							F32 fHue, 
							U32 uiTime, 
							EPowerAnimFXFlag ePowerAnimFlags, 
							PowerAnimNodeSelectionType eNodeSelectType, 
							U32 uiEquipSlot )
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	DynParamBlock *pBlock;
	EntityRef erSource,erTarget;
	PowerDef* pPowDef = pact ? GET_REF(pact->hdef) : (papp ? papp->pdef : NULL);
	PowerTarget* pPowTarget = pPowDef ? GET_REF(pPowDef->hTargetMain) : NULL;
	F32 fArc, fYaw;
	EPMFXStartFlags eFlags;
	F32 fPowerRange = 0.f;

	if(!eaSize(&ppchFXName))
		return;

	erSource = entGetRef(pchar->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;
	pBlock = CreateParamBlock(iPartitionIdx, pchar, pcharTarget, papp, pact, ppParams, (ePowerAnimFlags&EPowerFXFlags_MISS)!=0, uiEquipSlot);
	fArc = pPowDef ? RAD(pPowDef->fTargetArc) : 0.0f;
	fYaw = ppow ? ppow->fYaw : (pPowDef ? RAD(pPowDef->fYaw) : 0.0f);

	if (ppow && CHECK_FLAG(ePowerAnimFlags,EPowerAnimFXFlag_ADD_POWER_AREA_EFFECT_PARAMS))
	{
		DynParamBlock_AddPowerScaleParams(&pBlock, ppow, pPowDef);
	}

	if(CHECK_FLAG(ePowerAnimFlags,EPowerAnimFXFlag_GLOBAL))
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pchar->pEntParent));
		if(!(e && e->pChar))
			return;
		pchar = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pchar);

	eFlags = EPowerAnimFXFlag_To_EPMFXStartFlags(ePowerAnimFlags);
	if(SAFE_MEMBER(pPowTarget, bDoNotTargetUnlessRequired))
		eFlags |= EPMFXStartFlags_USE_TARGET_NODE;
	if (pPowDef)
		fPowerRange = power_GetRange(ppow, pPowDef);

	// should we turn off any flags that aren't expected to be here ?

	pmFxStart(pchar->pPowersMovement,
		uiID,
		uiSubID,
		eType,
		erSource,
		erTarget,
		uiTime,
		ppchFXName,
		pBlock,
		fHue,
		fPowerRange,
		fArc,
		fYaw,
		NULL,
		vecTarget,
		NULL,
		eFlags,
		eNodeSelectType);
#endif
}

// Turns off the given fx on the character
void character_StickyFXOff(Character *pchar,
						   U32 uiID,
						   U32 uiSubID,
						   PowerAnimFXType eType,
						   Character *pcharTarget,
						   const char **ppchFXName, 
						   U32 uiTime,
						   int bGlobal)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	int i;
	EntityRef erSource,erTarget;

	if(!eaSize(&ppchFXName))
		return;

	erSource = entGetRef(pchar->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;

	if(bGlobal)
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pchar->pEntParent));
		if(!(e && e->pChar))
			return;
		pchar = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pchar);

	for(i=eaSize(&ppchFXName)-1; i>=0; i--)
	{
 		pmFxStop(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,erTarget,uiTime,ppchFXName[i]);
	}
#endif
}

// Turns off the given fx on the character
void character_StickyFXCancel(Character *pchar,
							  Character *pcharTarget,
							  U32 uiID,
							  U32 uiSubID,
							  PowerAnimFXType eType,
							  const char **ppchFXName,
							  int bGlobal)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	EntityRef erSource,erTarget;

	if(!eaSize(&ppchFXName))
		return;

	erSource = entGetRef(pchar->pEntParent);
	erTarget = pcharTarget && !IS_HANDLE_ACTIVE(pcharTarget->pEntParent->hCreatorNode) ? entGetRef(pcharTarget->pEntParent) : 0;

	if(bGlobal)
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pchar->pEntParent));
		if(!(e && e->pChar))
			return;
		pchar = e->pChar;
		uiSubID &= erSource;
	}

	PM_CREATE_SAFE(pchar);

	pmFxCancel(pchar->pPowersMovement,uiID,uiSubID,eType,erSource,erTarget);
#endif
}

// Build a HitReact package on the Character, given the IDs, attacker, etc
static void character_FlashHitReact(int iPartitionIdx,
									Character *pchar,
									U32 uiID,
									U32 uiSubID,
									Character *pcharAttacker,
									PowerActivation *pact,
									const char **ppchBitNames,
									const char **ppchFXNames,
									PowerFXParam **ppParams,
									F32 fHue,
									U32 uiTimeDead,
									U32 bGlobal)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	DynParamBlock *pParams;
	EntityRef er, erAttacker;

	if(!(eaSize(&ppchBitNames) || eaSize(&ppchFXNames)))
		return;

	er = entGetRef(pchar->pEntParent);
	erAttacker = pcharAttacker ? entGetRef(pcharAttacker->pEntParent) : 0;
	pParams = CreateParamBlock(iPartitionIdx,pchar,pcharAttacker,NULL,pact,ppParams,false, 0);

	if(bGlobal)
	{
		Entity *e = getTransportEnt(entGetPartitionIdx(pchar->pEntParent));
		if(!(e && e->pChar))
			return;
		pcharAttacker = e->pChar;
		uiSubID &= er;
	}

	PM_CREATE_SAFE(pchar);

	pmHitReactStart(pchar->pPowersMovement,
		uiID,uiSubID,erAttacker,
		ppchBitNames,
		ppchFXNames,
		pParams,
		fHue,
		uiTimeDead);
#endif
}

#if GAMESERVER || GAMECLIENT
static TacticalDisableFlags GetTacticalDisableFlags(PowerAnimFX *pafx)
{
	TacticalDisableFlags flags = 0;

	if (g_CombatConfig.tactical.bRollDisableDuringPowers)
		flags |= TDF_ROLL;
	if (g_CombatConfig.tactical.bAimDisableDuringPowers)
		flags |= TDF_AIM;
	if (g_CombatConfig.tactical.bSprintDisableDuringPowers)
		flags |= TDF_SPRINT;

	return flags;
}
#endif

// Starts the charge bits and fx
void character_AnimFXChargeOn(int iPartitionIdx,
							  Character *pchar,
							  PowerActivation *pact,
							  Power *ppow,
							  const Vec3 vecTarget)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiTime = pact->uiTimestampActivate;
		U32 uiID = pact->uchID;
		int bVec = pact->bChargeAtVecTarget;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);
		Character *pcharTarget = charFromEntRef(iPartitionIdx, pact->erTarget);
		PowerDef* pPowDef = GET_REF(pact->hdef);
		PowerTarget* pPowTarget = pPowDef ? GET_REF(pPowDef->hTargetMain) : NULL;

		if(!pcharTarget && !SAFE_MEMBER(pPowTarget,bDoNotTargetUnlessRequired)) 
			pcharTarget = pchar;
		
		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				EPowerAnimFXFlag ePowerAnimFlags = 0;
				F32 fHue = pref->fHue;
				bGlobal |= (i<0 && pafx->bGlobalFX);

				if (pafx->bLocationActivate)
					bVec = true;

				if (i < 0 && pafx->bChargeFXNeedsPowerScales)
				{
					power_RefreshCachedAreaOfEffectExprValue(iPartitionIdx, pchar, ppow, pPowDef);
				}
				
				ePowerAnimFlags = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlags |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;
				if (pafx->bChargeFXNeedsPowerScales)
					ePowerAnimFlags |= EPowerAnimFXFlag_ADD_POWER_AREA_EFFECT_PARAMS;

				character_StickyBitsOn(pchar,uiID,0,kPowerAnimFXType_ChargeSticky,erSource,pafx->ppchChargeStickyBits,uiTime);
								
				character_StickyFXOn(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_ChargeSticky,
										(bVec? NULL : pcharTarget),
										(bVec? vecTarget : NULL),
										NULL,
										pact,
										ppow,
										pafx->ppchChargeStickyFX,
										pafx->ppChargeStickyFXParams,
										fHue,
										uiTime,
										ePowerAnimFlags,
										PowerAnimFX_GetNodeSelectionType(pafx), 0);

				character_FlashBitsOn(pchar,uiID,0,kPowerAnimFXType_ChargeFlash,erSource,pafx->ppchChargeFlashBits,uiTime,false,false,false,false);
				character_FlashFX(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_ChargeFlash, 
									(bVec? NULL : pcharTarget), 
									(bVec? vecTarget : NULL),
									NULL,
									pact,
									ppow,
									pafx->ppchChargeFlashFX,
									pafx->ppChargeFlashFXParams,
									fHue,
									uiTime,
									ePowerAnimFlags,
									PowerAnimFX_GetNodeSelectionType(pafx));
				character_SendAnimKeywordOrFlag(pchar,uiID,0,kPowerAnimFXType_ChargeFlash,erSource,pafx->pchAnimKeyword,pact,uiTime,false,false,false,true,true,false);
				
				// Optional root during charge from primary
				if(i<0)
				{
					if(pafx->fSpeedPenaltyDuringCharge==1)
					{
						character_PowerActivationRootStart(pchar,uiID,kPowerAnimFXType_ChargeSticky,uiTime,pPowDef?pPowDef->pchName:"UNKNOWN");
					}
					else if(pafx->fSpeedPenaltyDuringCharge>0)
					{
						ASSERT_FALSE_AND_SET(pact->bSpeedPenaltyIsSet);
						mrSurfaceSpeedPenaltyStart(	pchar->pEntParent->mm.mrSurface,
							uiID,
							pafx->fSpeedPenaltyDuringCharge,
							uiTime);
					}
					
					if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
					{
						TacticalDisableFlags flags = GetTacticalDisableFlags(pafx);
						PowerDef *pdef = g_CombatConfig.tactical.bAimCancelsPowersBeforeHitFrame ? GET_REF(pact->hdef) : NULL;
						
						if (!pdef || pdef->bDoNotAllowCancelBeforeHitFrame)
						{
							mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical,uiID,flags,uiTime);
						}
					}
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
#endif
}

// Stops the charge bits and fx.  If the server and client will both do this, but at different times,
//  set bSynced to false.
void character_AnimFXChargeOff(int iPartitionIdx,
							   Character *pchar,
							   PowerActivation *pact,
							   U32 uiTime,
							   bool bSynced)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		bool bPeriodic = pact->uiPeriod;
		U32 uiID = pact->uchID;
		int bVec = pact->bChargeAtVecTarget;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);
		Character *pcharTarget = charFromEntRef(iPartitionIdx, pact->erTarget);
		PowerDef *pdef = GET_REF(pact->hdef);
		// this is probably a heavy hammer, but if we're using UpdateChargeTargetOnDeactivate, clear the target 
		// so it cleans everything up from the charge
		if (pdef && pdef->bUpdateChargeTargetOnDeactivate)
		{
			pcharTarget = NULL;
		}

		//if(!pcharTarget) pcharTarget = pchar;	// If we can't find the target, we should probably pass 0 to clean anything up

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				// TODO(JW): Charge: Do we need to check for periodic when we're just turning off charging?
				U32 uiTimeBits = pmTimestampFrom(uiTime,(bPeriodic?pafx->iFramesBeforePeriodicActivateBits:pafx->iFramesBeforeActivateBits)/PAFX_FPS);
				U32 uiTimeFX = pmTimestampFrom(uiTime,(bPeriodic?pafx->iFramesBeforePeriodicActivateFX:pafx->iFramesBeforeActivateFX)/PAFX_FPS);
				bGlobal |= (i<0 && pafx->bGlobalFX);

				if (pafx->bLocationActivate)
					bVec = true;

				character_StickyBitsOff(pchar, uiID, 0, kPowerAnimFXType_ChargeSticky, erSource, pafx->ppchChargeStickyBits, uiTimeBits);
				character_StickyFXOff(pchar, uiID, 0, kPowerAnimFXType_ChargeSticky, 
										(bVec?NULL:pcharTarget), pafx->ppchChargeStickyFX, uiTimeFX, bGlobal);
				if(i<0)
				{
					if(pafx->fSpeedPenaltyDuringCharge==1)
						character_PowerActivationRootStop(pchar,uiID,kPowerAnimFXType_ChargeSticky,uiTime);
					else if(TRUE_THEN_RESET(pact->bSpeedPenaltyIsSet))
						mrSurfaceSpeedPenaltyStop(pchar->pEntParent->mm.mrSurface,uiID,uiTime);
					
					if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
						mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical,uiID,uiTime);
				}
				
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
#endif
}

// Cancels the charge bits/fx/moves
void character_AnimFXChargeCancel(int iPartitionIdx, Character *pchar, PowerActivation *pact, bool bInterruped)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bVec = pact->bChargeAtVecTarget;
		U32 uiTime = pact->uiTimestampCurrented;  // JE: Hopefully this is the timestamp we want...
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);
		Character *pcharTarget = charFromEntRef(iPartitionIdx,pact->erTarget);
		PowerDef *pdef = GET_REF(pact->hdef);
		// this is probably a heavy hammer, but if we're using UpdateChargeTargetOnDeactivate, clear the target 
		// so it cleans everything up from the charge
		if (pdef && pdef->bUpdateChargeTargetOnDeactivate)
		{
			pcharTarget = NULL;
		}

		//if(!pcharTarget) pcharTarget = pchar;	// If we can't find the target, we should probably pass 0 to clean anything up

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				bGlobal |= (i<0 && pafx->bGlobalFX);
				character_BitsCancel(pchar,uiID,0,kPowerAnimFXType_ChargeFlash,erSource);
				character_BitsCancel(pchar,uiID,0,kPowerAnimFXType_ChargeSticky,erSource);
				character_FlashFXCancel(pchar,uiID,0,kPowerAnimFXType_ChargeFlash,bVec?NULL:pcharTarget,pafx->ppchChargeFlashFX,bGlobal);
				character_StickyFXCancel(pchar,bVec?NULL:pcharTarget,uiID,0,kPowerAnimFXType_ChargeSticky,pafx->ppchChargeStickyFX,bGlobal);
				pmReleaseAnim(pchar->pPowersMovement,uiTime,uiID,"abort");

				if(i<0)
				{
					if (bInterruped)
						character_SendAnimKeywordOrFlag(pchar,uiID,1,kPowerAnimFXType_ChargeFlash,erSource,s_pchFlagInterrupt,pact,pmTimestamp(0),false,false,false,false,true,false);

					if(pafx->fSpeedPenaltyDuringCharge==1)
						character_PowerActivationRootCancel(pchar,uiID,kPowerAnimFXType_ChargeSticky);
					else if(TRUE_THEN_RESET(pact->bSpeedPenaltyIsSet))
						mrSurfaceSpeedPenaltyStop(pchar->pEntParent->mm.mrSurface,uiID,pmTimestamp(0));

					if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
						mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical,uiID,pmTimestamp(0));
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}

	character_MoveCancel(pchar,pact->uchID,0);
#endif
}

// Starts the charge bits and fx from a location
void location_AnimFXChargeOn(Vec3 vecSource,
							 int iPartitionIdx,
							 PowerAnimFX *pafx,
							 Character *pcharTarget,
							 Vec3 vecTarget,
							 U32 uiTime,
							 U32 uiID)
{
	location_FlashFX(vecSource,iPartitionIdx,uiID,uiID,kPowerAnimFXType_ChargeFlash,
					pcharTarget,vecTarget,NULL,NULL,pafx->ppchChargeFlashFX,
					pafx->ppChargeFlashFXParams,pafx->fDefaultHue,uiTime,0);
}



// Starts and stops the lunge bits and fx based on the times in the activation
void character_AnimFXLunge(int iPartitionIdx,
						   Character *pchar,
						   PowerActivation *pact)
{
	if(pact->uiTimestampLungeAnimate && pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);
		PowerDef *pdef = GET_REF(pact->hdef);
		Character *pCharTarget = NULL;

		if (pact->erTarget)
		{
			Entity *pTargetEnt = entFromEntityRef(iPartitionIdx, pact->erTarget);
			if (pTargetEnt && pTargetEnt->pChar)
			{
				pCharTarget = pTargetEnt->pChar;
			}
		}

		character_PowerActivationRootStart(pchar,uiID,kPowerAnimFXType_LungeSticky,
										   pact->uiTimestampLungeAnimate,pdef?pdef->pchName:"UNKNOWN");

		character_PowerActivationRootStop(pchar,uiID,kPowerAnimFXType_LungeSticky,pact->uiTimestampActivate);

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				F32 fHue = pref->fHue;
				EPowerAnimFXFlag ePowerAnimFlags;

				bGlobal |= (i<0 && pafx->bGlobalFX);
				
				ePowerAnimFlags = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlags |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				character_FlashBitsOn(pchar, uiID, 0, kPowerAnimFXType_LungeFlash, erSource,
									  pafx->ppchLungeFlashBits, pact->uiTimestampLungeAnimate,
									  false, false, false, false);

				character_StickyBitsOn(pchar, uiID, 0, kPowerAnimFXType_LungeSticky, erSource,
									   pafx->ppchLungeStickyBits, pact->uiTimestampLungeAnimate);

				character_StickyBitsOff(pchar,uiID,0,kPowerAnimFXType_LungeSticky,erSource,
										pafx->ppchLungeStickyBits,pact->uiTimestampActivate);
				
				character_FlashFX(iPartitionIdx, pchar, uiID, 0, kPowerAnimFXType_LungeFlash, pCharTarget, 
								  NULL, NULL, pact, NULL, pafx->ppchLungeFlashFX, pafx->ppLungeFlashFXParams, fHue,
								  pact->uiTimestampLungeAnimate, ePowerAnimFlags, PowerAnimFX_GetNodeSelectionType(pafx));

				character_StickyFXOn(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_LungeSticky, pCharTarget, 
									 NULL, NULL, pact, NULL, pafx->ppchLungeStickyFX, pafx->ppLungeStickyFXParams, fHue,
									 pact->uiTimestampLungeAnimate, ePowerAnimFlags, PowerAnimFX_GetNodeSelectionType(pafx), 0);

				character_StickyFXOff(pchar, uiID, 0, kPowerAnimFXType_LungeSticky, pCharTarget,
									  pafx->ppchLungeStickyFX, pact->uiTimestampActivate,bGlobal);

				character_SendAnimKeywordOrFlag(pchar, uiID, 0, kPowerAnimFXType_LungeFlash, erSource, 
												pafx->pchAnimKeyword, pact, pact->uiTimestampLungeAnimate,
												false, false, false, true, true, false);
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
}

// Starts the grab state
S32 character_AnimFXGrab(int iPartitionIdx,
						 Character *pchar,
						 PowerActivation *pact)
{
	if(pact->eGrabMode==kGrabMode_Pending)
	{
		Entity *eTarget = entFromEntityRef(iPartitionIdx,pact->erTarget);
		PowerDef *pdef = GET_REF(pact->hdef);
		PowerAnimFX *pafx = pdef ? GET_REF(pdef->hFX) : NULL;
		if(eTarget && eTarget!=pchar->pEntParent && pafx && pafx->pGrab)
		{
#ifdef GAMESERVER
			int i;
			MovementRequester*	mrTarget;
			MRGrabConfig		c = {0};

			// Setup the config
			c.actorSource.er = entGetRef(pchar->pEntParent);
			for(i=0; i<eaSize(&pafx->pGrab->ppchSourceBits); i++)
				eaiPush(&c.actorSource.animBitHandles,mmGetAnimBitHandleByName(pafx->pGrab->ppchSourceBits[i], 0));

			c.actorTarget.er = pact->erTarget;
			c.actorTarget.flags.stopMoving = 1;
			for(i=0; i<eaSize(&pafx->pGrab->ppchTargetBits); i++)
				eaiPush(&c.actorTarget.animBitHandles,mmGetAnimBitHandleByName(pafx->pGrab->ppchTargetBits[i], 0));

			c.maxSecondsToReachTarget = pafx->pGrab->fTimeChase;
			c.distanceToStartHold = pafx->pGrab->fDistStart;
			c.distanceToHold = pafx->pGrab->fDistHold;
			c.secondsToHold = pafx->pGrab->iFramesHold/PAFX_FPS;

			// Destroy old source requester
			mrDestroy(&pchar->pEntParent->mm.mrGrab);

			// Create requesters and get their handles
			mrGrabCreate(pchar->pEntParent->mm.movement, &pchar->pEntParent->mm.mrGrab);
			mrGetHandleFG(pchar->pEntParent->mm.mrGrab, &c.actorSource.mrHandle);
			mrGrabCreate(eTarget->mm.movement, &mrTarget);
			mrGetHandleFG(mrTarget, &c.actorTarget.mrHandle);

			// Send config to source
			mrGrabSetConfig(pchar->pEntParent->mm.mrGrab, &c);

			// Send config to target
			c.flags.isTarget = 1;
			mrGrabSetConfig(mrTarget, &c);

			// Cleanup
			StructDeInit(parse_MRGrabConfig, &c);
#endif

			return true;
		}
	}

	return false;
}

// Returns the effective grab state
S32 character_GetAnimFXGrabState(Character *pchar)
{
	S32 iState = 0;
#if defined(GAMESERVER) || defined(GAMECLIENT)
	MRGrabStatus status;
	if(!mrGrabGetStatus(pchar->pEntParent->mm.mrGrab, &status)){
		iState = -1;
	}else{
		switch(status){
			xcase MR_GRAB_STATUS_DONE:{
				iState = -1;// Chase failed, lost hold, or no requester?
			}
			xcase MR_GRAB_STATUS_CHASE:{
				iState = 0;// Chasing
			}
			xcase MR_GRAB_STATUS_HOLDING:{
				iState = 1;// Holding
			}
		}
	}
#endif
	return iState;
}

// Starts the proper preactivate anims and fx from a character
void character_AnimFXPreactivateOn(	int iPartitionIdx,
									Character *pchar,
									PowerApplication *papp,
									PowerActivation *pact,
									Power *ppow,
									Character *pcharTarget,
									Vec3 vecTarget,
									U32 uiTime,
									U32 uiActID,
									U32 uiActSubID)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	static const char **s_ppchPreactivateBitsTemp = NULL;
	PowerAnimFX* pafx = NULL;
	F32 fHue = 0;
	PowerDef *pDef = pact ? GET_REF(pact->hdef) : NULL;

	PERFINFO_AUTO_START_FUNC();

	if(pact && pact->pRefAnimFXMain)
	{
		pafx = GET_REF(pact->pRefAnimFXMain->hFX);
		fHue = pact->pRefAnimFXMain->fHue;
	}
	else if (papp)
	{
		pafx = papp->pafx;
		fHue = papp->fHue;
	}

	if (pafx && pafx->bLocationActivate)
	{
		// Ignore the target character
		pcharTarget = NULL;
	}

	if(pafx)
	{
		int i, s = pact ? eaSize(&pact->ppRefAnimFXEnh) : 0;
		S32 bPeriodic = (pact && pact->uiPeriod);
		//S32 bAlreadyPlayed = (pact && pact->bPlayedActivate);
		S32 bOutOfRange = (pact && !pact->bRange);
		S32 bNoTarget = (pact && pact->erTarget==0 && !IS_HANDLE_ACTIVE(pact->hTargetObject));
		EntityRef erSource = entGetRef(pchar->pEntParent);
		int bGlobal = pafx->bGlobalFX;
		int bMiss = pcharTarget && (pafx->ppActivateMissFX || pafx->bActivateFXOffsetOnMiss) ? !combat_HitTest(iPartitionIdx,papp,pact,pchar,pcharTarget,papp?papp->ppow:NULL,false,NULL) : false;
		U32 uiTimeBits = pmTimestampFrom(uiTime,(bPeriodic?pafx->iFramesBeforePeriodicActivateBits:pafx->iFramesBeforeActivateBits)/PAFX_FPS);
		U32 uiTimeFX = pmTimestampFrom(uiTime,(bPeriodic?pafx->iFramesBeforePeriodicActivateFX:pafx->iFramesBeforeActivateFX)/PAFX_FPS);
		//float fCharged = pact ? pact->fTimeCharged : 0.0f;
		EntityRef erMoveTarget = pcharTarget && pcharTarget != pchar ? entGetRef(pcharTarget->pEntParent) : 0;
		DynAnimGraph *pAnimGraph = GET_REF(pafx->lurch.hMovementGraph);
		
		if (pact && pact->erProximityAssistTarget)
			erMoveTarget = pact->erProximityAssistTarget;

		// Don't start the lurch in the pre-activate phase if the power movement says so
		if (pAnimGraph == NULL || 
			pAnimGraph->pPowerMovementInfo == NULL || 
			pAnimGraph->pPowerMovementInfo->pDefaultMovement == NULL ||
			!pAnimGraph->pPowerMovementInfo->pDefaultMovement->bSkipPreActivationPhase)
		{
			character_MoveLurchStart(iPartitionIdx, pchar, uiActID, pact, pafx, erMoveTarget, vecTarget, uiTime);
		}		

		if(pact && !bPeriodic)
		{
			PowerDef *pdef = GET_REF(pact->hdef);

			if(pdef)
			{
				if(pdef->eType!=kPowerType_Maintained)
				{
					pmReleaseAnim(	pchar->pPowersMovement,
									pmTimestampFrom(uiTime,pdef->fTimePreactivate),
									uiActID,
									__FUNCTION__);
				}

				if(pafx->fSpeedPenaltyDuringPreactivate==1)
				{
					character_PowerActivationRootStart(pchar,uiActID,kPowerAnimFXType_PreactivateSticky,uiTime,pdef->pchName);
					character_PowerActivationRootStop(pchar,uiActID,kPowerAnimFXType_PreactivateSticky,pmTimestampFrom(uiTime,pdef->fTimePreactivate+0.2f));
				}
				else if(pafx->fSpeedPenaltyDuringPreactivate>0)
				{
					ASSERT_FALSE_AND_SET(pact->bSpeedPenaltyIsSet);
					mrSurfaceSpeedPenaltyStart(	pchar->pEntParent->mm.mrSurface,
												uiActID,
												pafx->fSpeedPenaltyDuringPreactivate,
												uiTime);
				}
			}
		}

		for(i=-1; i<s; i++)
		{
			if(pafx)
			{
				bool bBasic = true;

				EPowerAnimFXFlag ePowerAnimFlags = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
				
				if (bMiss)
					ePowerAnimFlags |= EPowerFXFlags_MISS;

				if (pafx->eReactTrigger==kAttackReactTrigger_FX)
					ePowerAnimFlags |= EPowerFXFlags_TRIGGER;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlags |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				if(pact)
				{
					character_StickyBitsOn(pchar,uiActID,i+1,kPowerAnimFXType_PreactivateSticky,erSource,pafx->ppchPreactivateStickyBits,uiTimeBits);
					character_StickyFXOn(iPartitionIdx,pchar,uiActID,i+1,kPowerAnimFXType_PreactivateSticky,
										pcharTarget,vecTarget,papp,pact,ppow,
										pafx->ppchPreactivateStickyFX,pafx->ppPreactivateStickyFXParams,fHue,uiTimeFX,
										ePowerAnimFlags, PowerAnimFX_GetNodeSelectionType(pafx), 0);
				}

				if(bBasic)
				{
					if (bPeriodic)
					{
						character_FlashBitsOn(pchar,uiActID,i+1,kPowerAnimFXType_PreactivateFlash,erSource,pafx->ppchPeriodicPreactivateBits,uiTimeBits,false,false,false,false);
					}
					else
					{
						if(bOutOfRange || bNoTarget)
						{
							eaCopy(&s_ppchPreactivateBitsTemp,&pafx->ppchPreactivateBits);
							if(bOutOfRange) eaPush(&s_ppchPreactivateBitsTemp,s_pchBitActivateOutOfRange);
							if(bNoTarget) eaPush(&s_ppchPreactivateBitsTemp,s_pchBitActivateNoTarget);
							character_FlashBitsOn(pchar,uiActID,i+1,kPowerAnimFXType_PreactivateFlash,erSource,s_ppchPreactivateBitsTemp,uiTimeBits,false,false,false,false);
						}
						else
						{
							character_FlashBitsOn(pchar,uiActID,i+1,kPowerAnimFXType_PreactivateFlash,erSource,pafx->ppchPreactivateBits,uiTimeBits,false,false,false,false);
						}
					}

					// Send the anim flag from activate
					//I don't think that this should be sent on periodic powers, but it's been running this way for awhile
					character_SendAnimKeywordOrFlag(pchar,
													uiActID,
													uiActSubID,
													kPowerAnimFXType_PreactivateFlash,
													erSource,
													pafx->pchPreactivateAnimFlag,
													pact,
													uiTimeBits,
													false,
													false,
													false,
													false,
													true,
													false);

					character_SendAnimKeywordOrFlag(pchar,
													uiActID,
													uiActSubID,
													kPowerAnimFXType_PreactivateFlash,
													erSource,
													pafx->pchAnimKeyword,
													pact,
													uiTimeBits,
													false,
													false,
													false,
													true,
													true,
													false);

					character_FlashActivateFX(	iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_PreactivateFlash,
												pcharTarget,vecTarget,papp,pact,ppow,
												pafx->ppPreactivateFX,pafx->ppPreactivateFXParams,fHue,uiTimeFX,
												ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));

				}
			}

			if(i+1<s)
			{
				pafx = GET_REF(pact->ppRefAnimFXEnh[i+1]->hFX);
				fHue = pact->ppRefAnimFXEnh[i+1]->fHue;
			}
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

// Stops the sticky activate bits and fx
void character_AnimFXPreactivateOff(int iPartitionIdx,
									Character *pchar,
									PowerActivation *pact,
									U32 uiTime, 
									bool bCancel)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	mrLog(pchar->pPowersMovement, NULL, "%s, spc %u", __FUNCTION__, uiTime);

	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);
		PowerDef* pdef = GET_REF(pact->hdef);

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				character_StickyBitsOff(pchar,uiID,i+1,kPowerAnimFXType_PreactivateSticky,erSource,pafx->ppchPreactivateStickyBits,uiTime);

				bGlobal |= (i<0 && pafx->bGlobalFX);
				character_StickyFXOff(pchar,uiID,i+1,kPowerAnimFXType_PreactivateSticky,NULL,pafx->ppchPreactivateStickyFX,uiTime,bGlobal);

				if(bCancel && i < 0)
				{
					if(pafx->fSpeedPenaltyDuringPreactivate==1)
						character_PowerActivationRootStop(pchar,uiID,kPowerAnimFXType_PreactivateSticky,uiTime);
					else if(TRUE_THEN_RESET(pact->bSpeedPenaltyIsSet))
						mrSurfaceSpeedPenaltyStop(pchar->pEntParent->mm.mrSurface,uiID,uiTime);
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
#endif
}

// void character_AnimFXPreactivate 
void character_AnimFXPreactivateCancel(	int iPartitionIdx,
										Character *pchar,
										PowerActivation *pact)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		U32 uiID = pact->uchID;
		U32 uiTime = pmTimestamp(0);
		EntityRef erSource = entGetRef(pchar->pEntParent);
				
		character_MoveCancel(pchar, uiID, 0);
				
		pmReleaseAnim(pchar->pPowersMovement, uiTime, uiID, __FUNCTION__);
		
		for (i=-1; i<s; i++) {
			character_BitsCancel(pchar,uiID,i+1,kPowerAnimFXType_PreactivateFlash,erSource);
			//I'm not cancelling the sticky bits here since charcter_AnimFXPreactivateOff is called immediately below & it turns those bits off
		}

		character_SendAnimKeywordOrFlag(pchar, uiID, 1, kPowerAnimFXType_ActivateFlash,
										erSource, s_pchFlagInterrupt, pact, uiTime,
										false,false,false,false,true,false);

		character_AnimFXPreactivateOff(iPartitionIdx, pchar, pact, uiTime, true);
	}
#endif
}

void character_AnimFxPowerActivationImmunity(	int iPartitionIdx, 
												Character *pchar,
												PowerActivation *pact)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (pchar->bPowerActivationImmunity && 
		!pact->bPlayedImmuneFX && 
		g_CombatConfig.pPowerActivationImmunities->ppchStickyFX)
	{
		character_StickyFXOn(	iPartitionIdx,
								pchar,
								pact->uchID, 0, 
								kPowerAnimFXType_ActivationImmunity,
								pchar, 
								NULL,
								NULL,
								pact,
								NULL,
								g_CombatConfig.pPowerActivationImmunities->ppchStickyFX,
								NULL,
								0.f,
								pact->uiTimestampActivate,
								0, 0, 0);

		pact->bPlayedImmuneFX = true;
	}
#endif
}

void character_AnimFxPowerActivationImmunityCancel(	int iPartitionIdx, 
													Character *pchar,
													PowerActivation *pact)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if (pact->bPlayedImmuneFX)
	{
		character_StickyFXOff(	pchar, 
								pact->uchID, 
								0, 
								kPowerAnimFXType_ActivationImmunity, 
								NULL, 
								g_CombatConfig.pPowerActivationImmunities->ppchStickyFX,
								pmTimestamp(0), 
								false);

		pact->bPlayedImmuneFX = false;
	}
#endif
}

static bool character_ShouldPerformActivationLurch(	SA_PARAM_OP_VALID PowerDef *pdef, 
													SA_PARAM_OP_VALID PowerActivation *pact, 
													SA_PARAM_NN_VALID PowerAnimFX* pafx)
{	
	if (!pdef)
		return true;

	if(pdef->fTimePreactivate == 0.0f)
	{
		if (pdef->eType != kPowerType_Maintained)
			return true;
		// making maintains only perform lurch on the first period. 
		return pact && pact->uiPeriod == 0;
	}
	else
	{	// had a preactivate, see if the animgraph told us to skip the preactivation lurch
		DynAnimGraph *pAnimGraph = GET_REF(pafx->lurch.hMovementGraph);
		bool bSkipPreActivationPhase = SAFE_MEMBER3(pAnimGraph, pPowerMovementInfo, pDefaultMovement, bSkipPreActivationPhase);
		if (bSkipPreActivationPhase)
		{
			if (pdef->eType != kPowerType_Maintained)
				return true;

			return pact && pact->uiPeriod == 0;
		}

		return false;
	}
}

// Starts the proper activate bits and fx from a character
void character_AnimFXActivateOn(int iPartitionIdx,
								Character *pchar,
								PowerApplication *papp,
								PowerActivation *pact,
								Power *ppow,
								Character *pcharTarget,
								Vec3 vecTarget,
								U32 uiTime,
								U32 uiActID,
								U32 uiActSubID,
								PowerFXHitType eHitType)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	static const char **s_ppchActivateBitsTemp = NULL;
	PowerAnimFX* pafx = NULL;
	F32 fHue = 0;
	U32 iSlot = 0;

	PERFINFO_AUTO_START_FUNC();

	if(pact && pact->pRefAnimFXMain)
	{
		pafx = GET_REF(pact->pRefAnimFXMain->hFX);
		fHue = pact->pRefAnimFXMain->fHue;
	}
	else if (papp)
	{
		pafx = papp->pafx;
		fHue = papp->fHue;
	}

	if (pafx && pafx->bLocationActivate)
	{
		// Ignore the target character
		pcharTarget = NULL;
	}

	if(pafx)
	{
		int i, s = pact ? eaSize(&pact->ppRefAnimFXEnh) : 0;
		S32 bPeriodic = (pact && pact->uiPeriod);
		S32 bAlreadyPlayed = (pact && pact->bPlayedActivate);
		S32 bOutOfRange = (pact && !pact->bRange);
		S32 bNoTarget = (pact && pact->erTarget==0 && !IS_HANDLE_ACTIVE(pact->hTargetObject));
		EntityRef erSource = entGetRef(pchar->pEntParent);
		int bGlobal = pafx->bGlobalFX;
		U32 uiTimeBits = pmTimestampFrom(uiTime,(bPeriodic?pafx->iFramesBeforePeriodicActivateBits:pafx->iFramesBeforeActivateBits)/PAFX_FPS);
		U32 uiTimeFX = pmTimestampFrom(uiTime,(bPeriodic?pafx->iFramesBeforePeriodicActivateFX:pafx->iFramesBeforeActivateFX)/PAFX_FPS);
		float fCharged = pact ? pact->fTimeCharged : 0.0f;
		EntityRef erMoveTarget = pcharTarget && pcharTarget != pchar ? entGetRef(pcharTarget->pEntParent) : 0;
		PowerDef *pdef = pact ? GET_REF(pact->hdef) : NULL;
		int bMiss = false;

		if (pcharTarget && (pafx->ppActivateMissFX || pafx->bActivateFXOffsetOnMiss))
		{
			if (eHitType == kPowerFXHitType_Unset || eHitType == kPowerFXHitType_UnsetEvalHitChanceWithoutPower)
			{
				bool bEvalHitChanceWithoutPower = (eHitType == kPowerFXHitType_UnsetEvalHitChanceWithoutPower);
				bMiss = !combat_HitTest(iPartitionIdx,papp,pact,pchar,pcharTarget,papp?papp->ppow:NULL,bEvalHitChanceWithoutPower,NULL);
			}
			else
			{
				bMiss = (eHitType == kPowerFXHitType_Miss);
			}
		}

		if (pact && pact->erProximityAssistTarget)
			erMoveTarget = pact->erProximityAssistTarget;

		if(character_ShouldPerformActivationLurch(pdef, pact, pafx))
		{
			character_MoveLurchStart(iPartitionIdx, pchar, uiActID, pact, pafx, erMoveTarget, vecTarget, uiTime);
		}

		if(pact && pafx->bPlayChargeDuringActivate)
		{
			character_AnimFXChargeOn(iPartitionIdx, pchar, pact, ppow, vecTarget);
			character_AnimFXChargeOff(iPartitionIdx, pchar, pact, uiTime,false);
		}

		if(pact && !bPeriodic)
		{
			if(pdef)
			{
				if(pdef->eType!=kPowerType_Maintained)
				{
					pmReleaseAnim(	pchar->pPowersMovement,
									pmTimestampFrom(uiTime,pdef->fTimeActivate),
									uiActID,
									__FUNCTION__);
				}

				if(pafx->fSpeedPenaltyDuringActivate==1)
				{
					character_PowerActivationRootStart(pchar,uiActID,kPowerAnimFXType_ActivateSticky,uiTime,pdef->pchName);
					// If it's not maintained, we know when it'll stop
					if(pdef->eType!=kPowerType_Maintained)
						character_PowerActivationRootStop(pchar,uiActID,kPowerAnimFXType_ActivateSticky,pmTimestampFrom(uiTime,pdef->fTimeActivate));
				}
				else if(pafx->fSpeedPenaltyDuringActivate>0)
				{
					ASSERT_FALSE_AND_SET(pact->bSpeedPenaltyIsSet);
					mrSurfaceSpeedPenaltyStart(	pchar->pEntParent->mm.mrSurface,
												uiActID,
												pafx->fSpeedPenaltyDuringActivate,
												uiTime);
				}
			}
			if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
			{
				TacticalDisableFlags flags = GetTacticalDisableFlags(pafx);
				U32 uiDisableTime = pact->uiTimestampLungeAnimate == 0 ? uiTime : pact->uiTimestampLungeAnimate;

				if (g_CombatConfig.tactical.bAimCancelsPowersBeforeHitFrame && pafx->piFramesBeforeHit)
				{
					if (pdef && !pdef->bDoNotAllowCancelBeforeHitFrame)
					{
						uiDisableTime = pmTimestampFrom(uiTime, *pafx->piFramesBeforeHit / PAFX_FPS);
					}
				}
				
				if(pdef->eType != kPowerType_Instant)
				{
					mrTacticalNotifyPowersStart(pchar->pEntParent->mm.mrTactical, uiActID, flags, uiDisableTime);
					
					// if this power has a timeOverride, set the tactical requester to be enabled at this time instead of at the end of activation
					if (pdef->fTimeOverride)
					{
						F32 fSecondsOffset = pdef->fTimeActivate - pdef->fTimeOverride;
						mrTacticalNotifyPowersStop(	pchar->pEntParent->mm.mrTactical, 
													uiActID, 
													pmTimestampFrom(uiDisableTime, fSecondsOffset));
					}
				}
			}
			
/*			if (pafx->bDisableFaceActivate)
			{
				pchar->bFaceSelectedIgnoreTargetSystem = true;
				pmUpdateSelectedTarget(pchar->pEntParent, false);
				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}*/

		}

		for(i=-1; i<s; i++)
		{
			if(pafx)
			{
				bool bBasic = true;

				EPowerAnimFXFlag ePowerAnimFlags = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
				if (pafx->eReactTrigger==kAttackReactTrigger_FX)
					ePowerAnimFlags |= EPowerFXFlags_TRIGGER;
				if (bMiss && pafx->bActivateFXOffsetOnMiss)
					ePowerAnimFlags |= EPowerFXFlags_MISS;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlags |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;
				if (pafx->bActivateFXNeedsPowerScales)
					ePowerAnimFlags |= EPowerAnimFXFlag_ADD_POWER_AREA_EFFECT_PARAMS;

				if(pact && !bAlreadyPlayed)
				{
					character_StickyBitsOn(pchar,uiActID,i+1,kPowerAnimFXType_ActivateSticky,erSource,pafx->ppchActivateStickyBits,uiTimeBits);
					character_StickyFXOn(iPartitionIdx,pchar,uiActID,i+1,kPowerAnimFXType_ActivateSticky,
								pcharTarget,vecTarget,papp,pact,ppow,
								pafx->ppchActivateStickyFX,pafx->ppActivateStickyFXParams,fHue,uiTimeFX,
								ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx), iSlot);
				}

				if(pafx->ppChargedActivate && fCharged>0)
				{
					int j;
					for(j=0; j<eaSize(&pafx->ppChargedActivate); j++)
					{
						PowerChargedActivate *pChargedAct = pafx->ppChargedActivate[j];
						if(pChargedAct->fTimeMax && pChargedAct->fTimeMax <= fCharged)
							continue;
						if(pChargedAct->fTimeMin && pChargedAct->fTimeMin > fCharged)
							continue;

						if(bOutOfRange || bNoTarget)
						{
							eaCopy(&s_ppchActivateBitsTemp,&pChargedAct->ppchActivateBits);
							if(bOutOfRange) eaPush(&s_ppchActivateBitsTemp,s_pchBitActivateOutOfRange);
							if(bNoTarget) eaPush(&s_ppchActivateBitsTemp,s_pchBitActivateNoTarget);
							character_FlashBitsOn(pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,erSource,s_ppchActivateBitsTemp,uiTimeBits,false,false,false,false);
						}
						else
						{
							character_FlashBitsOn(pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,erSource,pChargedAct->ppchActivateBits,uiTimeBits,false,false,false,false);
						}
						
						//send the activate flag, should only happen 1x due to continue statements above
						character_SendAnimKeywordOrFlag(pchar,
														uiActID,
														uiActSubID,
														kPowerAnimFXType_ActivateFlash,
														erSource,
														pafx->pchActivateAnimFlag,
														pact,
														uiTimeBits,
														false,
														false,
														false,
														false,
														false,
														false);
						//not sending the keyword here, since this should only be reachable by charged powers

						if(pChargedAct->ppchActivateMissFX && bMiss)
						{
							character_FlashFX(iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,
											pcharTarget,vecTarget,papp,pact,ppow,
											pChargedAct->ppchActivateMissFX,pChargedAct->ppActivateFXParams,fHue,uiTimeFX,
											ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));
						}
						else
						{
							character_FlashFX(iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,
											pcharTarget,vecTarget,papp,pact,ppow,
											pChargedAct->ppchActivateFX,pChargedAct->ppActivateFXParams,fHue,uiTimeFX,
											ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));
						}
						bBasic = false;
					}
				}

				if(bBasic)
				{
					if(bPeriodic)
					{
						character_FlashBitsOn(pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,erSource,pafx->ppchPeriodicActivateBits,uiTimeBits,false,false,false,false);
					}
					else
					{
						if(bOutOfRange || bNoTarget)
						{
							eaCopy(&s_ppchActivateBitsTemp,&pafx->ppchActivateBits);
							if(bOutOfRange) eaPush(&s_ppchActivateBitsTemp,"ACTIVATE_OUT_OF_RANGE");
							if(bNoTarget) eaPush(&s_ppchActivateBitsTemp,"ACTIVATE_NO_TARGET");
							character_FlashBitsOn(pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,erSource,s_ppchActivateBitsTemp,uiTimeBits,false,false,false,false);
						}
						else
						{
							character_FlashBitsOn(pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,erSource,pafx->ppchActivateBits,uiTimeBits,false,false,false,false);
						}
						// Send the anim flag from activate
						character_SendAnimKeywordOrFlag(pchar,
														uiActID,
														uiActSubID,
														kPowerAnimFXType_ActivateFlash,
														erSource,
														pafx->pchActivateAnimFlag,
														pact,
														uiTimeBits,
														false,
														false,
														false,
														false,
														true,
														false);
					}

					character_SendAnimKeywordOrFlag(pchar,
													uiActID,
													uiActSubID,
													kPowerAnimFXType_ActivateFlash,
													erSource,
													pafx->pchAnimKeyword,
													pact,
													uiTimeBits,
													false,
													false,
													false,
													true,
													true,
													false);

					if(bPeriodic)
					{
						character_FlashActivateFX(iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,
												pcharTarget,vecTarget,papp,pact,ppow,
												pafx->ppPeriodicActivateFX,pafx->ppPeriodicActivateFXParams,fHue,uiTimeFX,
												ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));
					}
					else
					{
						if(pafx->ppActivateMissFX && bMiss)
						{
							character_FlashActivateFX(iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,
													pcharTarget,vecTarget,papp,pact,ppow,
													pafx->ppActivateMissFX,pafx->ppActivateFXParams,fHue,uiTimeFX,
													ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));
						}
						else if(pact && pact->fDistToTarget >= 0.0f && pafx->fNearActivateFXDistance && pact->fDistToTarget < pafx->fNearActivateFXDistance)
						{
							character_FlashActivateFX(iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,
													pcharTarget,vecTarget,papp,pact,ppow,
													pafx->ppActivateNearFX,pafx->ppActivateFXParams,fHue,uiTimeFX,
													ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));
						}
						else
						{
							character_FlashActivateFX(iPartitionIdx,pchar,uiActID,uiActSubID,kPowerAnimFXType_ActivateFlash,
													pcharTarget,vecTarget,papp,pact,ppow,
													pafx->ppActivateFX,pafx->ppActivateFXParams,fHue,uiTimeFX,
													ePowerAnimFlags,PowerAnimFX_GetNodeSelectionType(pafx));
						}
					}
				}
			}

			if(i+1<s)
			{
				pafx = GET_REF(pact->ppRefAnimFXEnh[i+1]->hFX);
				iSlot = pact->ppRefAnimFXEnh[i+1]->uiSrcEquipSlot;
				fHue = pact->ppRefAnimFXEnh[i+1]->fHue;
			}
		}
	}

	if(pact)
	{
		pact->bPlayedActivate = true;

		if(pcharTarget==NULL)
		{
			pact->bActivateAtVecTarget = true;
		}
	}

	PERFINFO_AUTO_STOP();
#endif
}

// Starts the proper activate bits and fx from a location
void location_AnimFXActivateOn(Vec3 vecSource,
							   int iPartitionIdx,
							   PowerAnimFX *pafx,
							   Character *pcharTarget,
							   Vec3 vecTarget,
							   Vec3 quatTarget,
							   U32 uiTime,
							   U32 uiID)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	U32 uiTimeFX = pmTimestampFrom(uiTime,pafx->iFramesBeforeActivateFX/PAFX_FPS);
	location_FlashActivateFX(vecSource,iPartitionIdx,uiID,uiID,kPowerAnimFXType_ActivateFlash,
						pcharTarget,vecTarget,quatTarget,NULL,NULL,
						pafx->ppActivateFX,pafx->ppActivateFXParams,pafx->fDefaultHue,uiTimeFX,
						((pafx->eReactTrigger==kAttackReactTrigger_FX) ? EPowerFXFlags_TRIGGER: 0), 0);
#endif
}

// Stops the sticky hit fx on the PowerActivation, with optional override of the targets, so that
//  they can be shut off on specific targets in the middle of an activation.
void character_AnimFXHitStickyFXOff(int iPartitionIdx,
									Character *pchar,
									PowerActivation *pact,
									U32 uiTime,
									EntityRef *perTargets)
{
	if(!perTargets)
		perTargets = pact->perTargetsHit;

	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				bGlobal |= (i<0 && pafx->bGlobalFX);
				if(pafx->ppchHitStickyFX)
				{
					int j;
					for(j=eaiSize(&perTargets)-1; j>=0; j--)
					{
						Entity *be = entFromEntityRef(iPartitionIdx,perTargets[j]);
						Character *pcharTarget = be ? be->pChar : NULL;
						if(pcharTarget)
						{
							character_StickyFXOff(pcharTarget,uiID,0,kPowerAnimFXType_HitSticky,pchar,pafx->ppchHitStickyFX,uiTime,bGlobal);
						}
					}
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
}

// Cancels the sticky hit fx on the PowerActivation
static void CharacterAnimFXHitStickyFXCancel(int iPartitionIdx,
											 Character *pchar,
											 PowerActivation *pact)
{
	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				bGlobal |= (i<0 && pafx->bGlobalFX);
				if(pafx->ppchHitStickyFX)
				{
					int j;
					for(j=eaiSize(&pact->perTargetsHit)-1; j>=0; j--)
					{
						Entity *be = entFromEntityRef(iPartitionIdx,pact->perTargetsHit[j]);
						Character *pcharTarget = be ? be->pChar : NULL;
						if(pcharTarget)
						{
							character_StickyFXCancel(pcharTarget,pchar,uiID,0,kPowerAnimFXType_HitSticky,pafx->ppchHitStickyFX,bGlobal);
						}
					}
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
}



// Stops the sticky activate bits and fx
void character_AnimFXActivateOffEx(int iPartitionIdx,
									Character *pchar,
									PowerActivation *pact,
									U32 uiTime, 
									bool bKeepAnimsAndPenalties)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	mrLog(pchar->pPowersMovement, NULL, "%s, spc %u", __FUNCTION__, uiTime);

	character_AnimFXHitStickyFXOff(iPartitionIdx,pchar,pact,uiTime,NULL);

	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);
		PowerDef* pdef = GET_REF(pact->hdef);

		if(!bKeepAnimsAndPenalties && pdef && pdef->eType==kPowerType_Maintained)
		{
			pmReleaseAnim(pchar->pPowersMovement, uiTime, uiID, __FUNCTION__);
		}

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				bGlobal |= (i<0 && pafx->bGlobalFX);
				character_StickyBitsOff(pchar,uiID,i+1,kPowerAnimFXType_ActivateSticky,erSource,pafx->ppchActivateStickyBits,uiTime);
				character_StickyFXOff(pchar,uiID,i+1,kPowerAnimFXType_ActivateSticky,NULL,pafx->ppchActivateStickyFX,uiTime,bGlobal);

				if(!bKeepAnimsAndPenalties && i<0)
				{
					U32 delayedTime = pmTimestampFrom(uiTime, g_CombatConfig.fSpeedPenaltyRemoveDelay); // Delay removal of speed penalty if appropriate

					if(pafx->fSpeedPenaltyDuringActivate==1)
						character_PowerActivationRootStop(pchar,uiID,kPowerAnimFXType_ActivateSticky,uiTime);					
					else if(TRUE_THEN_RESET(pact->bSpeedPenaltyIsSet))
						mrSurfaceSpeedPenaltyStop(pchar->pEntParent->mm.mrSurface,uiID,delayedTime);
					
					if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
						mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical,uiID,uiTime);

					/*					
					if(pafx->bDisableFaceActivate)
					{
						pchar->bFaceSelectedIgnoreTargetSystem = false;
						pmUpdateSelectedTarget(pchar->pEntParent, false);
						entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
					}*/
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
#endif
}

void character_AnimFXActivateCancel(int iPartitionIdx,
								    Character *pchar,
									PowerActivation *pact,
									bool bInterrupted,
									bool bDoNotCancelFlashFX)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	CharacterAnimFXHitStickyFXCancel(iPartitionIdx,pchar,pact);

	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);

		pmReleaseAnim(pchar->pPowersMovement, pmTimestamp(0), uiID, __FUNCTION__);

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				if(pafx->bPlayChargeDuringActivate)
					character_AnimFXChargeCancel(iPartitionIdx,pchar,pact,false);

				bGlobal |= (i<0 && pafx->bGlobalFX);
				character_BitsCancel(pchar,uiID,i+1,kPowerAnimFXType_ActivateFlash,erSource);
				character_BitsCancel(pchar,uiID,i+1,kPowerAnimFXType_ActivateSticky,erSource);
				
				if (!bDoNotCancelFlashFX)
					character_FlashActivateFXCancel(pchar,uiID,i+1,kPowerAnimFXType_ActivateFlash,NULL,pafx->ppActivateFX,bGlobal);

				character_StickyFXCancel(pchar,NULL,uiID,i+1,kPowerAnimFXType_ActivateSticky,pafx->ppchActivateStickyFX,bGlobal);

				if(i<0)
				{
					if (bInterrupted)
						character_SendAnimKeywordOrFlag(pchar,uiID,1,kPowerAnimFXType_ActivateFlash,erSource,s_pchFlagInterrupt,pact,pmTimestamp(0),false,false,false,false,true,false);

					if(pafx->fSpeedPenaltyDuringActivate==1)
						character_PowerActivationRootCancel(pchar,uiID,kPowerAnimFXType_ActivateSticky);
					else if(TRUE_THEN_RESET(pact->bSpeedPenaltyIsSet))
							mrSurfaceSpeedPenaltyStop(pchar->pEntParent->mm.mrSurface,uiID,pmTimestamp(0));
					
					if(g_CombatConfig.tactical.bTacticalDisableDuringPowers)
						mrTacticalNotifyPowersStop(pchar->pEntParent->mm.mrTactical,uiID,pmTimestamp(0));
					
					/*
					if(pafx->bDisableFaceActivate)
					{
						pchar->bFaceSelectedIgnoreTargetSystem = false;
						pmUpdateSelectedTarget(pchar->pEntParent, false);
						entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
					}
					*/
				}
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}

		character_MoveCancel(pchar,uiID,0);
	}
#endif
}

// Cancels the most recent periodic activate bits and fx
void character_AnimFXActivatePeriodicCancel(Character *pchar,
											PowerActivation *pact,
											Character *pcharTarget)
{
	if(pact->pRefAnimFXMain)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		U32 uiSubID = pact->uiPeriod;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);

		// Clear the target if we shot at a vec
		if(pact->bActivateAtVecTarget) pcharTarget = NULL;

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				bGlobal |= (i<0 && pafx->bGlobalFX);
				character_BitsCancel(pchar,uiID,uiSubID,kPowerAnimFXType_ActivateFlash,erSource);
				character_FlashActivateFXCancel(pchar,uiID,uiSubID,kPowerAnimFXType_ActivateFlash,pcharTarget,pafx->ppActivateFX,bGlobal);
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
}

// Starts the deactivate bits and fx
void character_AnimFXDeactivate(int iPartitionIdx,
							    Character *pchar,
								PowerActivation *pact,
								U32 uiTime)
{
	if(pact->pRefAnimFXMain && !pact->bPlayedDeactivate)
	{
		int i, s = eaSize(&pact->ppRefAnimFXEnh);
		PowerAnimFXRef *pref = pact->pRefAnimFXMain;
		U32 uiID = pact->uchID;
		int bGlobal = false;
		EntityRef erSource = entGetRef(pchar->pEntParent);

		pact->bPlayedDeactivate = true;

		for(i=-1; i<s; i++)
		{
			PowerAnimFX *pafx = GET_REF(pref->hFX);
			if(pafx)
			{
				F32 fHue = pref->fHue;
				EPowerAnimFXFlag ePowerAnimFlag;
				bGlobal |= (i<0 && pafx->bGlobalFX);
				
				ePowerAnimFlag = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				character_FlashBitsOn(pchar,uiID,0,kPowerAnimFXType_Deactivate,erSource,pafx->ppchDeactivateBits,uiTime,false,false,false,false);
				character_FlashFX(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_Deactivate,
								pchar,NULL,NULL,pact,NULL,
								pafx->ppchDeactivateFX,pafx->ppDeactivateFXParams,fHue,uiTime,
								ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx));
				character_SendAnimKeywordOrFlag(pchar,uiID,0,kPowerAnimFXType_Deactivate,erSource,pafx->pchDeactivateAnimFlag,pact,uiTime,false,false,false,false,true,false);
			}

			if(i+1<s)
			{
				pref = pact->ppRefAnimFXEnh[i+1];
			}
		}
	}
}



// Starts the targeted bits and fx
void character_AnimFXTargeted(int iPartitionIdx,
							  Character *pcharTarget,
							  Vec3 vecTargetHit,
							  PowerApplication *papp)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PERFINFO_AUTO_START_FUNC();
	if(papp->pafx)
	{
		int i, s = eaSize(&papp->ppafxEnhancements);
		PowerAnimFX *pafx = papp->pafx;
		F32 fHue = papp->fHue;
		EntityRef erSource = papp->pcharSource ? entGetRef(papp->pcharSource->pEntParent) : 0;
		F32 fDelay = MAX(0.0f, ((papp->uiPeriod ? pafx->iFramesBeforePeriodicTargeted : pafx->iFramesBeforeTargeted)-papp->iFramesBeforeHitAdjust) /PAFX_FPS);
		U32 uiTime = pmTimestampFrom(papp->uiTimestampAnim,fDelay);
		int bGlobal = pafx->bGlobalFX;
		
		for(i=-1; i<s; i++)
		{
			if(papp->bPrimaryTarget || !pafx->bMainTargetOnly)
			{
				EPowerAnimFXFlag ePowerAnimFlag;
				
				ePowerAnimFlag = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
				if (pafx->bCapsuleHit)
					ePowerAnimFlag |= EPowerFXFlags_FROM_SOURCE_VEC;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				if(!character_IgnoresExternalAnimBits(pcharTarget, erSource))
				{
					character_FlashBitsOn(pcharTarget,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Targeted,erSource,pafx->ppchTargetedBits,uiTime,false,false,false,false);
				}
				//character_SendAnimKeywordOrFlag(pcharTarget,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Targeted,erSource,pafx->pchAnimKeyword,NULL,uiTime,false,false,false,true,true,false);
				character_FlashFXEx(iPartitionIdx,pcharTarget,vecTargetHit,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Targeted,
							papp->pcharSource,papp->vecSourcePos,papp,papp->pact,papp->ppow,
							pafx->ppchTargetedFX,pafx->ppTargetedFXParams,fHue,uiTime,
							ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx));
			}

			if(i+1<s)
			{
				pafx = papp->ppafxEnhancements[i+1]->pafx;
				fHue = papp->ppafxEnhancements[i+1]->fHue;
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

static bool bDisableHitPause;
AUTO_CMD_INT(bDisableHitPause, danimDisableHitPause) ACMD_CATEGORY(Debug)  ACMD_CATEGORY(dynAnimation);

static bool bDisablePlayerHitReacts;
AUTO_CMD_INT(bDisablePlayerHitReacts, disablePlayerHitReacts) ACMD_CATEGORY(dynAnimation) ACMD_ACCESSLEVEL(7) ACMD_COMMANDLINE;

// Starts the hit bits and fx
void character_AnimFXHit(int iPartitionIdx,
						 Character *pcharTarget,
						 Vec3 vecTargetHit,
						 PowerApplication *papp,
						 F32 fDelayProjectile,
						 U32 bBits,
						 U32 *bAppliedHitPause)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PERFINFO_AUTO_START_FUNC();

	if (bDisablePlayerHitReacts && pcharTarget && entIsPlayer(pcharTarget->pEntParent))
	{
		return;
	}

	// TODO(JW): AnimFX: Make sure we don't run into an issue with multiple hits on activations
	//  with really high activation count
	if(papp->pafx && verify(papp->uiActSubID<(1<<PAFX_HITFX_SHIFT)))
	{
		int i, s = eaSize(&papp->ppafxEnhancements);
		PowerAnimFX *pafx = papp->pafx;
		F32 fHue = papp->fHue;
		EntityRef erSource = papp->pcharSource && !IS_HANDLE_ACTIVE(papp->pcharSource->pEntParent->hCreatorNode) ? entGetRef(papp->pcharSource->pEntParent) : 0;
		int *piFramesBeforeHit = papp->uiActSubID ? pafx->piFramesBeforePeriodicHit : pafx->piFramesBeforeHit;
		int bGlobal = pafx->bGlobalFX;
		S32 iFramesAdjust = papp->iFramesBeforeHitAdjust;
		S32 bTrigger = pafx->eReactTrigger!=kAttackReactTrigger_Time;
		S32 bTriggerIsEntityID = pafx->eReactTrigger==kAttackReactTrigger_Bits;
		S32 bTriggerMultiHit = (eaiSize(&piFramesBeforeHit) > 1);
		EPowerAnimFXFlag eGlobalAnimFlag;

		if(!eaiSize(&piFramesBeforeHit))
		{
			piFramesBeforeHit = s_piFramesBeforeHitZero;
		}

		eGlobalAnimFlag = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
		if (bTrigger)
			eGlobalAnimFlag |= EPowerFXFlags_TRIGGER;
		if (bTriggerIsEntityID)
			eGlobalAnimFlag |= EPowerFXFlags_TRIGGER_IS_ENTITY_ID;
		if (bTriggerMultiHit)
			eGlobalAnimFlag |= EPowerFXFlags_TRIGGER_MULTI_HIT;

		
#ifdef GAMECLIENT
		gclCombatAdvantage_ReportHitForTarget(pcharTarget, papp, pafx);
#endif

		for(i=-1; i<s; i++)
		{
			if(papp->bPrimaryTarget || !pafx->bMainTargetOnly)
			{
				int j;
				for(j=eaiSize(&piFramesBeforeHit)-1; j>=0; j--)
				{
					F32 fDelay = MAX(0.0f, (piFramesBeforeHit[j]-iFramesAdjust)/PAFX_FPS);
					U32 uiTime = pmTimestampFrom(papp->uiTimestampAnim,fDelay+fDelayProjectile);
					EPowerAnimFXFlag ePowerAnimFlag;

					if (gConf.bNewAnimationSystem &&
						!bDisableHitPause &&
						!*bAppliedHitPause &&
						j == 0 &&
						papp->pcharSource &&
						pafx->pchHitAnimKeyword &&
						SAFE_MEMBER(papp->pdef,eType) == kPowerType_Click)
					{
						U32 uiTimeHitPause;
						if (papp->pact) {
							//this is when the activate keyword is sent on the character performing the hit
							uiTimeHitPause = pmTimestampFrom(papp->uiTimestampAnim,(papp->pact->uiPeriod?pafx->iFramesBeforePeriodicActivateBits:pafx->iFramesBeforeActivateBits)/PAFX_FPS);
						} else {
							//this is when the mob is hit based on the 1st listed frame before hit
							uiTimeHitPause = uiTime;
						}
						character_SendAnimKeywordOrFlag(papp->pcharSource,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),kPowerAnimFXType_None,erSource,s_pchFlagHitPause,NULL,uiTimeHitPause,false,false,false,false,true,false);
						*bAppliedHitPause = true;
					}

					if(bTrigger)
					{
						uiTime = papp->uiTimestampAnim;
					}

					if(bBits)
					{
						character_FlashBitsOn(pcharTarget,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),kPowerAnimFXType_HitFlash,erSource,pafx->ppchHitBits,uiTime,bTrigger,bTriggerIsEntityID,bTriggerMultiHit,false);						
						character_SendAnimKeywordOrFlag(pcharTarget,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),kPowerAnimFXType_HitFlash,erSource,pafx->pchHitAnimKeyword,NULL,uiTime,bTrigger,bTriggerIsEntityID,bTriggerMultiHit,true,false,false);

						if (gConf.bNewAnimationSystem && !bTrigger)
						{
							if (papp->pdef->eEffectArea == kEffectArea_Sphere)
							{
								//if a spherical attack is used, set the hit react's directional flag to point outward from the epicenter of the attack
								//this is used so all of the hit critters don't play the same hit react direction in unison regardless of where they are standing
								Vec3 vWSDirection;
								F32 fDirMag;

								if (fabs(papp->vecTarget[0]) > 0 ||
									fabs(papp->vecTarget[1]) > 0 ||
									fabs(papp->vecTarget[2]) > 0 )
								{
									//should only be used for special cases, such as (i) no entity/object or (ii) special combat position
									subVec3(vecTargetHit, papp->vecTarget, vWSDirection);
								} else {
									subVec3(vecTargetHit, papp->vecTargetEff, vWSDirection);
								}
								fDirMag = normalVec3(vWSDirection);

								if (fabs(vWSDirection[1]) < 0.99f			&&
									pafx->eHitDirection != PADID_Default	&&
									papp->pcharSource						&&
									papp->pcharSource->pEntParent)
								{
									Mat3 mFS;
									Quat qFS;

									copyVec3(vWSDirection, mFS[2]);
									crossVec3(upvec,  mFS[2], mFS[0]);
									crossVec3(mFS[2], mFS[0], mFS[1]);
									normalVec3(mFS[0]);
									normalVec3(mFS[1]);
									mat3ToQuat(mFS, qFS);

									quatRotateVec3Inline(qFS, powerAnimImpactDirection[pafx->eHitDirection], vWSDirection);
									fDirMag = normalVec3(vWSDirection);
								}

								if (fDirMag > 0.1f)
								{
									const char *pcHitDirectionFlag;
									Mat3 matFaceSpace;

									entGetFaceSpaceMat3(pcharTarget->pEntParent, matFaceSpace);
									pcHitDirectionFlag = dtCalculateHitReactDirectionBit(matFaceSpace, vWSDirection);
										
									//using the kPowerAnimFXType_HitFlag here so that we won't bits compare equal with the kPowerAnimFXType_HitFlash
									//also forcing as a detail flag since the hit react keywords are sent as a detail
									character_SendAnimKeywordOrFlag(pcharTarget,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),kPowerAnimFXType_HitFlag,erSource,pcHitDirectionFlag,NULL,uiTime,bTrigger,bTriggerIsEntityID,bTriggerMultiHit,false,false,true);
								}
							}
							else if(pafx->eHitDirection != PADID_Default &&
									papp->pcharSource &&
									papp->pcharSource->pEntParent)
							{
								//apply the power art's direction when a trigger hasn't been set
								Vec3 vWSDirection;	
								Quat qSourceFS;

								entGetFaceSpaceQuat(papp->pcharSource->pEntParent, qSourceFS);
								quatRotateVec3Inline(qSourceFS, powerAnimImpactDirection[pafx->eHitDirection], vWSDirection);

								if (normalVec3(vWSDirection) > 0.1f)
								{
									const char *pcHitDirectionFlag;
									Mat3 matFaceSpace;

									entGetFaceSpaceMat3(pcharTarget->pEntParent, matFaceSpace);
									pcHitDirectionFlag = dtCalculateHitReactDirectionBit(matFaceSpace, vWSDirection);

									//using the kPowerAnimFXType_HitFlag here so that we won't bits compare equal with the kPowerAnimFXType_HitFlash
									//also forcing as a detail flag since the hit react keywords are sent as a detail
									character_SendAnimKeywordOrFlag(pcharTarget,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),kPowerAnimFXType_HitFlag,erSource,pcHitDirectionFlag,NULL,uiTime,bTrigger,bTriggerIsEntityID,bTriggerMultiHit,false,false,true);
								}
							}
						}
					}

					ePowerAnimFlag = eGlobalAnimFlag | EPowerFXFlags_DO_NOT_TRACK_FLASHED;
					if (pafx->bCapsuleHit)
						ePowerAnimFlag |= EPowerFXFlags_FROM_SOURCE_VEC;
					if (pafx->bAlwaysChooseSameNode)
						ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;
					
					character_FlashFXEx(iPartitionIdx,pcharTarget,vecTargetHit,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),
							kPowerAnimFXType_HitFlash,papp->pcharSource,papp->vecSourcePos,papp,papp->pact,papp->ppow,
							pafx->ppchHitFX,pafx->ppHitFXParams,fHue,uiTime,
							ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx));

					if(0)
					{
						U32 uiTimeDead = pmTimestampFrom(papp->uiTimestampAnim,2.f*(fDelay+fDelayProjectile));
						character_FlashHitReact(iPartitionIdx,pcharTarget,papp->uiActID,papp->uiActSubID|(j<<PAFX_HITFX_SHIFT),papp->pcharSource,papp->pact,bBits?pafx->ppchHitBits:NULL,pafx->ppchHitFX,pafx->ppHitFXParams,fHue,uiTimeDead,bGlobal);
					}
				}

				if(!papp->uiActSubID && papp->pact)
				{
					F32 fDelay = MAX(0.0f, (piFramesBeforeHit[0]-iFramesAdjust)/PAFX_FPS);
					U32 uiTime = pmTimestampFrom(papp->uiTimestampAnim,fDelay+fDelayProjectile);
					EPowerAnimFXFlag ePowerAnimFlag;

					ePowerAnimFlag = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;

					if (pafx->bAlwaysChooseSameNode)
						ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

					character_StickyFXOn(iPartitionIdx,pcharTarget,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_HitSticky,
									papp->pcharSource,NULL,papp,papp->pact,papp->ppow,pafx->ppchHitStickyFX,
									pafx->ppHitStickyFXParams,fHue,uiTime,
									ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx), 0);
				}
			}

			if(i+1<s)
			{
				pafx = papp->ppafxEnhancements[i+1]->pafx;
				fHue = papp->ppafxEnhancements[i+1]->fHue;
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

// Starts the block bits and fx
void character_AnimFXBlock(int iPartitionIdx,
						   Character *pcharTarget,
						   Vec3 vecTargetHit,
						   PowerApplication *papp,
						   F32 fDelayProjectile)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	PERFINFO_AUTO_START_FUNC();
	if(papp->pafx)
	{
		int i, s = eaSize(&papp->ppafxEnhancements);
		PowerAnimFX *pafx = papp->pafx;
		F32 fHue = papp->fHue;
		EntityRef erSource = papp->pcharSource ? entGetRef(papp->pcharSource->pEntParent) : 0;
		F32 fDelay = MAX(0.0f, ((papp->uiPeriod ? pafx->iFramesBeforePeriodicBlock : pafx->iFramesBeforeBlock) - papp->iFramesBeforeHitAdjust)/PAFX_FPS);
		U32 uiTime = pmTimestampFrom(papp->uiTimestampAnim,fDelay+fDelayProjectile);
		int bGlobal = pafx->bGlobalFX;
		S32 bTrigger = pafx->eReactTrigger!=kAttackReactTrigger_Time;
		S32 bTriggerIsEntityID = pafx->eReactTrigger==kAttackReactTrigger_Bits;
		EPowerAnimFXFlag eGlobalAnimFlag;

		eGlobalAnimFlag = bGlobal ? EPowerAnimFXFlag_GLOBAL : 0;
		if (bTrigger)
			eGlobalAnimFlag |= EPowerFXFlags_TRIGGER;
		if (bTriggerIsEntityID)
			eGlobalAnimFlag |= EPowerFXFlags_TRIGGER_IS_ENTITY_ID;

		for(i=-1; i<s; i++)
		{
			if(papp->bPrimaryTarget || !pafx->bMainTargetOnly)
			{
				EPowerAnimFXFlag ePowerAnimFlag;

				if(!character_IgnoresExternalAnimBits(pcharTarget, erSource))
				{
					character_FlashBitsOn(pcharTarget,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Block,erSource,pafx->ppchBlockBits,uiTime,bTrigger,bTriggerIsEntityID,false,false);
					character_SendAnimKeywordOrFlag(pcharTarget,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Block,erSource,pafx->pchBlockAnimKeyword,NULL,uiTime,bTrigger,bTriggerIsEntityID,false,true,false,false);
				}

				ePowerAnimFlag = eGlobalAnimFlag | EPowerFXFlags_DO_NOT_TRACK_FLASHED;
							 	
				if (pafx->bCapsuleHit)
					ePowerAnimFlag |= EPowerFXFlags_FROM_SOURCE_VEC;
				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				character_FlashFXEx(iPartitionIdx,pcharTarget,vecTargetHit,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Block,
								papp->pcharSource,papp->vecSourcePos,papp,papp->pact,papp->ppow,pafx->ppchBlockFX,
								pafx->ppBlockFXParams,fHue,uiTime,
								ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx));
			}

			if(i+1<s)
			{
				pafx = papp->ppafxEnhancements[i+1]->pafx;
				fHue = papp->ppafxEnhancements[i+1]->fHue;
			}
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}



// Starts the targeted and hit fx at the given location
void location_AnimFXHit(Vec3 vecTarget,
						PowerApplication *papp,
						F32 fDelayHit)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(papp->pafx)
	{
		int i, s = eaSize(&papp->ppafxEnhancements);
		PowerAnimFX *pafx = papp->pafx;
		F32 fHue = papp->fHue;
		EntityRef erSource = papp->pcharSource ? entGetRef(papp->pcharSource->pEntParent) : 0;
		F32 fDelayTargeted = MAX(0.0f, ((papp->uiPeriod ? pafx->iFramesBeforePeriodicTargeted : pafx->iFramesBeforeTargeted)-papp->iFramesBeforeHitAdjust)/PAFX_FPS);

		for(i=-1; i<s; i++)
		{
			EPowerAnimFXFlag ePowerAnimFlag = 0;

			if (pafx->bAlwaysChooseSameNode)
				ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

			location_FlashFX(vecTarget,papp->iPartitionIdx,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_Targeted,
							papp->pcharSource,papp->vecSourcePos,papp->pact,papp->ppow,pafx->ppchTargetedFX,
							pafx->ppTargetedFXParams,fHue,pmTimestampFrom(papp->uiTimestampAnim,fDelayTargeted),
							ePowerAnimFlag);

			if (pafx->eReactTrigger == kAttackReactTrigger_FX)
				ePowerAnimFlag |= EPowerFXFlags_TRIGGER;

			location_FlashFX(vecTarget,papp->iPartitionIdx,papp->uiActID,papp->uiActSubID,kPowerAnimFXType_HitFlash,
							papp->pcharSource,papp->vecSourcePos,papp->pact,papp->ppow,pafx->ppchHitFX,
							pafx->ppHitFXParams,fHue,pmTimestampFrom(papp->uiTimestampAnim,fDelayHit),
							ePowerAnimFlag);

			if(i+1<s)
			{
				pafx = papp->ppafxEnhancements[i+1]->pafx;
				fHue = papp->ppafxEnhancements[i+1]->fHue;
			}
		}
	}
#endif
}

// Starts the death bits and fx
void character_AnimFXDeath(int iPartitionIdx,
						   Character *pcharTarget,
						   Character *pcharSource,
						   PowerAnimFX *pafx,
						   U32 uiTime)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	EntityRef erSource = pcharSource ? entGetRef(pcharSource->pEntParent) : 0;
	EPowerAnimFXFlag ePowerAnimFlag = 0;

	// TODO(JW): Support per-power hue for DeathFX
	if(!character_IgnoresExternalAnimBits(pcharTarget, erSource))
	{
		if(!gConf.bNewAnimationSystem){
			character_FlashBitsOn(pcharTarget,0,0,kPowerAnimFXType_Death,erSource,pafx->ppchDeathBits,uiTime,false,false,false,false);
		}else{
			MovementRequester *mr;
			const char *pcDir = NULL;

			//determine the direction
			if (pcharSource &&
				pafx->eDeathDirection != PADID_Default)
			{
				Vec3 vToTargetWS, vUpVectorWS, vImpactDir;

				//compute the target vector
				{
					Vec3 vSrcPos, vTgtPos;
					entGetCombatPosDir(pcharSource->pEntParent, NULL, vSrcPos, NULL);
					entGetCombatPosDir(pcharTarget->pEntParent, NULL, vTgtPos, NULL);
					subVec3(vTgtPos, vSrcPos, vToTargetWS);
					normalVec3(vToTargetWS);
				}

				//compute the up vector
				{
					Quat qSrcRot;
					entGetRot(pcharSource->pEntParent, qSrcRot);
					quatRotateVec3Inline(qSrcRot, upvec, vUpVectorWS);

					// Make sure vToTarget is not colinear w/ vUpVector
					if (fabsf(dotVec3(vUpVectorWS, vToTargetWS)) > 0.9999f)
					{
						// for now just snap the vToTargetWS to something else
						vToTargetWS[0] = vUpVectorWS[1];
						vToTargetWS[1] = vUpVectorWS[2];
						vToTargetWS[2] = vUpVectorWS[0];
					}
				}

				//compute the impact direction
				{
					Mat3 mFaceSpace;
					Quat qFaceSpace;
					orientMat3ToNormalAndForward(mFaceSpace, vUpVectorWS, vToTargetWS);
					mat3ToQuat(mFaceSpace, qFaceSpace);
					quatRotateVec3Inline(qFaceSpace, powerAnimImpactDirection[pafx->eDeathDirection], vImpactDir);
				}

				//compute & set the direction bit
				{
					Mat3 mFaceSpace;
					entGetFaceSpaceMat3(pcharTarget->pEntParent, mFaceSpace);
					pcDir = dtCalculateHitReactDirectionBit(mFaceSpace, vImpactDir);
				}
			}

			if (mmRequesterGetByNameFG(pcharTarget->pEntParent->mm.movement, "RagdollMovement", &mr))
			{
				mrRagdollSetDeathDirection(	mr,
											pcDir);
			} else {
				mrDeadSetDirection(	pcharTarget->pEntParent->mm.mrDead,
									pcDir);
				mrDeadAddStanceNamesIfDead(	pcharTarget->pEntParent->mm.mrDead,
											pafx->ppchDeathAnimStanceWords);
			}
		}
	}

	ePowerAnimFlag = EPowerFXFlags_DO_NOT_TRACK_FLASHED;
	if (pafx->bGlobalFX)
		ePowerAnimFlag |= EPowerAnimFXFlag_GLOBAL;
	if (pafx->bCapsuleHit)
		ePowerAnimFlag |= EPowerFXFlags_FROM_SOURCE_VEC;

	character_FlashFXEx(iPartitionIdx,pcharTarget,NULL,0,0,kPowerAnimFXType_Death,
					pcharSource,NULL,NULL,NULL,NULL,pafx->ppchDeathFX,pafx->ppDeathFXParams,pafx->fDefaultHue,uiTime,
					ePowerAnimFlag, 0);
#endif
}



// Special

// Starts the custom carry bits/fx, based on the data in the AttribModDef
void character_AnimFXCarryOn(int iPartitionIdx,
							 Character *pchar,
							 AttribModDef *pdef,
							 U32 uiID)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	static const char **s_ppchBits = NULL;
	WorldInteractionNode *pnode = GET_REF(pchar->hHeldNode);
	if(pnode && !eaSize(&pchar->ppchHeldFXNames))
	{
		U32 uiTime = pmTimestamp(0);
		EntityRef er = entGetRef(pchar->pEntParent);
		const char *cpchBit = worldInteractionGetCarryAnimationBitName(REF_STRING_FROM_HANDLE(pchar->hHeldNode));

		eaCopy(&s_ppchBits,&pdef->ppchContinuingBits);
		if(cpchBit)
		{
			eaPush(&s_ppchBits,allocAddString(cpchBit));
		}
		character_StickyBitsOn(pchar,uiID,0,kPowerAnimFXType_Carry,er,s_ppchBits,uiTime);
		
		character_StickyFXOn(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_Carry,
						pchar,NULL,NULL,NULL,NULL,pdef->ppchContinuingFX,pdef->ppContinuingFXParams,0,uiTime,0,0, 0);

		eaCopy(&pchar->ppchHeldFXNames,&pdef->ppchContinuingFX);
	}
#endif
}

// Stops the custom carry bits/fx (actually a cancel for ease of use)
void character_AnimFXCarryOff(Character *pchar)
{
	EntityRef er = entGetRef(pchar->pEntParent);
	character_BitsCancel(pchar,0,0,kPowerAnimFXType_Carry,er);
	character_StickyFXCancel(pchar,pchar,0,0,kPowerAnimFXType_Carry,pchar->ppchHeldFXNames,false);
	eaDestroy(&pchar->ppchHeldFXNames);
}



// Sets the Character's default stance to the given PowerDef.  If null, the
//  Character's default stance is cleared.
void character_SetDefaultStance(int iPartitionIdx,
								Character *pchar,
								PowerDef *pdef)
{
#if !(GAMESERVER || GAMECLIENT)
	assert(0);
#else
	if(pdef!=GET_REF(pchar->hPowerDefStanceDefault))
	{
		// Get the stance we're currently in, and check if it matches the current default stance
		PowerDef *pdefStanceCurrent = pchar->pPowerRefStance ? GET_REF(pchar->pPowerRefStance->hdef) : NULL;
		S32 bInDefaultStance = pdefStanceCurrent==GET_REF(pchar->hPowerDefStanceDefault);
		
		REMOVE_HANDLE(pchar->hPowerDefStanceDefault);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		if(pdef)
		{
			SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdef,pchar->hPowerDefStanceDefault);
		}

		// Enter the default stance if we were in the default stance before this change
		if(bInDefaultStance)
		{
			character_EnterStance(iPartitionIdx,pchar,NULL,NULL,true,pmTimestamp(0));
		}
	}
#endif
}

// Puts the character into the stance of the given power.  If bReplace is true
//  the power will be considered the base stance of the character.  Returns
//  true if the power is placing the character into a new stance
int character_EnterStance(int iPartitionIdx,
						  Character *pchar,
						  Power *ppow,
						  PowerActivation *pact,
						  int bReplace,
						  U32 uiTime)
{
	int bFirstInStance = false;
	int bEnterStance = false;
	int bNewIsDefaultStance = false;
	PowerDef *pdefNew = ppow ? GET_REF(ppow->hDef) : NULL;
	PowerDef *pdefOld = pchar->pPowerRefStance ? GET_REF(pchar->pPowerRefStance->hdef) : NULL;
	PowerEmit *pemitNew = ppow ? power_GetEmit(ppow, pchar) : NULL;
	PowerEmit *pemitOld = GET_REF(pchar->hPowerEmitStance);
	GlobalType eType = entGetType(pchar->pEntParent);

	if(!pdefNew)
	{
		pdefNew = GET_REF(pchar->hPowerDefStanceDefault);
		bNewIsDefaultStance = true;
	}

	if(!bReplace || !poweranimfx_StanceMatch(pdefNew,pdefOld) || pemitOld != pemitNew)
		bEnterStance = true;

	if(!bEnterStance && pdefNew)
	{
		PowerAnimFX *pafxNew = GET_REF(pdefNew->hFX);
		if(pafxNew && 
			pafxNew->bFlashTriggersStanceSwitch &&
			(pafxNew->ppchStanceFlashBits || pafxNew->ppchStanceFlashFX))
		{
			bEnterStance = true;
		}
	}

	if(bEnterStance)
	{
		int i;
		int bValidStance = false;
		PowerAnimFX *pafx = pdefNew ? GET_REF(pdefNew->hFX) : NULL;
		U32 uiID = 0;
		const char* pchRefStanceStickyFX = NULL;

		// First, exit the old stance if it's being replaced
		if(bReplace)
		{
			if(pafx && pafx->bKeepStance)
			{
				// Don't actually replace
				bReplace = false;
			}
			else if(pdefOld && GET_REF(pdefOld->hFX))
			{
				if(pemitOld && pemitNew!=pemitOld)
				{
					// Exit the emit aspect of the stance
					EntityRef erSource = entGetRef(pchar->pEntParent);
					character_StickyBitsOff(pchar,uiID,0,kPowerAnimFXType_StanceEmit,erSource,pemitOld->ppchBits,uiTime);
					REMOVE_HANDLE(pchar->hPowerEmitStance);
				}
				character_ExitStance(pchar,GET_REF(pdefOld->hFX),pchar->uiStancePowerAnimFXID,uiTime);
			}
		}

		// Next, enter the new stance
		if(pdefNew)
		{
			// If we've actually got a PowerAnimFX
			if(pafx)
			{
				EntityRef erSource = entGetRef(pchar->pEntParent);
				F32 fHue = powerapp_GetHue(pchar,ppow,pact,pdefNew);
				EPowerAnimFXFlag ePowerAnimFlag = 0;

				// Simplest way to get FX from Enhancements to use the same params is to just send them in the same batch
				const char **ppchStanceFlashFX = pafx->ppchStanceFlashFX;
				const char **ppchStanceStickyFX = pafx->ppchStanceStickyFX;
				if(pact && eaSize(&pact->ppRefAnimFXEnh))
				{
					ppchStanceFlashFX = NULL;
					ppchStanceStickyFX = NULL;
					eaCopy(&ppchStanceFlashFX,&pafx->ppchStanceFlashFX);
					eaCopy(&ppchStanceStickyFX,&pafx->ppchStanceStickyFX);
					for(i=eaSize(&pact->ppRefAnimFXEnh)-1; i>=0; i--)
					{
						PowerAnimFX *pafxEnh = GET_REF(pact->ppRefAnimFXEnh[i]->hFX);
						if(pafxEnh)
						{
							int j,t;
							t=eaSize(&pafxEnh->ppchStanceFlashFX);
							for(j=0; j<t; j++)
							{
								eaPush(&ppchStanceFlashFX,pafxEnh->ppchStanceFlashFX[j]);
							}
							t=eaSize(&pafxEnh->ppchStanceStickyFX);
							for(j=0; j<t; j++)
							{
								eaPush(&ppchStanceStickyFX,pafxEnh->ppchStanceStickyFX[j]);
							}
						}
					}
				}

				uiID = pact && pact->uchID ? pact->uchID : ppow ? power_AnimFXID(ppow) : bNewIsDefaultStance ? -1 : 0;

				if (pafx->bAlwaysChooseSameNode)
					ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				pchRefStanceStickyFX = eaGet(&ppchStanceStickyFX, 0);
				character_FlashFX(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_StanceFlash,
								pchar,NULL,NULL,pact,ppow,ppchStanceFlashFX,pafx->ppStanceFlashFXParams,fHue,uiTime,
								ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx));

				character_FlashBitsOn(pchar,uiID,0,kPowerAnimFXType_StanceFlash,erSource,pafx->ppchStanceFlashBits,uiTime,false,false,false,false);
				character_StickyFXOn(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_StanceSticky,
									pchar,NULL,NULL,pact,ppow,ppchStanceStickyFX,pafx->ppStanceStickyFXParams,fHue,uiTime,
									ePowerAnimFlag, PowerAnimFX_GetNodeSelectionType(pafx), 0);

				character_StickyBitsOn(pchar,uiID,0,kPowerAnimFXType_StanceSticky,erSource,pafx->ppchStanceStickyBits,uiTime);
				character_StanceWordOn(pchar,uiID,0,kPowerAnimFXType_StanceSticky,erSource,pafx->ppchStickyStanceWords,uiTime);

				if(pact && eaSize(&pact->ppRefAnimFXEnh))
				{
					eaDestroy(&ppchStanceFlashFX);
					eaDestroy(&ppchStanceStickyFX);
				}

				if(bReplace && pemitNew && pemitNew!=pemitOld)
				{
					// Enter the emit aspect of the stance
					character_StickyBitsOn(pchar,uiID,0,kPowerAnimFXType_StanceEmit,erSource,pemitNew->ppchBits,uiTime);
					SET_HANDLE_FROM_REFERENT(g_hPowerEmitDict,pemitNew,pchar->hPowerEmitStance);
				}

				// Note if this is a valid stance
				if(!poweranimfx_IsEmptyStance(pafx->uiStanceID))
				{
					bValidStance = true;
				}
			}

			bFirstInStance = true;
		}

		// Last, set up the ref to the stance
		if(bReplace)
		{
			if(bValidStance)
			{
				pchar->pchRefStanceStickyFX = allocAddString(pchRefStanceStickyFX);
				if(!pchar->pPowerRefStance)
				{
					pchar->pPowerRefStance = power_CreateRef(NULL);
				}

				if(ppow)
				{
					powerref_Set(pchar->pPowerRefStance,ppow);
				}
				else
				{
					powerref_Set(pchar->pPowerRefStance,NULL);
					SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdefNew,pchar->pPowerRefStance->hdef);
				}
			}
			else
			{
				pchar->pchRefStanceStickyFX = NULL;
				powerref_DestroySafe(&pchar->pPowerRefStance);
			}

			pchar->uiStancePowerAnimFXID = uiID;
		}
	}

	return bFirstInStance;
}


static PowerAnimFX* power_GetItemPowerAnimFXForPersistStance(SA_PARAM_NN_VALID Power* ppow)
{
	if (ppow->pSourceItem)
	{
		int i, iSize = eaSize(&ppow->pSourceItem->ppPowers);
		for (i = 0; i < iSize; i++)
		{
			Power* pItemPower = ppow->pSourceItem->ppPowers[i];
			PowerDef* pItemPowDef = GET_REF(pItemPower->hDef);
			PowerAnimFX* pafxItem = SAFE_GET_REF(pItemPowDef, hFX);

			if (pafxItem && 
				(pafxItem->ppchPersistStanceStickyBits || 
				 pafxItem->ppchPersistStanceStickyFX))
			{
				return pafxItem;
			}
		}
	}
	return NULL;
}

// Puts the character into the persist stance of the given power. Returns
//  true if the power is placing the character into a new stance
int character_EnterPersistStance(int iPartitionIdx,
								 Character *pchar,
								 Power *ppow,
								 PowerDef *pdef,
								 PowerActivation *pact,
								 U32 uiTime,
								 U32 uiRequestID,
								 bool bInactiveStance)
{
	int bFirstInStance = false;
	int bEnterStance = false;
	PowerDef *pdefNew = ppow ? GET_REF(ppow->hDef) : pdef;
	PowerDef *pdefOld = pchar->pPowerRefPersistStance ? GET_REF(pchar->pPowerRefPersistStance->hdef) : NULL;
	PowerAnimFX *pafx = pdefNew ? GET_REF(pdefNew->hFX) : NULL;
	PowerAnimFX* pafxDerived = NULL;
	GlobalType eType = entGetType(pchar->pEntParent);
	
	if((!pdefNew || (pafx && (pafx->bDerivePersistStanceFromItem || !poweranimfx_IsEmptyPersistStance(pafx->uiPersistStanceID)))) &&
		((bInactiveStance && !pchar->bPersistStanceInactive) ||
		 (!bInactiveStance && pchar->bPersistStanceInactive) ||
		 !poweranimfx_PersistStanceMatch(pdefNew,pdefOld)))
	{
		bEnterStance = true;
	}
	if(bEnterStance)
	{
		int bValidStance = false;
		U32 uiID = 0;
		PowerAnimFX *pafxOld = pdefOld ? GET_REF(pdefOld->hFX) : NULL;

		// First, exit the old stance
		if(pafxOld)
		{
			character_ExitPersistStance(pchar,pafxOld,pchar->uiPersistStancePowerAnimFXID,uiTime);
		}

		// Next, enter the new stance
		if (ppow && pdefNew)
		{
			if(!bInactiveStance)
			{
				EntityRef erSource = entGetRef(pchar->pEntParent);
				F32 fHue = powerapp_GetHue(pchar,ppow,pact,pdefNew);
				EPowerAnimFXFlag ePowerAnimFlag = 0;

				uiID = pact && pact->uchID ? pact->uchID : (uiRequestID | PAFX_PERSIST_STANCE_MASK);

				if (pafx->bDerivePersistStanceFromItem)
					pafxDerived = power_GetItemPowerAnimFXForPersistStance(ppow);
				if (!pafxDerived)
					pafxDerived = pafx;

				if (pafxDerived->bAlwaysChooseSameNode)
					ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

				character_FlashFX(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_PersistStanceFlash,
					pchar,NULL,NULL,pact,ppow,pafxDerived->ppchPersistStanceFlashFX,
					pafxDerived->ppPersistStanceFlashFXParams,fHue,uiTime,
					ePowerAnimFlag,PowerAnimFX_GetNodeSelectionType(pafxDerived));
				character_FlashBitsOn(pchar,uiID,0,kPowerAnimFXType_PersistStanceFlash,erSource,pafxDerived->ppchPersistStanceFlashBits,uiTime,false,false,false,!(SAFE_MEMBER(pact, uchID) || uiRequestID));
				character_StickyFXOn(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_PersistStanceSticky,
					pchar,NULL,NULL,pact,ppow,pafxDerived->ppchPersistStanceStickyFX,
					pafxDerived->ppPersistStanceStickyFXParams,fHue,uiTime,
					ePowerAnimFlag,PowerAnimFX_GetNodeSelectionType(pafxDerived), 0);

				character_StickyBitsOn(pchar,uiID,0,kPowerAnimFXType_PersistStanceSticky,erSource,pafxDerived->ppchPersistStanceStickyBits,uiTime);

				bValidStance = true;
				bFirstInStance = true;
			}
			else // Try to enter the inactive version of the persist stance
			{
				if (pafx->bDerivePersistStanceFromItem)
					pafxDerived = power_GetItemPowerAnimFXForPersistStance(ppow);
				if (!pafxDerived)
					pafxDerived = pafx;

				if (pafxDerived && eaSize(&pafxDerived->ppPersistStanceInactiveStickyFXParams))
				{
					F32 fHue = powerapp_GetHue(pchar,NULL,NULL,pdefNew);
					EPowerAnimFXFlag ePowerAnimFlag = 0;

					if (pafx->bAlwaysChooseSameNode)
						ePowerAnimFlag |= EPowerFXFlags_ALWAYS_CHOOSE_SAME_NODE;

					uiID = uiRequestID | PAFX_INACTIVE_PERSIST_STANCE_MASK;
					character_StickyFXOn(iPartitionIdx,pchar,uiID,0,kPowerAnimFXType_PersistStanceSticky,
						pchar,NULL,NULL,NULL,NULL,pafxDerived->ppchPersistStanceStickyFX,
						pafxDerived->ppPersistStanceInactiveStickyFXParams,fHue,uiTime,
						ePowerAnimFlag,PowerAnimFX_GetNodeSelectionType(pafxDerived), 0);
					bValidStance = true;
				}
			}
		}

		// Last, set up the ref to the stance
		if(bValidStance)
		{
			if(!pchar->pPowerRefPersistStance)
			{
				pchar->pPowerRefPersistStance = power_CreateRef(NULL);
			}

			if(ppow)
			{
				powerref_Set(pchar->pPowerRefPersistStance,ppow);
			}
			else
			{
				powerref_Set(pchar->pPowerRefPersistStance,NULL);
				SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdefNew,pchar->pPowerRefPersistStance->hdef);
			}

			if (pafx->bDerivePersistStanceFromItem && pafxDerived != pafx)
			{
				pchar->pchRefPersistStanceStickyFX = pafxDerived->ppchPersistStanceStickyFX[0];
				pchar->pchRefPersistStanceStickyBits = pafxDerived->ppchPersistStanceStickyBits[0];
			}
			else
			{
				pchar->pchRefPersistStanceStickyFX = NULL;
				pchar->pchRefPersistStanceStickyBits = NULL;
			}
			pchar->bPersistStanceInactive = !!bInactiveStance;
		}
		else
		{
			powerref_DestroySafe(&pchar->pPowerRefPersistStance);
			pchar->bPersistStanceInactive = false;
			pchar->pchRefPersistStanceStickyFX = NULL;
			pchar->pchRefPersistStanceStickyBits = NULL;
		}

		pchar->uiPersistStancePowerAnimFXID = uiID;
	}

	return bFirstInStance;
}

// Takes the character out of the stance in the pafx.  Does NOT update
//  the base stance of the character
void character_ExitStance(Character *pchar,
						  PowerAnimFX *pafx,
						  U32 uiID,
						  U32 uiTime)
{
	EntityRef erSource = entGetRef(pchar->pEntParent);
	static const char** s_ppchStanceStickyFX = NULL;

	character_StickyBitsOff(pchar,uiID,0,kPowerAnimFXType_StanceSticky,erSource,pafx->ppchStanceStickyBits,uiTime);
	character_StanceWordOff(pchar,uiID,0,kPowerAnimFXType_StanceSticky,erSource,pafx->ppchStickyStanceWords,uiTime);
	
	eaClearFast(&s_ppchStanceStickyFX);
	if (pchar->pchRefStanceStickyFX)
	{
		eaPush(&s_ppchStanceStickyFX, pchar->pchRefStanceStickyFX);
	}
	else if (eaSize(&pafx->ppchStanceStickyFX))
	{
		eaPush(&s_ppchStanceStickyFX, pafx->ppchStanceStickyFX[0]);
	}
	character_StickyFXOff(pchar,uiID,0,kPowerAnimFXType_StanceSticky,pchar,s_ppchStanceStickyFX,uiTime,false);
}

// Takes the character out of the persist stance in the pafx.
void character_ExitPersistStance(Character *pchar,
								 PowerAnimFX *pafx,
								 U32 uiID,
								 U32 uiTime)
{
	EntityRef erSource = entGetRef(pchar->pEntParent);
	static const char** s_ppchStanceStickyFX = NULL;

	eaClearFast(&s_ppchStanceStickyFX);
	if (pchar->pchRefPersistStanceStickyBits) {
		eaPush(&s_ppchStanceStickyFX, pchar->pchRefPersistStanceStickyBits);
	} else if (eaSize(&pafx->ppchPersistStanceStickyBits)) {
		eaPush(&s_ppchStanceStickyFX, pafx->ppchPersistStanceStickyBits[0]);
	}
	character_StickyBitsOff(pchar,uiID,0,kPowerAnimFXType_PersistStanceSticky,erSource,s_ppchStanceStickyFX,uiTime);

	eaClearFast(&s_ppchStanceStickyFX);
	if (pchar->pchRefPersistStanceStickyFX) {
		eaPush(&s_ppchStanceStickyFX, pchar->pchRefPersistStanceStickyFX);
	} else if (eaSize(&pafx->ppchPersistStanceStickyFX)) {
		eaPush(&s_ppchStanceStickyFX, pafx->ppchPersistStanceStickyFX[0]);
	}
	character_StickyFXOff(pchar,uiID,0,kPowerAnimFXType_PersistStanceSticky,pchar,s_ppchStanceStickyFX,uiTime,false);
}

// Returns true if the two powers use the same 'stance'
int poweranimfx_StanceMatch(PowerDef *pdefA,
							PowerDef *pdefB)
{
	PowerAnimFX *pafxA, *pafxB;

	// Same stance means both powers and their afx's stance ids are the same and none are null

	if(pdefA==pdefB)
	{
		return true;
	}

	if(!pdefA || !pdefB)
	{
		return false;
	}

	pafxA = GET_REF(pdefA->hFX);
	pafxB = GET_REF(pdefB->hFX);

	if(!pafxA || !pafxB || pafxA->uiStanceID != pafxB->uiStanceID)
	{
		return false;
	}

	return true;
}

// Returns true if the two powers use the same 'persistent stance'
int poweranimfx_PersistStanceMatch(PowerDef *pdefA,
								   PowerDef *pdefB)
{
	PowerAnimFX *pafxA, *pafxB;

	// Same stance means both powers and their afx's stance ids are the same and none are null

	if(pdefA==pdefB)
	{
		return true;
	}

	if(!pdefA || !pdefB)
	{
		return false;
	}

	pafxA = GET_REF(pdefA->hFX);
	pafxB = GET_REF(pdefB->hFX);

	if(!pafxA || !pafxB || pafxA->uiPersistStanceID != pafxB->uiPersistStanceID)
	{
		return false;
	}

	return true;
}

// Returns true if the given stance ID matches the empty stance ID
int poweranimfx_IsEmptyStance(U32 uiStanceID)
{
	return uiStanceID==s_uiEmptyStanceID;
}

// Returns true if the given persist stance ID matches the empty persist stance ID
int poweranimfx_IsEmptyPersistStance(U32 uiStanceID)
{
	return uiStanceID==s_uiEmptyPersistStanceID;
}




static int SortStanceString(const char **ppStringA, const char **ppStringB)
{
	return stricmp((*ppStringA), (*ppStringB)); 
}

static int SortStanceParam(const PowerFXParam **ppParamA, const PowerFXParam **ppParamB)
{
	return stricmp((*ppParamA)->cpchParam,(*ppParamB)->cpchParam);
}


// Generates a stance id for the animfx and stores it
static bool GenerateStanceID(PowerAnimFX *pafx)
{
	// If you change the way this function operates, change s_uiEmptyStanceID to match
	int i,s;
	bool bValid = true;
	char *pchString = NULL;
	const char **ppStrings = NULL;
	estrStackCreate(&pchString);

	// Bits
	eaCopy(&ppStrings,&pafx->ppchStanceStickyBits);
	eaQSort(ppStrings,SortStanceString);
	s = eaSize(&ppStrings);
	estrConcatf(&pchString,"%d",s);
	for(i=0; i<s; i++)
	{
		estrConcatf(&pchString," %s",ppStrings[i]);
	}

	// Stance words
	eaCopy(&ppStrings,&pafx->ppchStickyStanceWords);
	eaQSort(ppStrings,SortStanceString);
	s = eaSize(&ppStrings);
	estrConcatf(&pchString," %d",s);
	for(i=0; i<s; i++)
	{
		estrConcatf(&pchString," %s",ppStrings[i]);
	}

	// FX
	eaCopy(&ppStrings,&pafx->ppchStanceStickyFX);
	eaQSort(ppStrings,SortStanceString);
	s = eaSize(&ppStrings);
	estrConcatf(&pchString," %d",s);
	for(i=0; i<s; i++)
	{
		estrConcatf(&pchString," %s",ppStrings[i]);
	}

	eaDestroy(&ppStrings);

	if(s)
	{
		// Had some fx, so include params if they exist
		PowerFXParam **ppParams = NULL;
		eaCopy(&ppParams,&pafx->ppStanceStickyFXParams);
		eaQSort(ppParams,SortStanceParam);
		s = eaSize(&ppParams);
		estrConcatf(&pchString," %d",s);
		for(i=0; i<s; i++)
		{
			char* exprString = exprGetCompleteString(ppParams[i]->expr);

			if(exprString && exprString[0])
			{
				estrConcatf(&pchString," (%s %s)",ppParams[i]->cpchParam,exprString);
			}
			else
			{
				ErrorFilenamef(pafx->cpchFile,"Empty StanceStickyFXParams\n");
				bValid = false;
			}
		}
		eaDestroy(&ppParams);
	}

	// Transition time
	estrConcatf(&pchString," %g",pafx->fStanceTransitionTime);

	pafx->uiStanceID = hashStringInsensitive(pchString);
	estrDestroy(&pchString);

	return bValid;
}

// Generates a persist stance id for the animfx and stores it
static bool GeneratePersistStanceID(PowerAnimFX *pafx)
{
	// If you change the way this function operates, change s_uiEmptyPersistStanceID to match
	int i,s;
	bool bValid = true;
	char *pchString = NULL;
	const char **ppStrings = NULL;
	estrStackCreate(&pchString);

	// Bits
	eaCopy(&ppStrings,&pafx->ppchPersistStanceStickyBits);
	eaQSort(ppStrings,SortStanceString);
	s = eaSize(&ppStrings);
	estrConcatf(&pchString,"%d",s);
	for(i=0; i<s; i++)
	{
		estrConcatf(&pchString," %s",ppStrings[i]);
	}

	// FX
	eaCopy(&ppStrings,&pafx->ppchPersistStanceStickyFX);
	eaQSort(ppStrings,SortStanceString);
	s = eaSize(&ppStrings);
	estrConcatf(&pchString," %d",s);
	for(i=0; i<s; i++)
	{
		estrConcatf(&pchString," %s",ppStrings[i]);
	}

	eaDestroy(&ppStrings);

	if(s)
	{
		// Had some fx, so include params if they exist
		PowerFXParam **ppParams = NULL;
		eaCopy(&ppParams,&pafx->ppPersistStanceStickyFXParams);
		eaPushEArray(&ppParams,&pafx->ppPersistStanceInactiveStickyFXParams);
		eaQSort(ppParams,SortStanceParam);
		s = eaSize(&ppParams);
		estrConcatf(&pchString," %d",s);
		for(i=0; i<s; i++)
		{
			char* exprString = exprGetCompleteString(ppParams[i]->expr);

			if(exprString && exprString[0])
			{
				estrConcatf(&pchString," (%s %s)",ppParams[i]->cpchParam,exprString);
			}
			else
			{
				ErrorFilenamef(pafx->cpchFile,"Empty PersistStanceStickyFXParams\n");
				bValid = false;
			}
		}
		eaDestroy(&ppParams);
	}

	pafx->uiPersistStanceID = hashStringInsensitive(pchString);
	estrDestroy(&pchString);

	return bValid;
}

#if GAMESERVER
// Dragon specific power animFX.
void poweranimfx_DragonStartPowerActivation(Character *pChar, 
											PowerAnimFX *pAfx,
											PowerActivation *pAct,
											PowerDef *pPowerDef)
{
	if (pChar->pEntParent->mm.mrDragon && pAfx->pDragon)
	{
		if (pAfx->pDragon->bRotateHeadToBodyOrientation)
		{
			mrDragon_SetOverrideRotateHeadToBodyOrientation(pChar->pEntParent->mm.mrDragon);
		}
		else if (pAfx->pDragon->bRotateBodyToHeadOrientation)
		{
			mrDragon_SetOverrideRotateBodyToHeadOrientation(pChar->pEntParent->mm.mrDragon);
		}
	}
}
#endif


static void ValidateAnimFXParam(SA_PARAM_OP_VALID PowerFXParam *pParam, SA_PARAM_OP_VALID PowerFXParam **ppParams, SA_PARAM_NN_STR const char *file)
{
	if(pParam)
	{
		int i;

		if(!pParam->expr)
			ErrorFilenamef(file, "FXParam \"%s\" with no expression", pParam->cpchParam);

		for(i=eaSize(&ppParams)-1; i>=0; i--)
		{
			if(ppParams[i]==pParam)
				break;

			if(pParam->cpchParam==ppParams[i]->cpchParam)
			{
				ErrorFilenamef(file,"Multiple FXParams \"%s\" in a set of FXParams", pParam->cpchParam);
			}
		}
	}
}

static void GenerateAnimFXParam(SA_PARAM_OP_VALID PowerAnimFX *pafx, SA_PARAM_OP_VALID Expression *pExpr)
{
	if(pExpr)
	{
		ExprContext* pContext = ParamBlockContext();
		char *pchString = NULL;
		
		// Do the actual generate
		exprGenerate(pExpr,pContext);
		
		// Check for Costume Bone usage
		estrStackCreate(&pchString);
		exprGetCompleteStringEstr(pExpr,&pchString);
		if(strStartsWith(pchString,"GetCostumePart"))
		{
			char *pchQuote = strchr(pchString+13,'"');
			if(pchQuote)
			{
				char *pchBone = pchQuote + 1;
				pchQuote = strchr(pchBone,'"');
				if(pchQuote)
				{
					*pchQuote = '\0';
					if(!pafx)
					{
						ErrorFilenamef(pExpr->filename,"Can't refer to a PCBone in a non-powerart FX param");
					}
					else if(!pafx->cpchPCBoneName)
					{
						pafx->cpchPCBoneName = allocAddString(pchBone);
					}
					else if(stricmp(pafx->cpchPCBoneName,pchBone))
					{
						ErrorFilenamef(pafx->cpchFile,"Can't currently refer to more than one PCBone (%s, %s)",pafx->cpchPCBoneName,pchBone);
					}
				}
			}
		}
		estrDestroy(&pchString);
	}
}

static void ValidateAndGenerateAnimFXParams(PowerAnimFX *pafx)
{
#define PARAM_EXPR_GENERATE(fxname) \
	for(i=eaSize(&pafx->pp##fxname##FXParams)-1; i>=0; i--) { \
		ValidateAnimFXParam(pafx->pp##fxname##FXParams[i],pafx->pp##fxname##FXParams,pafx->cpchFile); \
		GenerateAnimFXParam(pafx,pafx->pp##fxname##FXParams[i]->expr); \
	}

	int i,j;

	PARAM_EXPR_GENERATE(StanceSticky);
	PARAM_EXPR_GENERATE(PersistStanceSticky);
	PARAM_EXPR_GENERATE(PersistStanceInactiveSticky);
	PARAM_EXPR_GENERATE(StanceFlash);
	PARAM_EXPR_GENERATE(ChargeSticky);
	PARAM_EXPR_GENERATE(ChargeFlash);
	PARAM_EXPR_GENERATE(LungeSticky);
	PARAM_EXPR_GENERATE(LungeFlash);
	PARAM_EXPR_GENERATE(ActivateSticky);
	PARAM_EXPR_GENERATE(Activate);
	PARAM_EXPR_GENERATE(PreactivateSticky);
	PARAM_EXPR_GENERATE(Preactivate);
	PARAM_EXPR_GENERATE(PeriodicActivate);
	PARAM_EXPR_GENERATE(Deactivate);
	PARAM_EXPR_GENERATE(Targeted);
	PARAM_EXPR_GENERATE(HitSticky);
	PARAM_EXPR_GENERATE(Hit);
	PARAM_EXPR_GENERATE(Block);
	PARAM_EXPR_GENERATE(Death);

	for(j=eaSize(&pafx->ppChargedActivate)-1; j>=0; j--)
	{
		PARAM_EXPR_GENERATE(ChargedActivate[j]->ppActivate);
	}
}

// Generates the FX Params for the AttribModDef's FX
void moddef_GenerateFXParams(AttribModDef *pmoddef)
{
	int i;
	for(i=eaSize(&pmoddef->ppContinuingFXParams)-1; i>=0; i--)
	{
		ValidateAnimFXParam(pmoddef->ppContinuingFXParams[i],pmoddef->ppContinuingFXParams,pmoddef->pPowerDef->pchFile);
		GenerateAnimFXParam(NULL,pmoddef->ppContinuingFXParams[i]->expr);
	}
	for(i=eaSize(&pmoddef->ppConditionalFXParams)-1; i>=0; i--)
	{
		ValidateAnimFXParam(pmoddef->ppConditionalFXParams[i],pmoddef->ppConditionalFXParams,pmoddef->pPowerDef->pchFile);
		GenerateAnimFXParam(NULL,pmoddef->ppConditionalFXParams[i]->expr);
	}
}

// Copies the AttribModDef's PowerDef's Icon data into a spot where param generation
//  can get at it, and returns the static PowerFXParam to use to refer to it.
PowerFXParam* moddef_SetPowerIconParam(AttribModDef *pmoddef)
{
	PowerDef *pdef = pmoddef->pPowerDef;
	estrClear(&s_pchPowerIconParam);
	if(pdef && pdef->pchIconName)
	{
		estrCopy2(&s_pchPowerIconParam,"CFX_");
		estrAppend2(&s_pchPowerIconParam,pdef->pchIconName);
	}

	if(!s_pPowerIconParam)
	{
		s_pPowerIconParam = StructCreate(parse_PowerFXParam);
		s_pPowerIconParam->cpchParam = s_pchPowerIcon;
		s_pPowerIconParam->eType = kPowerFXParamType_STR;
	}

	return s_pPowerIconParam;
}


static int ValidateAnimFXNames(PowerAnimFX *pafx)
{
	int bRet = true;
#ifdef GAMECLIENT

#define FX_NAME_VALIDATE(fxname, enforceSelfTermination) \
	for(i=eaSize(&pafx->ppch##fxname##FX)-1; i>=0; i--) {\
		if(!dynFxInfoExists(pafx->ppch##fxname##FX[i])) { ErrorFilenamef(pafx->cpchFile,"Can't find FX %s",pafx->ppch##fxname##FX[i]); bRet = false; } \
		if (enforceSelfTermination && !dynFxInfoSelfTerminates(pafx->ppch##fxname##FX[i])) {ErrorFilenamef(pafx->cpchFile,"FX %s is flashed, yet does not always self terminate!", pafx->ppch##fxname##FX[i]); bRet = false; } }


	int i, j;

	FX_NAME_VALIDATE(StanceSticky, false);
	FX_NAME_VALIDATE(StanceFlash, true);
	FX_NAME_VALIDATE(ChargeSticky, false);
	FX_NAME_VALIDATE(ChargeFlash, true);
	FX_NAME_VALIDATE(LungeSticky, false);
	FX_NAME_VALIDATE(LungeFlash, true);
	FX_NAME_VALIDATE(ActivateSticky, false);
	//FX_NAME_VALIDATE(Activate);
	for(i=eaSize(&pafx->ppActivateFX)-1; i>=0; i--)
	{
		if(!dynFxInfoExists(pafx->ppActivateFX[i]->pchActivateFX)) { ErrorFilenamef(pafx->cpchFile,"Can't find FX %s",pafx->ppActivateFX[i]->pchActivateFX); bRet = false; }
		if (!dynFxInfoSelfTerminates(pafx->ppActivateFX[i]->pchActivateFX)) {ErrorFilenamef(pafx->cpchFile,"FX %s is flashed, yet does not always self terminate!", pafx->ppActivateFX[i]->pchActivateFX); bRet = false; }
	}
	for(i=eaSize(&pafx->ppActivateMissFX)-1; i>=0; i--)
	{
		if(!dynFxInfoExists(pafx->ppActivateMissFX[i]->pchActivateFX)) { ErrorFilenamef(pafx->cpchFile,"Can't find FX %s",pafx->ppActivateMissFX[i]->pchActivateFX); bRet = false; }
		if (!dynFxInfoSelfTerminates(pafx->ppActivateMissFX[i]->pchActivateFX)) {ErrorFilenamef(pafx->cpchFile,"FX %s is flashed, yet does not always self terminate!", pafx->ppActivateMissFX[i]->pchActivateFX); bRet = false; }
	}
	for(i=eaSize(&pafx->ppPeriodicActivateFX)-1; i>=0; i--)
	{
		if(!dynFxInfoExists(pafx->ppPeriodicActivateFX[i]->pchActivateFX)) { ErrorFilenamef(pafx->cpchFile,"Can't find FX %s",pafx->ppPeriodicActivateFX[i]->pchActivateFX); bRet = false; }
		if (!dynFxInfoSelfTerminates(pafx->ppPeriodicActivateFX[i]->pchActivateFX)) {ErrorFilenamef(pafx->cpchFile,"FX %s is flashed, yet does not always self terminate!", pafx->ppPeriodicActivateFX[i]->pchActivateFX); bRet = false; }
	}
	FX_NAME_VALIDATE(Deactivate, true);
	FX_NAME_VALIDATE(Targeted, true);
	FX_NAME_VALIDATE(HitSticky, false);
	FX_NAME_VALIDATE(Hit, true);
	FX_NAME_VALIDATE(Block, true);
	FX_NAME_VALIDATE(Death, true);
	for(j=eaSize(&pafx->ppChargedActivate)-1; j>=0; j--)
	{
		for(i=eaSize(&pafx->ppChargedActivate[j]->ppchActivateFX)-1; i>=0; i--)
		{
			if(!dynFxInfoExists(pafx->ppChargedActivate[j]->ppchActivateFX[i])) { ErrorFilenamef(pafx->cpchFile,"Can't find FX %s",pafx->ppChargedActivate[j]->ppchActivateFX[i]); bRet = false; }
			if (!dynFxInfoSelfTerminates(pafx->ppChargedActivate[j]->ppchActivateFX[i])) {ErrorFilenamef(pafx->cpchFile,"FX %s is flashed, yet does not always self terminate!", pafx->ppChargedActivate[j]->ppchActivateFX[i]); bRet = false; }
		}
		for(i=eaSize(&pafx->ppChargedActivate[j]->ppchActivateMissFX)-1; i>=0; i--)
		{
			if(!dynFxInfoExists(pafx->ppChargedActivate[j]->ppchActivateMissFX[i])) { ErrorFilenamef(pafx->cpchFile,"Can't find FX %s",pafx->ppChargedActivate[j]->ppchActivateMissFX[i]); bRet = false; }
			if (!dynFxInfoSelfTerminates(pafx->ppChargedActivate[j]->ppchActivateMissFX[i])) {ErrorFilenamef(pafx->cpchFile,"FX %s is flashed, yet does not always self terminate!", pafx->ppChargedActivate[j]->ppchActivateMissFX[i]); bRet = false; }
		}
	}
#endif
	return bRet;
}

static void GenerateAnimFXHasSticky(PowerAnimFX *pafx)
{
	pafx->bHasSticky = false;
	pafx->bHasSticky |= (eaSize(&pafx->ppchStanceStickyBits) || eaSize(&pafx->ppchStanceStickyFX) || eaSize(&pafx->ppStanceStickyFXParams));
	pafx->bHasSticky |= (eaSize(&pafx->ppchChargeStickyBits) || eaSize(&pafx->ppchChargeStickyFX) || eaSize(&pafx->ppChargeStickyFXParams));
	pafx->bHasSticky |= (eaSize(&pafx->ppchLungeStickyBits) || eaSize(&pafx->ppchLungeStickyFX) || eaSize(&pafx->ppLungeStickyFXParams));
	pafx->bHasSticky |= (eaSize(&pafx->ppchActivateStickyBits) || eaSize(&pafx->ppchActivateStickyFX) || eaSize(&pafx->ppActivateStickyFXParams));
	pafx->bHasSticky |= (eaSize(&pafx->ppchHitStickyFX) || eaSize(&pafx->ppHitStickyFXParams));
}

static int ValidatePowerLunge(PowerAnimFX *pafx)
{
	int bRet = true;

	if(pafx->pLunge)
	{
		if(pafx->pLunge->fSpeed <= 0 && pafx->pLunge->iStrictFrameDuration <= 0)
		{
			ErrorFilenamef(pafx->cpchFile,"Lunge speed must be greater than 0");
			bRet = false;
		}

		if(pafx->pLunge->fRange <= 0)
		{
			ErrorFilenamef(pafx->cpchFile,"Lunge range must be greater than 0");
			bRet = false;
		}
	}

	return bRet;
}

static int ValidateAndGenerateAnimFX(PowerAnimFX *pafx)
{
	int bRet = true;
	int iFramesBeforeHit = eaiSize(&pafx->piFramesBeforeHit) ? pafx->piFramesBeforeHit[0] : 0;
	int iFramesBeforePeriodicHit = eaiSize(&pafx->piFramesBeforePeriodicHit) ? pafx->piFramesBeforePeriodicHit[0] : 0;
	bRet &= GenerateStanceID(pafx);
	bRet &= GeneratePersistStanceID(pafx);
	ValidateAndGenerateAnimFXParams(pafx);
	GenerateAnimFXHasSticky(pafx);

	if (pafx->eReactTrigger != kAttackReactTrigger_Time &&
		pafx->eHitDirection != PADID_Default)
	{
		ErrorFilenamef(pafx->cpchFile, "Not allowed to use a ReactTrigger and a HitDirection in the same file");
		bRet = false;
	}

	if(pafx->bKeepStance && pafx->uiStanceID!=s_uiEmptyStanceID)
	{
		ErrorFilenamef(pafx->cpchFile,"KeepStance can not be set to true if you have StanceStickyBits/FX");
		bRet = false;
	}

	// Verify speed penalty values before applying the default
	if(pafx->fSpeedPenaltyDuringCharge > 1 || pafx->fSpeedPenaltyDuringCharge < 0)
	{
		ErrorFilenamef(pafx->cpchFile,"If you specify a SpeedPenaltyDuringCharge, it must be in the range [0 .. 1]");
		bRet = false;
	}
	if(pafx->fSpeedPenaltyDuringActivate > 1 || pafx->fSpeedPenaltyDuringActivate < 0)
	{
		ErrorFilenamef(pafx->cpchFile,"If you specify a SpeedPenaltyDuringActivate, it must be in the range [0 .. 1]");
		bRet = false;
	}

	// Get default speed penalties if they weren't specified in the file
	if(!TokenIsSpecified(parse_PowerAnimFX, s_iSpeedPenaltyDuringChargeIndex, pafx, s_iUsedFieldsIndex))
	{
		pafx->fSpeedPenaltyDuringCharge = g_CombatConfig.fSpeedPenaltyDuringChargeDefault;
	}
	if(!TokenIsSpecified(parse_PowerAnimFX, s_iSpeedPenaltyDuringActivateIndex, pafx, s_iUsedFieldsIndex))
	{
		pafx->fSpeedPenaltyDuringActivate = g_CombatConfig.fSpeedPenaltyDuringActivateDefault;
	}

	// Final insurance that the values are sane
	pafx->fSpeedPenaltyDuringCharge = CLAMP(pafx->fSpeedPenaltyDuringCharge,0,1);
	pafx->fSpeedPenaltyDuringActivate = CLAMP(pafx->fSpeedPenaltyDuringActivate,0,1);

	if(pafx->bDelayedHit)
	{
		if(pafx->fProjectileSpeed<=0 && pafx->fMeleeSwingAnglePerSecond<=0.f)
		{
			ErrorFilenamef(pafx->cpchFile,"ProjectileSpeed or MeleeSwingAnglePerSecond must be a positive value if DelayedHit is enabled");
		}
	}



	// Max sure FBA is not after FBH
	if(pafx->iFramesBeforeActivateBits > iFramesBeforeHit)
	{
		ErrorFilenamef(pafx->cpchFile,"FramesBeforeActivateBits must not be larger than the first FramesBeforeHit\n");
		bRet = false;
	}

	if(pafx->iFramesBeforeActivateFX > iFramesBeforeHit)
	{
		ErrorFilenamef(pafx->cpchFile,"FramesBeforeActivateFX must not be larger than the first FramesBeforeHit\n");
		bRet = false;
	}

	if(pafx->iFramesBeforePeriodicActivateBits > iFramesBeforePeriodicHit)
	{
		ErrorFilenamef(pafx->cpchFile,"FramesBeforePeriodicActivateBits must not be larger than the first FramesBeforePeriodicHit\n");
		bRet = false;
	}

	if(pafx->iFramesBeforePeriodicActivateFX > iFramesBeforePeriodicHit)
	{
		ErrorFilenamef(pafx->cpchFile,"FramesBeforePeriodicActivateFX must not be larger than the first FramesBeforePeriodicHit\n");
		bRet = false;
	}

	if(eaiSize(&pafx->piFramesBeforeHit) >= (1 << (32-PAFX_HITFX_SHIFT)))
	{
		ErrorFilenamef(pafx->cpchFile,"FramesBeforeHit has too many entries\n");
		bRet = false;
	}

	if(eaiSize(&pafx->piFramesBeforePeriodicHit) >= (1 << (32-PAFX_HITFX_SHIFT)))
	{
		ErrorFilenamef(pafx->cpchFile,"FramesBeforePeriodicHit has too many entries\n");
		bRet = false;
	}

	if (IS_HANDLE_ACTIVE(pafx->lurch.hMovementGraph) &&
		(pafx->lurch.iSlideFrameStart || pafx->lurch.iSlideFrameCount || pafx->lurch.fSlideDistance != 0.f || pafx->lurch.fMovementSpeed != 0.f))
	{
		ErrorFilenamef(pafx->cpchFile,"You cannot use any of the following parameters if you set an "
			"animation graph to be used for movement: MovementFrameStart, MovementFrameCount, MovementDistance "
			"and MovementSpeed.\n");
		bRet = false;
	}

	bRet = ValidateAnimFXNames(pafx) && bRet;

	bRet = ValidatePowerLunge(pafx) && bRet;

	return bRet;
}

AUTO_FIXUPFUNC;
TextParserResult PowerArtFixup(PowerAnimFX *pafx, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			// Derive proper pchName and pchScope from the filename
			char achTemp[512];
			const char *pchScopeStart, *pchScopeEnd;
			if(pafx->cpchName)
				pafx->cpchName = NULL;
			if(pafx->cpchScope)
				pafx->cpchScope = NULL;
			getFileNameNoExtNoDirs(achTemp, pafx->cpchFile);
			pafx->cpchName = allocAddString(achTemp);
			pchScopeStart = pafx->cpchFile + strlen("powerart/");
			pchScopeEnd = strstri(pchScopeStart,pafx->cpchName);
			if(pchScopeEnd > pchScopeStart)
			{
				strncpy(achTemp,pchScopeStart,(pchScopeEnd-pchScopeStart)-1);
				pafx->cpchScope = allocAddString(achTemp);
			}
		}
	}
	return PARSERESULT_SUCCESS;
}

static void PostTextReadFixupAnimFX(PowerAnimFX *pAfx)
{
	if (pAfx->pchActivateAnimFlag)
	{
		if (stricmp(pAfx->pchActivateAnimFlag, "Activate")!=0)
			ErrorFilenamef(pAfx->cpchFile, "Specifies ActivateAnimFlag '%s', expected 'Activate'", pAfx->pchActivateAnimFlag);
	}
	if (pAfx->pchDeactivateAnimFlag)
	{
		if (stricmp(pAfx->pchDeactivateAnimFlag, "Deactivate")!=0)
			ErrorFilenamef(pAfx->cpchFile, "Specifies DeactivateAnimFlag '%s', expected 'Deactivate'", pAfx->pchDeactivateAnimFlag);
	}
	if (pAfx->pchPreactivateAnimFlag)
	{
		if (stricmp(pAfx->pchPreactivateAnimFlag, "Preactivate")!=0 &&
			stricmp(pAfx->pchPreactivateAnimFlag, "Activate")!=0)
			ErrorFilenamef(pAfx->cpchFile, "Specifies PreactivateAnimFlag '%s', expected 'Preactivate' or 'Activate'", pAfx->pchPreactivateAnimFlag);
	}
	FOR_EACH_IN_EARRAY(pAfx->ppchStickyStanceWords, const char, pcWord)
	{
		if (!dynAnimStanceValid(pcWord))
			Errorf("PowerArt references invalid stance word: %s", pcWord);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(pAfx->ppchDeathAnimStanceWords, const char, pcWord)
	{
		if (!dynAnimStanceValid(pcWord))
			Errorf("PowerArt references invalid stance word: %s", pcWord);
	}
	FOR_EACH_END;

	if (pAfx->pDragon)
	{
		if (pAfx->pDragon->bRotateBodyToHeadOrientation && pAfx->pDragon->bRotateHeadToBodyOrientation)
		{
			Alertf("%s: Dragon powerAnimFX flags RotateBodyToHeadOrientation and RotateHeadToBodyOrientation are mutually exclusive to each other.", pAfx->cpchFile);
		}
	}
}

static void poweranimfx_ValidateRefs(PowerAnimFX *pAfx)
{
	DynAnimGraph *pGraph = GET_REF(pAfx->lurch.hMovementGraph);
	if (IS_HANDLE_ACTIVE(pAfx->lurch.hMovementGraph) && pGraph == NULL)
	{
		ErrorFilenamef(pAfx->cpchFile, "PowerArt references an invalid movement graph: %s", REF_STRING_FROM_HANDLE(pAfx->lurch.hMovementGraph));
	}
	if (pGraph && 
		(!pGraph->bGeneratePowerMovementInfo || pGraph->pPowerMovementInfo == NULL))
	{
		ErrorFilenamef(pAfx->cpchFile, "PowerArt references an animation graph with no movement information: %s", REF_STRING_FROM_HANDLE(pAfx->lurch.hMovementGraph));
	}
}

// Resource validation callback
static int PowerArtResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, PowerAnimFX *pAfx, U32 userID)
{
	switch (eType)
	{
	xcase RESVALIDATE_POST_TEXT_READING:
		PostTextReadFixupAnimFX(pAfx);
		return VALIDATE_HANDLED;
	xcase RESVALIDATE_POST_BINNING:
		ValidateAndGenerateAnimFX(pAfx);
		return VALIDATE_HANDLED;
#if GAMESERVER || GAMECLIENT
	xcase RESVALIDATE_CHECK_REFERENCES:
		poweranimfx_ValidateRefs(pAfx);
		return VALIDATE_HANDLED;
#endif
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int PowerAnimFXRegisterDict(void)
{
	// Set up reference dictionary
	g_hPowerAnimFXDict = RefSystem_RegisterSelfDefiningDictionary("PowerAnimFX", false, parse_PowerAnimFX, true, true, NULL);

	resDictManageValidation(g_hPowerAnimFXDict, PowerArtResValidateCB);
	resDictSetDisplayName(g_hPowerAnimFXDict, "Power Art File", "Power Art Files", RESCATEGORY_ART);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hPowerAnimFXDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hPowerAnimFXDict, NULL, NULL, NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hPowerAnimFXDict, 16, false, resClientRequestSendReferentCommand);
	}
	return 1;
}


AUTO_STARTUP(PowerAnimFX) ASTRT_DEPS(WorldLib, CombatConfig, PowerEmits, DynAnimStances, WorldLibMain);
void PowerAnimFXLoad(void)
{
	// Initialization
	s_uiEmptyStanceID = hashStringInsensitive("0 0 0 0");
	s_uiEmptyPersistStanceID = hashStringInsensitive("0 0");
	eaiPush(&s_piFramesBeforeHitZero,0);
	assert(ParserFindColumn(parse_PowerAnimFX, "bfUsedFields", &s_iUsedFieldsIndex));
	assert(ParserFindColumn(parse_PowerAnimFX, "SpeedPenaltyDuringCharge", &s_iSpeedPenaltyDuringChargeIndex));
	assert(ParserFindColumn(parse_PowerAnimFX, "SpeedPenaltyDuringActivate", &s_iSpeedPenaltyDuringActivateIndex));
	s_pchBitActivateOutOfRange = allocAddString("ACTIVATE_OUT_OF_RANGE");
	s_pchBitActivateNoTarget = allocAddString("ACTIVATE_NO_TARGET");

	if(IsServer() || isDevelopmentMode())
	{
		int flags = PARSER_OPTIONALFLAG;
		if (IsServer())
		{
			flags |= RESOURCELOAD_SHAREDMEMORY;
		}
		resLoadResourcesFromDisk(g_hPowerAnimFXDict,"powerart",".powerart", "PowerAnimFX.bin", flags);
	}

	if(!IsServer())
	{
		// Flush the dictionary
		RefSystem_ClearDictionary(g_hPowerAnimFXDict, false);
	}
}

#include "AutoGen/PowerAnimFX_h_ast.c"
