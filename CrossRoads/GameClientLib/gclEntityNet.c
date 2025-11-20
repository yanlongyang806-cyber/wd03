/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "CharacterAttribs.h"
#include "gclEntityNet.h"
#include "gclEntity.h"
#include "EntityLib.h"
#include "GameClientLib.h"
#include "structnet.h"
#include "gclEntity.h"
#include "gclDemo.h"
#include "AutoGen/Entity_h_ast.h"
#include "entitysysteminternal.h"
#include "GraphicsLib.h"
#include "AttribMod.h"
#include "CostumeCommon.h"
#include "CostumeCommonGenerate.h"
#include "inventoryCommon.h"
#include "netpacketutil.h"
#include "gclTransformation.h"

// For debugging game server client data discrencies
#include "file.h"
static Entity *s_EntCheckCopy;
S32 gAllowDebugEntityCheck = false;
AUTO_CMD_INT(gAllowDebugEntityCheck, DebugEntityCheck) ACMD_CMDLINE ACMD_ACCESSLEVEL(1);

//#define PRINT_ENTITY_CREATES_AND_DELETES 1

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

static void gclEntityDelete(S32 entIndex,
							S32 noFade);

void entDestroyLogReceiver(Entity* e);

static struct {
	S32*		entReceiveOrder;
	U32			packetCountSinceFullUpdate;
	
	struct {
		U32		hasPacketVerifyData : 1;
		U32		doCompareBefore		: 1;
	} flags;
} gclReceive;


// for debugging purposes
S32 gclNumEntities(void)
{
	return eaiSize(&gclReceive.entReceiveOrder);
}

void gclResetEntReceiveOrder(void)
{
	eaiSetSize(&gclReceive.entReceiveOrder, 0);
}

static void gclReceiveCheckString(	Packet* pak,
									const char* string)
{
	MM_CHECK_STRING_READ(pak, string);
	
	if(gclReceive.flags.hasPacketVerifyData){
		char buffer[30];
		
		pktGetString(pak, SAFESTR(buffer));
		
		if(strcmp(buffer, string)){
			assertmsgf(0, "Expected check string \"%s\" but received \"%s\"", string, buffer);
		}
	}
}

//used for debug purposes so we can get useful error messages when structReceive fails
static Entity *spReceivingEntity = NULL;

static void gclSetReceivingEntity(Entity *pEnt)
{
	spReceivingEntity = pEnt;
}

char *OVERRIDE_LATELINK_GetRecvFailCommentString(void)
{
	static char *spRetVal = NULL;
	if (spReceivingEntity)
	{
		estrPrintf(&spRetVal, "While receiving %s: ", ENTDEBUGNAME(spReceivingEntity));
		return spRetVal;
	}
	else
	{
		return "Not during normal entity receive: ";
	}

}

static void gclReceiveEntityStruct(	Packet* pak,
									Entity* e,
									S32 isDiff)
{
	gclReceiveCheckString(pak, "eb");
	
	START_BIT_COUNT(pak, "ParserRecv");
	gclSetReceivingEntity(e);
	ParserRecv(	parse_Entity,
				pak,
				e,
				isDiff ? RECVDIFF_FLAG_COMPAREBEFORESENDING : 0);
	STOP_BIT_COUNT(pak);
	gclSetReceivingEntity(NULL);

	gclReceiveCheckString(pak, "ee");
}

static void gclReceiveMovement(	Packet* pak,
								MovementManager* mm,
								S32 fullUpdate,
								RecordedEntityUpdate* recUpdate)
{
	gclReceiveCheckString(pak, "mb");

	mmReceiveFromServer(mm,
						pak,
						fullUpdate,
						recUpdate);

	gclReceiveCheckString(pak, "me");
}

static void gclReceiveEntityBucket( Packet* pak,
									Entity* e)
{
	U32 bucketFlags;

	gclReceiveCheckString(pak, "bb");
	START_BIT_COUNT(pak, "BucketRecv");

	bucketFlags = pktGetBits(pak, ENT_BUCKET_BITS);
	if(bucketFlags)
	{
		if(bucketFlags & ENT_BUCKET_ENTITYFLAGS)
		{
			START_BIT_COUNT(pak, "EntityFlags");
			e->myEntityFlags = pktGetBitsAuto(pak);
			e->myCodeEntityFlags = pktGetBitsAuto(pak);
			e->myDataEntityFlags = pktGetBitsAuto(pak);
			STOP_BIT_COUNT(pak);
		}

		if(bucketFlags & ENT_BUCKET_MODSNET)
		{
			START_BIT_COUNT(pak, "ModsNet");
			character_ModsNetReceive(e->pChar,pak);
			STOP_BIT_COUNT(pak);
		}

		if(bucketFlags & ENT_BUCKET_ATTRIBS)
		{
			START_BIT_COUNT(pak, "Attribs");
			character_AttribsNetReceive(e->pChar,pak);
			STOP_BIT_COUNT(pak);
		}

		if(bucketFlags & ENT_BUCKET_ATTRIBS_INNATE)
		{
			START_BIT_COUNT(pak, "AttribsInnate");
			character_AttribsInnateNetReceive(e->pChar,pak);
			STOP_BIT_COUNT(pak);
		}

		if(bucketFlags & ENT_BUCKET_LIMITEDUSE)
		{
			START_BIT_COUNT(pak, "LimitedUse");
			character_LimitedUseReceive(e->pChar,pak);
			STOP_BIT_COUNT(pak);
		}

		if(bucketFlags & ENT_BUCKET_CHARGEDATA)
		{
			START_BIT_COUNT(pak, "ChargeData");
			character_ChargeDataReceive(e->pChar,pak);
			STOP_BIT_COUNT(pak);
		}
	}

	STOP_BIT_COUNT(pak);
	gclReceiveCheckString(pak, "be");
}

// Used for debugging client and server data differences
static S32 s_lastTest = 0;

//once we've done all the crazily complicated things to decide whether or not a given packet is a valid diff update to
//apply to a given entity, this function actually does the applying
static void gclReceiveEntityDiff(Packet* pak, Entity* e)
{
	RecordedEntityUpdate* recUpdate = NULL;
	
	if(entIsLocalPlayer(e)){
		START_BIT_COUNT(pak, "local");
	}else{
		START_BIT_COUNT(pak, "non-local");
	}

	// debugging code for find fields on the ent that are being changed incorrectly by the client
	if(gAllowDebugEntityCheck && isDevelopmentMode() && e == entActivePlayerPtr() && s_EntCheckCopy)
	{
		// get the index of the packet as we will need to reset it after applying it to the check copy
		S32 iIdx = pktGetIndex(pak);
		bool bOk = true;

		s_lastTest = StructCompare(parse_Entity, e, s_EntCheckCopy, 0, 0, TOK_CLIENT_ONLY | TOK_NO_NETSEND | TOK_SERVER_ONLY);
		// a place to set a breakpoint
		if(s_lastTest != 0)
		{
			bOk = false;
		}

		// apply diff to copy
		gclReceiveEntityStruct(pak, s_EntCheckCopy, 1);
		pktSetIndex(pak, iIdx);
		// apply to real character
		gclReceiveEntityStruct(pak, e, 1);

		s_lastTest = StructCompare(parse_Entity, e, s_EntCheckCopy, 0, 0, TOK_CLIENT_ONLY | TOK_NO_NETSEND | TOK_SERVER_ONLY);
		// a place to set a breakpoint
		if(bOk && s_lastTest != 0)
		{
			bOk = false;
		}
	}
	else
	{
		gclReceiveEntityStruct(pak, e, 1);
	}

	if(demo_recording())
		recUpdate = StructAlloc(parse_RecordedEntityUpdate);

	gclReceiveMovement(pak, e->mm.movement, 0, recUpdate);

	gclReceiveEntityBucket(pak, e);

	if(TRUE_THEN_RESET(e->costumeRef.dirtiedCount)){
		if(TRUE_THEN_RESET(e->costumeRef.transformation)){
			gclTransformation_BeginTransformation(e);
		} else {
			costumeGenerate_FixEntityCostume(e);
		}

		demo_RecordEntityCostumeChange(e);
	}

	demo_RecordEntityUpdate(e, recUpdate);
	
	STOP_BIT_COUNT(pak);
}

static void gclDeleteClientOnlyWithSameRef(EntityRef entRef)
{
	U32 iter;
	if(gclClientOnlyEntityIterCreate(&iter)){
		ClientOnlyEntity* coe;
		while(coe = gclClientOnlyEntityIterGetNext(iter)){
			if(coe->oldEntityRef == entRef && !coe->noAutoFree){
				gclClientOnlyEntityDestroy(&coe);
				break;
			}
		}
		gclClientOnlyEntityIterDestroy(&iter);
	}
}

void gclDeleteAllClientOnlyEntities(void)
{
	U32 iter;
	if(gclClientOnlyEntityIterCreate(&iter)){
		ClientOnlyEntity* coe;
		while(coe = gclClientOnlyEntityIterGetNext(iter)){
			if(!coe->noAutoFree){
				gclClientOnlyEntityDestroy(&coe);
			}
		}
		gclClientOnlyEntityIterDestroy(&iter);
	}
}

/*this function has several cases:
-the entity already exists... clear its fields and read in the new fields
-the entity does not exist, and the slot is empty... create a new entity with the specified ID, read in the new fields
-the entity does not exist, and its slot is occupied... delete the
  current entity and create a new one and read in the new fields
*/
static void gclReceiveEntityFull(Packet *pak, int iEntIndex, int iEntityRefID, S32 localEntityIndex)
{
	Entity*						e = ENTITY_FROM_INDEX(iEntIndex);
	GlobalType					eEntityType;
	RecordedEntityUpdate*		recUpdate = NULL;
	
	START_BIT_COUNT(pak, "entityType");
		eEntityType = pktGetBitsPack(pak, 2);
	STOP_BIT_COUNT(pak);

	//ent already exists
	if(	e &&
		e->myEntityType != GLOBALTYPE_NONE &&
		ID_FROM_REFERENCE(e->myRef) == (EntityRef)iEntityRefID)
	{
		int i;

	    int oldEntityRef = e->myRef;
		//entities can not currently change type
		assert(eEntityType == e->myEntityType);
		
		gclDeleteClientOnlyWithSameRef(e->myRef);

		gclCleanupEntity(e,true);

		for (i = 0; i < entNumLocalPlayers(); i++)
		{
			if (e->myRef == entPlayerRef(i))
			{
				if (i != localEntityIndex - 1)
				{
					entSetPlayerRef(i, 0);
				}
			}
		}

		// This will actually get sent as a diff now, but we need to do the full cleanup/setup functions
		//StructDeInit(parse_Entity, pEntity);
		gclReceiveEntityStruct(pak, e, 0);

		#if PRINT_ENTITY_CREATES_AND_DELETES
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Receiving entity container %d:%d: id=0x%x (%s)\n",
						e->myEntityType,
						e->myContainerID,
						e->myRef,
						e->debugName);
		#endif
		
		gclInitializeEntity(e,true);

		// If we're currently recording a demo, have the movement manager create a record of this packet as it receives it
		if(demo_recording())
			recUpdate = StructAlloc(parse_RecordedEntityUpdate);

		gclReceiveMovement(pak, e->mm.movement, 1, recUpdate);

		gclReceiveEntityBucket(pak, e);

		demo_RecordEntityDestruction(oldEntityRef, true);
		demo_RecordEntityCreation(e);
		demo_RecordEntityUpdate(e, recUpdate);
	}
	//ent slot is empty
	else if (!e)
	{
		e = entCreateNewFromEntityRef(eEntityType, MakeEntityRef(iEntIndex, iEntityRefID), "gclReceiveEntityFull creating new entity for empty slot");

		gclDeleteClientOnlyWithSameRef(e->myRef);

		gclReceiveEntityStruct(pak, e, 0);
		
		//printf("ent->mySendFlags == %d\n", e->mySendFlags);

		#if PRINT_ENTITY_CREATES_AND_DELETES
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Receiving entity container %d:%d: id=0x%x (%s)\n",
						e->myEntityType,
						e->myContainerID,
						e->myRef,
						e->debugName);
		#endif

		objAddExistingContainerToRepository(e->myEntityType,e->myContainerID,e);

		if(demo_recording())
			recUpdate = StructAlloc(parse_RecordedEntityUpdate);

		gclReceiveMovement(pak, e->mm.movement, 1, recUpdate);

		gclReceiveEntityBucket(pak, e);

		demo_RecordEntityCreation(e);
		demo_RecordEntityUpdate(e, recUpdate);
	}
	//an entity exists in the slot... check if this full update packet is timely or out of order
	else
	{
		Entity *pOldEntity = ENTITY_FROM_INDEX(iEntIndex);

		EntityRef oldEntityRef = entGetRef(pOldEntity);

	
		gclEntityDelete(iEntIndex, true);



		e = entCreateNewFromEntityRef(eEntityType, MakeEntityRef(iEntIndex, iEntityRefID), "gclReceiveEntityFull creating new entity replacing old one in same slot");


		gclDeleteClientOnlyWithSameRef(e->myRef);

		gclReceiveEntityStruct(pak, e, 0);

		#if PRINT_ENTITY_CREATES_AND_DELETES
			printfColor(COLOR_BRIGHT|COLOR_GREEN,
						"Receiving entity container %d:%d: id=0x%x (%s)\n",
						e->myEntityType,
						e->myContainerID,
						e->myRef,
						e->debugName);
		#endif

		objAddExistingContainerToRepository(e->myEntityType,e->myContainerID,e);

		if(demo_recording())
			recUpdate = StructAlloc(parse_RecordedEntityUpdate);

		gclReceiveMovement(pak, e->mm.movement, 1, recUpdate);

		gclReceiveEntityBucket(pak, e);

		demo_RecordEntityDestruction(oldEntityRef, true);
		demo_RecordEntityCreation(e);
		demo_RecordEntityUpdate(e, recUpdate);
	}

	e->costumeRef.dirtiedCount = 0;
	costumeGenerate_FixEntityCostume(e);

	// debugging code for find fields on the ent that are being changed incorrectly by the client
	if(gAllowDebugEntityCheck && isDevelopmentMode() && e == entActivePlayerPtr())
	{
		// make a copy of the player ent (active)
		if(s_EntCheckCopy)
		{
			StructDestroy(parse_Entity, s_EntCheckCopy);
		}

		s_EntCheckCopy = StructClone(parse_Entity, e);
	}
}

static void gclMakeFadeOutEntityFromServerEntity(NOCONST(Entity)* e)
{
	ClientOnlyEntity* coe = gclClientOnlyEntityCreate(false);
	
	if(coe)
	{
		NOCONST(Entity)* eCopy = CONTAINER_NOCONST(Entity, coe->entity);
		
		coe->oldEntityRef = entGetRef((Entity*)e);

		copyVec3(e->posNextFrame, eCopy->posNextFrame);
		eCopy->locationNextFrameValid = e->locationNextFrameValid;
		copyVec3(e->pos_use_accessor, eCopy->pos_use_accessor);
		copyQuat(e->rot_use_accessor, eCopy->rot_use_accessor);
		copyVec2(e->pyFace_use_accessor, eCopy->pyFace_use_accessor);
		eCopy->fEntitySendDistance = e->fEntitySendDistance;

		// Move a bunch of basic values.

		#define MOVE_ENTITY_VALUE(x) eCopy->x = e->x;e->x = 0
			MOVE_ENTITY_VALUE(posViewIsAtRest);
			MOVE_ENTITY_VALUE(rotViewIsAtRest);
			MOVE_ENTITY_VALUE(pyFaceViewIsAtRest);
			MOVE_ENTITY_VALUE(frameWhenViewChanged);
			MOVE_ENTITY_VALUE(frameWhenViewSet);

			MOVE_ENTITY_VALUE(dyn.guidRoot);
			MOVE_ENTITY_VALUE(dyn.guidLocation);
			MOVE_ENTITY_VALUE(dyn.guidSkeleton);
			MOVE_ENTITY_VALUE(dyn.guidDrawSkeleton);
			MOVE_ENTITY_VALUE(dyn.guidFxMan);

			MOVE_ENTITY_VALUE(fHue);

			MOVE_ENTITY_VALUE(costumeRef.pEffectiveCostume);
			MOVE_ENTITY_VALUE(costumeRef.pStoredCostume);
			MOVE_ENTITY_VALUE(costumeRef.pSubstituteCostume);
			MOVE_ENTITY_VALUE(costumeRef.pcDestructibleObjectCostume);
			
			MOVE_ENTITY_VALUE(mm.movement);
			MOVE_ENTITY_VALUE(mm.glr);

			MOVE_ENTITY_VALUE(mm.mrSurface);
			MOVE_ENTITY_VALUE(mm.mrFlight);
			MOVE_ENTITY_VALUE(mm.mrDoorGeo);
			MOVE_ENTITY_VALUE(mm.mrTactical);
			MOVE_ENTITY_VALUE(mm.mrDead);
			MOVE_ENTITY_VALUE(mm.mrInteraction);
			MOVE_ENTITY_VALUE(mm.mrEmote);
			MOVE_ENTITY_VALUE(mm.mrDisabled);
			MOVE_ENTITY_VALUE(mm.mrDisabledCSR);

			MOVE_ENTITY_VALUE(mm.mdhIgnored);
			MOVE_ENTITY_VALUE(mm.mdhDisconnected);

			MOVE_ENTITY_VALUE(mm.mnchVanity);
			MOVE_ENTITY_VALUE(mm.mnchPowers);
			MOVE_ENTITY_VALUE(mm.mnchCostume);
			MOVE_ENTITY_VALUE(mm.mnchExpression);
			MOVE_ENTITY_VALUE(mm.mnchAttach);
			MOVE_ENTITY_VALUE(mm.mcbHandle);
			MOVE_ENTITY_VALUE(mm.mcbHandleDbg);
			MOVE_ENTITY_VALUE(mm.mcgHandle);
			MOVE_ENTITY_VALUE(mm.mcgHandleDbg);
		#undef MOVE_ENTITY_VALUE
		
		mmSetUserPointer(eCopy->mm.movement, eCopy);
		mmSetNetReceiveNoCollFG(eCopy->mm.movement, 1);

		mmDestroyBodies(eCopy->mm.movement);

		#define MOVE_ENTITY_HANDLE(x) eCopy->x = e->x;MOVE_HANDLE(eCopy->x, e->x)
			MOVE_ENTITY_HANDLE(hWLCostume);
			MOVE_ENTITY_HANDLE(costumeRef.hReferencedCostume);
		#undef MOVE_ENTITY_HANDLE

		// Set the fadeout.
		eCopy->fAlpha = e->fAlpha;
		eCopy->bFadeOutAndThenRemove = true;
	}
}

static void gclEntityDelete(S32 entIndex,
							S32 noFade)
{
	Entity* e = ENTITY_FROM_INDEX(entIndex);

	//check if the entity exists
	if(e->myEntityType == GLOBALTYPE_NONE)
	{
		#if PRINT_ENTITY_CREATES_AND_DELETES
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"Deleting entity index that doesn't exist %d\n",
						entIndex);
		#endif
	}
	else
	{
		#if PRINT_ENTITY_CREATES_AND_DELETES
			printfColor(COLOR_BRIGHT|COLOR_RED,
						"Deleting entity container %d:%d: id=0x%x (%s)\n",
						e->myEntityType,
						e->myContainerID,
						e->myRef,
						e->debugName);
		#endif

		// Record this destruction.  Needs to happen before the entity actuall gets destroyed so we have a ref to it
		demo_RecordEntityDestruction(e->myRef, noFade);

		// before we make the fade-out entity, stop any recording we may have on the entity
		// and then cleanup the log receiver because creating the fade-out entity will transfer most of the 
		// dyn and mm stuff to the client-only entity
		mmDebugStopRecording(e);
		entDestroyLogReceiver(e);

		// Make a fade-out entity.
		if(	!noFade &&
			!GET_REF(e->hCreatorNode) &&
			!gbNoGraphics)
		{
			gclMakeFadeOutEntityFromServerEntity(CONTAINER_NOCONST(Entity, e));
		}
		
		if(!objRemoveContainerFromRepository(	e->myEntityType,
												e->myContainerID))
		{
			assert(0);
		}
	}
}


void gclEntityDeleteForDemo(S32 entRef, bool noFade)
{
	S32 entIndex = INDEX_FROM_REFERENCE(entRef);
	Entity* e = ENTITY_FROM_INDEX(entIndex);
	if (!e)
	{
		ErrorDetailsf("Entity ref %d.\n", entRef);
		Errorf("Warning: demo may be double-deleting an entity.\n");
	}
	else
		gclEntityDelete( entIndex, noFade );
}

static void gclReceiveHeader(Packet* pak)
{
	U32 flags = pktGetBits(pak, 1);
	
	gclReceive.flags.hasPacketVerifyData = !!(flags & 1);
}

bool gclHandleEntityUpdate(Packet *pak)
{
	static S32* newEntIndices;
	
	U32 processCount = 0;
	
	eaiSetSize(&newEntIndices, 0);
	
	gclReceiveHeader(pak);

	START_BIT_COUNT(pak, "mmReceiveHeaderFromServer");
		mmClientReceiveHeaderFromServer(pak, &processCount);
	STOP_BIT_COUNT(pak);

	demo_RecordMMHeader(processCount);

	START_BIT_COUNT(pak, "createsAndDeletes");
	{
		U32 indexUpdates;
		
		START_BIT_COUNT(pak, "indexUpdateFlags");
			indexUpdates = pktGetBits(pak, 3);
		STOP_BIT_COUNT(pak);
		
		if(indexUpdates & 1){
			eaiDestroy(&gclReceive.entReceiveOrder);
			gclDeleteAllClientOnlyEntities();
			
			gclReceive.packetCountSinceFullUpdate = 0;
			
			// Destroy all the existing entities.
			
			FOR_BEGIN(i, gHighestActiveEntityIndex + 1);
				if(ENTITY_FROM_INDEX(i)){
					gclEntityDelete(i, 0);
				}
			FOR_END;
		}else{
			gclReceive.packetCountSinceFullUpdate++;
		}
		
		if(indexUpdates & 2)
		{
			// Entities are being deleted.
			
			S32 count;
			
			assert(!(indexUpdates & 1));
		
			gclReceiveCheckString(pak, "deletes");
			
			START_BIT_COUNT(pak, "deleteCount");
				count = pktGetBitsAuto(pak);
			STOP_BIT_COUNT(pak);
			
			FOR_BEGIN(i, count);
				S32 index;
				S32 noFade;

				START_BIT_COUNT(pak, "deletedEntIndex");
					index = pktGetBitsAuto(pak);
				STOP_BIT_COUNT(pak);
				
				noFade = index & 1;
				index >>= 1;
				
				//printf("deleting ent: %d\n", index);
				
				gclEntityDelete(index, noFade);

				if(eaiFindAndRemove(&gclReceive.entReceiveOrder, index) < 0)
				{
					// Entity should have existed.
					
					assert(0);
				}
			FOR_END;
		}
		
		if(indexUpdates & 4)
		{
			// Entities are being created.
			
			S32 count;
		
			gclReceiveCheckString(pak, "creates");

			START_BIT_COUNT(pak, "createCount");
				count = pktGetBitsAuto(pak);
			STOP_BIT_COUNT(pak);
			
			FOR_BEGIN(i, count);
				S32 index;
				
				START_BIT_COUNT(pak, "newEntIndex");
					index = pktGetBitsAuto(pak);
				STOP_BIT_COUNT(pak);
				
				//printf("creating ent: %d\n", index);

				eaiPush(&newEntIndices, index);
				
				// Won't have received a delete if this is the same entity with a different send type.
				
				eaiFindAndRemove(&gclReceive.entReceiveOrder, index);
			FOR_END;

			// Count total entities + COEs and make sure they are under a certain cap
			gclClientOnlyEntitiyEnforceCap(eaiSize(&newEntIndices) + eaiSize(&gclReceive.entReceiveOrder));
		}
	}
	STOP_BIT_COUNT(pak);

	// Receive basic diffed entities.

	gclReceiveCheckString(pak, "diffs");

	if(eaiSize(&gclReceive.entReceiveOrder)){
		START_BIT_COUNT(pak, "diffs");
			EARRAY_INT_CONST_FOREACH_BEGIN(gclReceive.entReceiveOrder, i, isize);
				S32 index = gclReceive.entReceiveOrder[i];
				
				gclReceiveEntityDiff(pak, ENTITY_FROM_INDEX(index));
			EARRAY_FOREACH_END;
		STOP_BIT_COUNT(pak);
	}
	
	// Receive full updates.
	
	gclReceiveCheckString(pak, "fulls");

	if(eaiSize(&newEntIndices)){
		START_BIT_COUNT(pak, "fulls");
			EARRAY_INT_CONST_FOREACH_BEGIN(newEntIndices, i, isize);
				S32 index = newEntIndices[i];
				S32 refID;
				S32 localEntityIndex;
				
				START_BIT_COUNT(pak, "refID");
					refID = pktGetBits(pak, ENTITY_REF_ID_BITS);
				STOP_BIT_COUNT(pak);

				START_BIT_COUNT(pak, "localEntityIndex");
					localEntityIndex = pktGetBitsPack(pak, 1);
				STOP_BIT_COUNT(pak);

				if(localEntityIndex)
				{
					entSetPlayerRef(localEntityIndex - 1, MakeEntityRef(index, refID));
				}

				gclReceiveEntityFull(pak, index, refID, localEntityIndex);
			EARRAY_FOREACH_END;
		STOP_BIT_COUNT(pak);
	}
		
	gclReceiveCheckString(pak, "done");
	
	eaiPushEArray(&gclReceive.entReceiveOrder, &newEntIndices);

	mmClientReceiveFooterFromServer(pak);

	return true;
}
