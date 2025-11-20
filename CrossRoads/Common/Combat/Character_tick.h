/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef CHARACTER_TICK_H__
#define CHARACTER_TICK_H__
GCC_SYSTEM

// Forward declarations
typedef struct Character		Character;
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct Power Power;

// Defines how often the combat aspect of characters is processed
//#define CHARACTER_TICK_PERIOD 0.1

// Globally available track of how long it's been since
extern F32 g_fCharacterTickTime;

typedef enum ECharacterPhase
{
	ECharacterPhase_NONE,
	ECharacterPhase_ONE,
	ECharacterPhase_TWO,
	ECharacterPhase_THREE,
} ECharacterPhase;

// used during the server's character combat tick updating to set the current state
void characterPhase_SetPhase(ECharacterPhase ePhase);
ECharacterPhase characterPhase_GetCurrentPhase();

// Accumulates time and then outputs how much to apply once it rolls over
void character_AccumulateTickTime(	F32 deltaSeconds,
									F32* secondsToApplyOut);

// Takes the input time and rounds it up to the next combat tick.  This helps
//  ensure that tick-based events have consistent times between server and
//  client
F32 character_TickRound(F32 fTime);

// Exposed call for CharacterTickQueue
void character_TickQueue(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate);

// game-server side pre-tick phase. Done on every combat tick regardless of combat sleeping
void character_TickPrePhaseOne(int iPartitionIdx, Character *pchar);

// Does all the character processing before attrib mods are accumulated
void character_TickPhaseOne(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract);

// Does all the accrual of mods and post-processing
void character_TickPhaseTwo(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract);

// Does the post-post-processing (after everyone has been affected)
void character_TickPhaseThree(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, F32 fRate, GameAccountDataExtract *pExtract);

// Special client version of character_TickPhaseTwo()
void character_TickPhaseTwoClient(SA_PARAM_NN_VALID Character *pchar, F32 fRate);

// Special function to operate on "offline" Characters and make them look like
//  they would if they were online
void character_TickOffline(int iPartitionIdx, SA_PARAM_NN_VALID Character *pchar, GameAccountDataExtract *pExtract);

// Returns the time the character expects to be able to start a new activation
U32 character_PredictTimeForNewActivation(Character *pchar, S32 bOverride, S32 bCooldownGlobalNotChecked, Power* pPowToQueue, F32* pfDelayInSeconds);

void CharacterTickRecharge(int iPartitionIdx, Character *pchar, F32 fRate, U32 uiTimeLoggedOut);

void character_CheckQueuedTargetUpdate(int iPartitionIdx, Character *pchar);

#endif