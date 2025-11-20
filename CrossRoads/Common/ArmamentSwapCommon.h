#ifndef ARMAMENT_SWAP_COMMON_H
#define ARMAMENT_SWAP_COMMON_H

#include "referencesystem.h"
typedef struct CharacterClass CharacterClass;
typedef struct Entity Entity;

AUTO_STRUCT;
typedef struct ArmamentActiveItemSwap
{
	S32 iBagID;
	
	// the index of the active slot 
	// (based on the number of active weapons an active weapon bag is able to have)
	S32 iIndex;
		
	// the slot to make active	
	S32 iActiveSlot;

} ArmamentActiveItemSwap;


AUTO_STRUCT;
typedef struct ArmamentSwapInfo
{
	REF_TO(CharacterClass)		hClassSwap;				

	ArmamentActiveItemSwap		**eaActiveItemSwap;		

	U32	bHasQueuedArmaments;							AST(SERVER_ONLY)

} ArmamentSwapInfo;


ArmamentActiveItemSwap* findQueuedActiveItemSwap(ArmamentSwapInfo *pArmamentSwap, S32 bagId, S32 index);


#endif
