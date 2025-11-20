/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character_target.h"
#include "CombatConfig.h"
#include "EntCritter.h"
#include "EntityIterator.h"
#include "EntityAttach.h"
#include "EntityInteraction.h"
#include "EntityLib.h"
#include "EntityMovementTactical.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "GameClientLib.h"
#include "GameStringFormat.h"
#include "gclExtern.h"
#include "gclPlayerControl.h"
#include "gclUIGen.h"
#include "gclUtils.h"
#include "gclSendToServer.h"
#include "GfxConsole.h"
#include "GraphicsLib.h"
#include "mapstate_common.h"
#include "mechanics_common.h"
#include "Player.h"
#include "RegionRules.h"
#include "SavedPetCommon.h"
#include "species_common.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "dynFxManager.h"

#include "Character.h"
#include "CharacterClass.h"
#include "ClientTargeting.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "PowersMovement.h"
#include "UIGen.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "dynAnimInterface.h"
#include "dynFxInterface.h"
#include "dynFxInfo.h"
#include "dynNode.h"
#include "dynSkeleton.h"
#include "dynDraw.h"
#include "gclDemo.h"
#include "gclEntity.h"
#include "wlEncounter.h"
#include "wlVolumes.h"
#include "wlInteraction.h"
#include "contact_common.h"

#include "GfxPrimitive.h"

#include "TextFilter.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "gclChat.h"
#include "mission_common.h"
#include "Guild.h"
#include "Team.h"
#include "SharedBankCommon.h"

#include "NotifyCommon.h"

#include "queue_common.h"
#include "AutoGen/queue_common_h_ast.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/gclEntity_h_ast.h"
#include "AutoGen/gclEntity_c_ast.h"
#include "AutoGen/EntityInteraction_h_ast.h"
#include "AutoGen/Player_h_ast.h"

#include "GroupProjectCommon.h"
#include "AutoGen/GroupProjectCommon_h_ast.h"

#include "AutoGen/dynFxInfo_h_ast.h"

// TODO: this stuff is a placeholder for getting an expression context on the client side for the FSM editor
#include "Expression.h"
#include "FSMEditorMain.h"
#include "StateMachine.h"
#include "WorldLib.h"
// TODO: end

#include "team.h"

#define PRINT_ENTITY_CLEANING 0
static bool gclIsEnemyToLocalPlayer(const char *pchFaction);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

extern ParseTable parse_FSMStateTrackerEntry[];
#define TYPE_parse_FSMStateTrackerEntry FSMStateTrackerEntry
extern ParseTable parse_ExprContext[];
#define TYPE_parse_ExprContext ExprContext

static MacroEditData* s_pMacroEditData = NULL;

//caches the door that will take player to an entity, so UI can point at it.
static EntRegionDoorForUIClamping **s_eaRegionDoorPosCache = NULL;

static S32 gclServerUnresponsiveOrDisconnected(void)
{
	return	gclServerIsDisconnected() ||
			gclServerTimeSinceRecv() > 5.f ||
			!mmIsSyncWithServerEnabled();
}

AUTO_COMMAND ACMD_NAME(setpos) ACMD_ACCESSLEVEL(4);
void gclEntSetPos(Entity* pEnt, const Vec3 vPos)
{
	if (!pEnt) {
		conPrintf("No entity to setpos on");
		return;
	}
	if(gclServerUnresponsiveOrDisconnected())
	{
		Entity *pMount;
		if (pMount = entGetMount(pEnt))
		{
			entSetPos(pMount, vPos, 1, "not connected");
		}
		entSetPos(pEnt, vPos, 1, "not connected");
	}
	
	ServerCmd_setpos(vPos);
}

// Returns whether or not the shared bank is enabled for this entity, i.e. has more than 0 slots
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsSharedBankEnabled");
bool entity_IsSharedBankEnabled(SA_PARAM_NN_VALID Entity *pEntity)
{
	if(pEntity->pPlayer)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		if(pExtract && SharedBank_GetNumSlots(pEntity, pExtract, true) > 0)
		{
			return true;
		}
	}

	return false;
}

// Returns whether or not the shared bank is ready for this entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsSharedBankReady");
bool entity_IsSharedBankReady(SA_PARAM_OP_VALID Entity *pEntity)
{
	static U32 uLastTime = 0;
	static U32 bCreatedOnce = 0;

	if(pEntity && pEntity->pPlayer)
	{
		U32 uTm = timeSecondsSince2000();

		Entity *pBankEntity = GET_REF(pEntity->pPlayer->hSharedBank);
		if(pBankEntity && pBankEntity->myContainerID == pEntity->pPlayer->accountID)
		{
			return true;
		}

		// get a subscription to the bank container
		if(uTm > uLastTime)
		{
			bool bDoCreate = false;

			// Check to make sure container is created/already exists the first time we are using this account 
			if(bCreatedOnce != pEntity->pPlayer->accountID)
			{
				bCreatedOnce = pEntity->pPlayer->accountID;
				bDoCreate = true;
			}

			// send to server (max every two seconds)
			uLastTime = uTm + 2;
			ServerCmd_SharedBankInit(bDoCreate);
		}
	}

	return false;
}

// Returns whether or not the guild bank is ready for this entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsGuildBankReady");
bool entity_IsGuildBankReady(SA_PARAM_NN_VALID Entity *pEntity)
{
	static U32 uLastTime = 0;
	static U32 bCreatedOnce = 0;

	if(pEntity->pPlayer && pEntity->pPlayer->pGuild)
	{
		Guild *pGuild = guild_GetGuild(pEntity);
		Entity *pGuildBank = guild_GetGuildBank(pEntity);
		if(pGuildBank && pGuildBank->myContainerID == pEntity->pPlayer->pGuild->iGuildID)
		{
			return true;
		}
		
		// get a subscription to the bank container
		if(timeSecondsSince2000() > uLastTime)
		{
			bool bDoCreate = false;

			// Check to make sure container is created/already exists the first time we are using this account 
			if(!IS_HANDLE_ACTIVE(pEntity->pPlayer->pGuild->hGuildBank) || bCreatedOnce != pEntity->pPlayer->pGuild->iGuildID)
			{
				bCreatedOnce = pEntity->pPlayer->pGuild->iGuildID;
				bDoCreate = true;
			}

			// send to server (max every two seconds)
			uLastTime = timeSecondsSince2000() + 2;
			ServerCmd_GuildBankInit(bDoCreate);
		}
	}

	return false;
}

AUTO_COMMAND ACMD_NAME(setposOffset) ACMD_ACCESSLEVEL(4);
void gclEntSetPosOffset(Entity* e, const Vec3 vecOffset)
{
	Vec3 pos;

	if(!e){
		return;
	}

	entGetPos(e, pos);
	addVec3(pos, vecOffset, pos);
	gclEntSetPos(e, pos);
}

void gclUpdateControlsInputMoveFace(Entity *pEnt, F32 yaw) 
{
	if (entIsLocalPlayer(pEnt))
	{
		gclPlayerControl_SetMoveAndFaceYawByEnt(pEnt, yaw);
	}
}

AUTO_COMMAND ACMD_NAME(setGameCamYaw) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void gclSetGameCamYaw(Entity *pEnt, F32 yaw)
{
	GfxCameraController *pOldCamera = gfxGetActiveCameraController();

	if(gbNoGraphics)
	{
		return;
	}
	
	gclUpdateControlsInputMoveFace(pEnt, addAngle(yaw, PI));
	
	gfxSetActiveCameraController(&gGCLState.pPrimaryDevice->gamecamera, false);
	gGCLState.pPrimaryDevice->gamecamera.campyr[1] = yaw;
	gGCLState.pPrimaryDevice->gamecamera.targetpyr[1] = yaw;
	devassertmsg(FINITEVEC3(gGCLState.pPrimaryDevice->gamecamera.targetpyr), "Undefined camera PYR!");
	if (!gGCLState.pPrimaryDevice->gamecamera.inited) {
		gGCLState.pPrimaryDevice->gamecamera.pyr_preinit = 1;
	}
	gfxSetActiveCameraController(pOldCamera, false);
}

AUTO_COMMAND ACMD_NAME(setGameCamPYR) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclSetGameCamPYR(Entity *pEnt, Vec3 pyr)
{
	GfxCameraController *pOldCamera = gfxGetActiveCameraController();

	if(gbNoGraphics)
	{
		return;
	}

	gclUpdateControlsInputMoveFace(pEnt, addAngle(pyr[1], PI));

	gfxSetActiveCameraController(&gGCLState.pPrimaryDevice->gamecamera, false);
	copyVec3(pyr, gGCLState.pPrimaryDevice->gamecamera.campyr);
	copyVec3(pyr, gGCLState.pPrimaryDevice->gamecamera.targetpyr);
	devassertmsg(FINITEVEC3(gGCLState.pPrimaryDevice->gamecamera.targetpyr), "Undefined camera PYR!");
	if (!gGCLState.pPrimaryDevice->gamecamera.inited) {
		gGCLState.pPrimaryDevice->gamecamera.pyr_preinit = 1;
	}
	gfxSetActiveCameraController(pOldCamera, false);
}

AUTO_COMMAND ACMD_NAME(setpyr) ACMD_ACCESSLEVEL(4);
void gclEntSetPYR(Entity* pEnt, const Vec3 pyr)
{
	if (!pEnt) {
		conPrintf("No entity to setpyr on");
		return;
	}
	if(gclServerUnresponsiveOrDisconnected())
	{
		Entity *pMount;
		
		Quat rot;
		PYRToQuat(pyr, rot);
		if (pMount = entGetMount(pEnt))
		{
			entSetRot(pMount, rot, 1, "not connected");
		}
		entSetRot(pEnt, rot, 1, "not connected");
	}
	
	{
		Vec3 camPYR;
		copyVec3(pyr, camPYR);
		camPYR[1] = addAngle(camPYR[1], PI);
		gclSetGameCamPYR(pEnt, camPYR);
	}
	
	ServerCmd_setpyr(pyr);
}

static EntityRef *localPlayers;

Entity *entPlayerPtr(int player)
{
	if (player < 0 || player >= entNumLocalPlayers() )
	{
		return NULL;
	}
	return entFromEntityRefAnyPartition(localPlayers[player]);
}

EntityRef entPlayerRef(int player)
{
	if (player < 0 || player >= entNumLocalPlayers())
	{
		return 0;
	}
	return localPlayers[player];
}
void entSetPlayerRef(int player,EntityRef ref)
{
	if (player >= eaiSize(&localPlayers))
	{
		eaiSetSize(&localPlayers, player + 1);
	}
	eaiSet(&localPlayers, ref, player);
}

bool entIsLocalPlayer(Entity *ent)
{
	if(ent)
	{
		int i;
		for (i = 0; i < entNumLocalPlayers(); i++)
		{
			if (localPlayers[i] == entGetRef(ent))
			{
				return true;
			}
		}
	}
	return false;
}

Entity *entActivePlayerPtr(void)
{
	// note that this can return NULL if it is called after logout/mapmove
	// UIGens sometimes exist a frame after logout/mapmove (which we should fix).
	//-SIP 23FEB2013
	return entPlayerPtr(0);
}

Entity *entActiveOrSelectedPlayer(void)
{
	Entity *pEnt = entPlayerPtr(0);
	return pEnt;
}

Character *characterActivePlayerPtr(void)
{
	Entity *pEnt = entActivePlayerPtr();
	return pEnt ? pEnt->pChar : NULL;
}

Player *playerActivePlayerPtr(void)
{
	Entity *pEnt = entActivePlayerPtr();
	return pEnt ? pEnt->pPlayer : NULL;
}

void entClearLocalPlayers(void)
{
	eaiClear(&localPlayers);
}

int entNumLocalPlayers(void)
{
	return eaiSize(&localPlayers);
}

//TODO(MM) Make user be able to chose which unit of measurement to use
F32 entConvertUOM(Entity *pEntity, F32 fLength, const char** ppchUnitsOut, bool bGetAbbreviatedUnits)
{
	static char s_pchUnits[12];
	static WorldRegionType s_eLastRegion = -1;
	static F32 s_fDistanceScale = 0.0f;
	static MeasurementType s_eType = -1;
	static MeasurementSize s_eSize = -1;

	if ( s_eLastRegion != entGetWorldRegionTypeOfEnt(pEntity))
	{
		RegionRules *pRules = getRegionRulesFromEnt(pEntity);
		if ( pRules )
		{
			s_fDistanceScale = pRules->fDefaultDistanceScale;
			s_eType = pRules->eDefaultMeasurement;
			s_eSize = pRules->eMeasurementSize;

			if ( bGetAbbreviatedUnits )
				sprintf(s_pchUnits, "%s",  TranslateMessageRef(pRules->dmsgDistUnitsShort.hMessage));
			else
				sprintf(s_pchUnits, "%s", TranslateMessageRef(pRules->dmsgDistUnits.hMessage));

			s_eLastRegion = entGetWorldRegionTypeOfEnt(pEntity);
		}
	}

	if ( s_eLastRegion >= 0 )
	{
		fLength = BaseToMeasurement(fLength,s_eType,s_eSize) * s_fDistanceScale;
		if ( ppchUnitsOut )
		{
			(*ppchUnitsOut) = s_pchUnits;
		}
	}

	return fLength;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntConvertUOMAsString");
const char *gclEntConvertUOM(SA_PARAM_OP_VALID Entity *pPlayerEnt, F32 fDistance, S32 iSignificantDigits)
{
	static char pchBuffer[260];
	const char* pchUnits = "";
	F32 fFinalDistance = pPlayerEnt ? entConvertUOM(pPlayerEnt,fDistance,&pchUnits,true) : 0.0f;
	StringFormatNumberSignificantDigits(pchBuffer, fFinalDistance, iSignificantDigits, true, true);
	sprintf(pchBuffer,"%s %s", pchBuffer, pchUnits);
	return pchBuffer;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetDistanceFromPointAsString");
const char *gclEntGetDistanceFromPointAsString(SA_PARAM_NN_VALID Entity *pPlayerEnt, F32 fX, F32 fY, F32 fZ)
{
	Vec3 vPoint;
	setVec3(vPoint, fX, fY, fZ);
	return gclEntConvertUOM(pPlayerEnt, entGetDistance(pPlayerEnt,NULL,NULL,vPoint,NULL), 3);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetScaledDistanceFromPlayerAsString");
const char *entExprGetScaledDistanceFromPlayerAsString(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char* pchFormat)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	if (pEntity && pEntPlayer)
	{
		static char pchBuffer[260];
		char* pchUnits;
		F32 fDistance = entGetDistance(pEntPlayer,NULL,pEntity,NULL,NULL);
		fDistance = entConvertUOM(pEntPlayer,fDistance,&pchUnits,true);
		StringFormatNumberSignificantDigits(pchBuffer, fDistance, 3, true, true);
		sprintf(pchBuffer,"%s %s", pchBuffer, pchUnits);
		return pchBuffer;
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetDistanceFromPlayer");
F32 entExprGetDistanceFromPlayer(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEntity)
	{
		return entGetDistance(pEnt,NULL,pEntity,NULL,NULL);
	}
	return -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerGetDistanceFromTarget");
F32 entExprPlayerGetDistanceFromTarget(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if ( pEnt && pEnt->pChar && pEnt->pPlayer )
	{
		Entity* pEntTarget = entFromEntityRefAnyPartition(pEnt->pChar->currentTargetRef);
		WorldInteractionNode* pNodeTarget = GET_REF(pEnt->pChar->currentTargetHandle);
		if ( pEntTarget )
		{
			return entGetDistance( pEnt, NULL, pEntTarget, NULL, NULL );
		}
		else if ( pNodeTarget )
		{
			Vec3 vClose;
			character_FindNearestPointForObject(pEnt->pChar,NULL,pNodeTarget,vClose,true);
			return entGetDistance( pEnt, NULL, NULL, vClose, NULL );
		}
	}
	return -1;
}

AUTO_COMMAND ACMD_NAME(ExportActiveCharacter) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
char* gclExportActiveCharacter(ACMD_SENTENCE outputDir)
{
	Entity *ent = entActivePlayerPtr();

	if (!ent) 
	{
		return "You must be logged into a character run this command.";
	}

	ServerCmd_ExportEntityPlayer(outputDir);
	return STACK_SPRINTF("Asking ObjectDB to export character \"%s\" to: %s", ent->debugName, outputDir);
}

//This command will save the current entityplayer to the local disk.
// The first parameter is an existing directory to save to.
// ***This command will not save SERVER_ONLY data and should not be used for reconstructing the character.
// You may optionally pass "OVERWRITE" as a second parameter to overwrite existing saved files.
AUTO_COMMAND ACMD_NAME(ExportLocalCharacter) ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
char* gclExportLocalCharacter(ACMD_SENTENCE outputDir)
{
	char *fileName = 0;
	char *optionalParam;
	static char result[1024];
	Entity *ent = entActivePlayerPtr();
	bool overwrite = false;

	if (!ent)
	{
		sprintf(result, "Could not get active player.");
		return result;
	}

	estrStackCreate(&fileName);
	estrPrintf(&fileName, "%s", outputDir);

	if (optionalParam = strstri(fileName, "OVERWRITE"))
	{
		overwrite = true;
		optionalParam[0] = '\0';
		optionalParam--;
		while (optionalParam >= fileName && (IS_WHITESPACE(*optionalParam) || *optionalParam == '\\' || *optionalParam == '/'))
		{
			*optionalParam = '\0';
			optionalParam--;
		}
		if (optionalParam >= fileName)
			estrSetSize(&fileName, (optionalParam - fileName) + 1);
		else
			estrSetSize(&fileName, 0);
	}

	if (!dirExists(fileName))
	{
		sprintf(result, "Output directory does not exist: %s", outputDir);
		estrDestroy(&fileName);
		return result;
	}

	estrConcatf(&fileName, "\\%d.ENTITYPLAYER.con",ent->myContainerID);

	if (fileExists(fileName))
	{
		if (!overwrite)
		{
			sprintf(result, "File already exists at: %s\nYou can overwrite this file by adding OVERWRITE to the end of this command.", fileName);
			estrDestroy(&fileName);
			return result;
		}
	}
	else
		overwrite = false;

	//write the file
	if (!ParserWriteTextFile(fileName,parse_Entity,ent,TOK_PERSIST,0)) 
		sprintf(result, "Failed to write container ENTITYPLAYER[%d]: Can't Write Data", ent->myContainerID);
	else if (overwrite)
		sprintf(result, "Overwrote current player to: %s", fileName);
	else
		sprintf(result, "Saved current player to: %s", fileName);

	estrDestroy(&fileName);
	return result;
}

static void entLogDynNode(	Entity* e,
							const DynNode* node)
{
	Vec3			posNode;
	const DynNode*	child = node->pChild;

	dynNodeGetWorldSpacePos(node, posNode);

	for(child = node->pChild; child; child = child->pSibling){
		Vec3 posChild;

		dynNodeGetWorldSpacePos(child, posChild);

		mmLogSegment(	e->mm.movement,
						NULL,
						"dyn.skeleton",
						0,
						posNode,
						posChild);

		entLogDynNode(e, child);
	}
}

AUTO_RUN;
void gclEntityAutoRunInitialize(void)
{
	dynSetHitReactImpactFuncs(	mmDynFxHitReactCallback,
								mmDynAnimHitReactCallback);

	worldLibSetEnemyFactionCheckFunc(gclIsEnemyToLocalPlayer);

	ui_GenInitStaticDefineVars(InteractOptionTypeEnum, "InteractOptionType_");
	ui_GenInitIntVar("EntityPlayer", GLOBALTYPE_ENTITYPLAYER);
	ui_GenInitIntVar("EntitySavedPet", GLOBALTYPE_ENTITYSAVEDPET);
	ui_GenInitIntVar("EntitySharedBank", GLOBALTYPE_ENTITYSHAREDBANK);
	ui_GenInitIntVar("EntityGuildBank", GLOBALTYPE_ENTITYGUILDBANK);
}

void gclEntityMovementManagerMsgHandler(const MovementManagerMsg* msg){
	Entity*				e = msg->userPointer;
	MovementManager*	mm = SAFE_MEMBER(e, mm.movement);
	
	if(	!e &&
		MM_MSG_IS_FG(msg->msgType))
	{
		return;
	}

	switch(msg->msgType){
		xcase MM_MSG_FG_LOG_VIEW:{
			Vec3 posCur;
			Quat rotCur;
			Mat3 matCur;
			
			entGetPos(e, posCur);
			entGetRot(e, rotCur);
			
			quatToMat(rotCur, matCur);

			mmLog(	mm,
					NULL,
					"[entView.cur] entity view: pos(%1.3f, %1.3f, %1.3f)",
					vecParamsXYZ(posCur));
			
			FOR_BEGIN(i, 3);
				mmLogSegmentOffset(	mm,
									NULL,
									"entView.cur",
									0xff000000 | (0xff << ((2 - i) * 8)),
									posCur,
									matCur[i]);
			FOR_END;

			if(e->locationNextFrameValid){
				mmLog(	mm,
						NULL,
						"[entView.next] entity view: pos(%1.3f, %1.3f, %1.3f)",
						vecParamsXYZ(e->posNextFrame));

				FOR_BEGIN(i, 3);
					mmLogSegmentOffset(	mm,
										NULL,
										"entView.next",
										0xff000000 | (0xff << ((2 - i) * 8)),
										e->posNextFrame,
										matCur[i]);
				FOR_END;
			}
		}

		xcase MM_MSG_FG_SET_VIEW:{
			e->frameWhenViewSet = frameFromMovementSystem;

			// for projectiles, check if we are not moving yet and shouldn't be drawn
			if (entIsProjectile(e))
			{	
				if (msg->fg.setView.netViewInitUnmoving)
				{
					e->bForceFadeOut = true;
					entSetCodeFlagBits(e,ENTITYFLAG_DONOTDRAW);
				}
				else
				{
					e->bForceFadeOut = false;
					entClearCodeFlagBits(e,ENTITYFLAG_DONOTDRAW);
				}
			}
			//

			if(	gfxWillWaitForZOcclusion() ||
				FALSE_THEN_SET(e->locationNextFrameValid))
			{
				if(msg->fg.setView.vec3Pos){
					copyVec3(msg->fg.setView.vec3Pos, e->posNextFrame);
				}
			}

			entSetPosRotFace(	e,
								e->posNextFrame,
								msg->fg.setView.quatRot,
								msg->fg.setView.vec2FacePitchYaw,
								false,
								false,
								__FUNCTION__);

			if(msg->fg.setView.vec3Pos){
				copyVec3(msg->fg.setView.vec3Pos, e->posNextFrame);

				if(msg->fg.setView.vec3Offset){
					entSetDynOffset(e, msg->fg.setView.vec3Offset);
				}
			}
		}
	
		xcase MM_MSG_FG_LOG_SKELETON:{
			DynSkeleton* pSkel = dynSkeletonFromGuid(e->dyn.guidSkeleton);

			if(	SAFE_MEMBER(pSkel, pRoot) &&
				pSkel->pDrawSkel &&
				!pSkel->pDrawSkel->bDontDraw)
			{
				entLogDynNode(e, pSkel->pRoot);
			}
		}

		xcase MM_MSG_FG_FIRST_VIEW_SET:{
			// Set the camera behind e.
		}
		
		xcase MM_MSG_FG_LOG_BEFORE_SIMULATION_SLEEPS:{
			DynSkeleton* pSkel = dynSkeletonFromGuid(e->dyn.guidSkeleton);

			if (!gConf.bNewAnimationSystem)
			{
				mmLog(	mm,
					NULL,
					"[sequencer] Logging %d sequencers BEGIN (guid:pSkel = 0x%x:0x%p).",
					pSkel ? eaSize(&pSkel->eaSqr) : 0,
					e->dyn.guidSkeleton,
					pSkel);

				if(pSkel){
					FOR_EACH_IN_EARRAY_FORWARDS(pSkel->eaSqr, DynSequencer, pSqr)
					{
						mmLog(	mm,
							NULL,
							"[sequencer] Sequencer 0x%p:",
							pSqr);

						mmLog(	mm,
							NULL,
							"%s",
							dynSequencerGetLog(pSqr));
					}
					FOR_EACH_END;
				}

				mmLog(mm, NULL, "[sequencer] Logging sequencers END.");
			}
		}
		
		xcase MM_MSG_FG_NEARBY_GEOMETRY_DESTROYED:{
			entInvalidateSplats(e);
		}

		xdefault:{
			entMovementDefaultMsgHandler(msg);
		}
	}
}

// Initializes an entity on the client
void gclInitializeEntity(Entity* e, bool isReloading)
{
	PERFINFO_AUTO_START_FUNC();

	entInitializeCommon(e, true);
	
	mmSetMsgHandler(e->mm.movement, gclEntityMovementManagerMsgHandler);
	
	e->pEntUI = StructCreate(parse_EntityUI);

	if (e->pChar)
	{	
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
		character_ResetPowersArray(entGetPartitionIdx(e), e->pChar, pExtract);
	}

	if (isReloading || e->costumeRef.pcDestructibleObjectCostume || e->bNoInterpAlphaOnSpawn)
	{
		// If we're reloading or are a destructible object, don't interp the alpha
		e->bNoInterpAlpha = 1;
		
		// Set the alpha to be 1 so it doesn't flicker
		e->fAlpha = 1.0f;
		e->fCameraCollisionFade = 1.0f;
	}

	if (gConf.bNewAnimationSystem &&
		SAFE_MEMBER2(e, pCritter, bSetSpawnAnim))
	{
		dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, 1.0f, 0.0f);
		e->fHideTime = 0.5f;
	}
	else
	{
		dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, 1.0f, 1.0f);
	}

	// Project specific initialization
	gclExternInitializeEntity(e, isReloading);

	PERFINFO_AUTO_STOP();
}

// Cleans up an entity on the client
void gclCleanupEntity(Entity* e, bool isReloading)
{	
	PERFINFO_AUTO_START_FUNC();

	#if PRINT_ENTITY_CLEANING
		if(e->myRef){
			printfColor(COLOR_RED|COLOR_GREEN,
						"Cleaning entity container %d:%d: id=0x%x (%s)\n",
						e->myEntityType,
						e->myContainerID,
						e->myRef,
						e->debugName);
		}
	#endif

	entPreCleanupCommon(e, isReloading);

	gclExternCleanupEntity(e, isReloading);

	dtFxManDestroy(e->dyn.guidFxMan);
	e->dyn.guidFxMan = 0;

	entCleanupCommon(entGetPartitionIdx(e), e, isReloading, false);

	PERFINFO_AUTO_STOP();
}


void *entClientCreateCB(ContainerSchema *sc)
{
	assertmsg(0,"This should never be called! Don't create entities on the client");
}

void entClientInitCB(ContainerSchema *sc, void *obj)
{
    Entity *ent = (Entity*)obj;
	gclInitializeEntity(ent,false);
}

void entClientDeInitCB(ContainerSchema *sc, void *obj)
{
    Entity *ent = (Entity*)obj;
	int i;
	for (i = 0; i < entNumLocalPlayers(); i++)
	{
		if (ent->myRef == entPlayerRef(i))
		{
			entSetPlayerRef(i, 0);
		}
	}
	gclCleanupEntity(ent,false);
}

void entClientDestroyCB(ContainerSchema *sc, void *obj, const char* file, int line)
{
    Entity *ent = (Entity*)obj;
	entDestroyEx(ent, file, line);
}

__forceinline static EntityVisibilityState entGetVisibilityState(Entity *pEnt)
{
	F32 wd, ht;
	Vec3 vBoundMin, vBoundMax, vCamSrc, vCamTarget;
	GfxCameraView *view = gfxGetActiveCameraView();
	WorldCollCollideResults results;
	Mat4 mEntMat;

	entGetLocalBoundingBox(pEnt, vBoundMin, vBoundMax, false);
	entGetBodyMat(pEnt, mEntMat);

	if( !frustumCheckBoundingBoxNonInline(&view->frustum, vBoundMin, vBoundMax, mEntMat, false) )
		return kEntityVisibility_Hidden;

	// Cast some rays to see if target is visible
	entGetPos(pEnt, vCamTarget );
	wd = MAXF(vBoundMax[0] - vBoundMin[0],vBoundMax[2] - vBoundMin[2]);
	ht = vBoundMax[1] - vBoundMin[1];
	gfxGetActiveCameraPos(vCamSrc);

	// Feet
	vCamTarget[1] += vBoundMin[1];
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_FILTER_BIT_TARGETING, &results )) // should these be CAMERA_BLOCKING?
		return kEntityVisibility_Visible;

	// Head
	vCamTarget[1] += ht;
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_FILTER_BIT_TARGETING, &results ))
		return kEntityVisibility_Visible;

	// Center
	vCamTarget[1] -= ht/2;
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_FILTER_BIT_TARGETING, &results ))
		return kEntityVisibility_Visible;

	// Right
	moveVinX(vCamTarget, view->frustum.cammat, wd/2); 
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_FILTER_BIT_TARGETING, &results ))
		return kEntityVisibility_Visible;

	// right
	moveVinX(vCamTarget, view->frustum.cammat, -wd);
	if(!worldCollideRay(PARTITION_CLIENT, vCamSrc, vCamTarget, WC_FILTER_BIT_TARGETING, &results ))
		return kEntityVisibility_Visible;

	return kEntityVisibility_Hidden;
}

bool entIsVisible(Entity *entity)
{
	Entity *e = entity;
	if (!e->pEntUI)
		e->pEntUI = StructCreate(parse_EntityUI);

	if (e->pEntUI->VisCache.eState == kEntityVisibility_Unknown
		|| e->pEntUI->VisCache.uiLastCheckTimeMs + ENTITY_VIS_CHECK_INTERVAL_MS < g_ui_State.totalTimeInMs)
	{
		e->pEntUI->VisCache.eState = entGetVisibilityState(e);
		e->pEntUI->VisCache.uiLastCheckTimeMs = g_ui_State.totalTimeInMs;
	}
	return (e->pEntUI->VisCache.eState == kEntityVisibility_Visible);
}

F32 entGetWindowScreenPosAndDist(Entity *e, Vec2 pixel_pos, F32 yOffsetInFeet)
{
	int		w,h;
	F32 skelH;
	GfxCameraView *view = gfxGetActiveCameraView();
	Vec3 t, pos3d;

	skelH = entGetHeightBasedOnSkeleton(e);
	gfxGetActiveSurfaceSize(&w, &h);
	entGetPos(e, t);
	t[1] += skelH + yOffsetInFeet;
	mulVecMat4(t, view->frustum.viewmat, pos3d);
	frustumGetScreenPosition(&view->frustum, w, h, pos3d, pixel_pos);
	return view->frustum.znear - pos3d[2];
}

F32 entGetScreenDist(Entity *e)
{
	Vec3 t, pos3d;
	GfxCameraView *view = gfxGetActiveCameraView();

	entGetPos(e, t);
	mulVecMat4(t, view->frustum.viewmat, pos3d);
	return view->frustum.znear - pos3d[2];
}

bool entGetWindowScreenPos(Entity *e, Vec2 pixel_pos, F32 yOffsetInFeet)
{
	S32 w, h;
	F32 fDist;
	fDist = entGetWindowScreenPosAndDist(e, pixel_pos, yOffsetInFeet);
	gfxGetActiveSurfaceSize(&w, &h);
	return (pixel_pos[0] < w) && (pixel_pos[0] >= 0) && (pixel_pos[1] < h) && (pixel_pos[1] >= 0) && fDist >= 0;
}

static bool entGetScreenBoundingBoxInternal(Entity *pEnt, const Vec3 vBoundingBoxMin, const Vec3 vBoundingBoxMax, CBox *pBox, F32 *pfDistance, bool bClipToScreen)
{
	GfxCameraView *pView;
	Vec2 v2Min;
	Vec2 v2Max;
	Mat4 mEntMat;
		
	PERFINFO_AUTO_START_FUNC();
	pView = gfxGetActiveCameraView();
	entGetVisualMat(pEnt, mEntMat);

	if(gfxGetScreenExtents(&pView->frustum, pView->projection_matrix, mEntMat, vBoundingBoxMin, vBoundingBoxMax, v2Min, v2Max, pfDistance, bClipToScreen))
	{
		S32 iWidth;
		S32 iHeight;
		gfxGetActiveSurfaceSize(&iWidth, &iHeight);

		pBox->lx = v2Min[0] * iWidth;
		pBox->hx = v2Max[0] * iWidth;
		pBox->ly = iHeight - v2Min[1] * iHeight;
		pBox->hy = iHeight - v2Max[1] * iHeight;
		CBoxNormalize(pBox);
		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		ZeroStruct(pBox);
		PERFINFO_AUTO_STOP();
		return false;
	}
}

bool entGetScreenBoundingBox(Entity *pEnt, CBox *pBox, F32 *pfDistance, bool bClipToScreen)
{
	Vec3 v3Min;
	Vec3 v3Max;
	entGetLocalBoundingBox(pEnt, v3Min, v3Max, false);
	return entGetScreenBoundingBoxInternal(pEnt, v3Min, v3Max, pBox, pfDistance, bClipToScreen);
}

bool entGetPrimaryCapsuleScreenBoundingBox(Entity *pEnt, CBox *pBox, F32 *pfDistance)
{
	const Capsule *pCap;
	pCap = entGetPrimaryCapsule(pEnt);
	
	if(pCap)
	{
		Vec3 v3Min;
		Vec3 v3Max;
		CapsuleGetBounds(pCap, v3Min, v3Max);
		return entGetScreenBoundingBoxInternal(pEnt, v3Min, v3Max, pBox, pfDistance, true);
	}
	
	return false;
}

CBox *entGetWindowScreenBox(SA_PARAM_NN_VALID Entity *e, CBox *box, F32 yOffsetInFeet, F32 width, F32 height)
{
	Vec2 pos;
	
	if (!box)
		return NULL;

	if (!entGetWindowScreenPos(e, pos, yOffsetInFeet))
		return NULL;

	BuildCBox(box, pos[0]-width*0.5f, pos[1]-height*0.5f, width, height);
	return box;
}

//if the target is in a different region, point to the door to that region instead:
void entGetPosClampedToRegion(Entity* pPlayer, Entity* pTarget, Vec3 targetPosOut){
	EntityRef hTarget = entGetRef(pTarget);
	int i;
	entGetPos(pTarget, targetPosOut);
	if(pTarget->astrRegion != pPlayer->astrRegion){
		EntRegionDoorForUIClamping* regionDoor = NULL;
		for( i = 0; i < eaSize(&s_eaRegionDoorPosCache); i++){
			if(s_eaRegionDoorPosCache[i]->target == hTarget){
				regionDoor = s_eaRegionDoorPosCache[i];
				break;
			}
		}
		if(!regionDoor){
			//didn't find a match.
			regionDoor = StructCreate(parse_EntRegionDoorForUIClamping);
			regionDoor->target = hTarget;
			eaPush(&s_eaRegionDoorPosCache, regionDoor);
		}
		if(regionDoor->targetRegion == pTarget->astrRegion && regionDoor->myRegion == pPlayer->astrRegion){
			//already have the right information
			copyVec3(regionDoor->pos, targetPosOut);
		}else{	
			//need to ask the server to find the door to that region
			if(timeSecondsSince2000() - regionDoor->timeUpdateRequested >= 5){
				//ask server only every 5 seconds (picked arbitrarily):
				ServerCmd_waypoint_CmdGetRegionDoorPosForMapIcon(hTarget);
				regionDoor->timeUpdateRequested = timeSecondsSince2000();
			}
		}
	} 
}

//called by the server command waypoint_CmdGetRegionDoorPosForMapIcon(), which is called in
//entGetPosClampedToRegion to find waypoints to entities on different regions from the player.
//This stores the results so we don't have to ask the server until player or target changes region.
AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void updateRegionDoorPosForMapIcon(Entity *pPlayerEnt, EntityRef target, Vec3 pos, char* targetRegion, char* playerRegion){
	int i;
	EntRegionDoorForUIClamping *regionDoor = NULL;

	//if there's one for this target, use that:
	for(i = eaSize(&s_eaRegionDoorPosCache) - 1; i >= 0; i--){
		if(s_eaRegionDoorPosCache[i]->target == target){
			regionDoor = s_eaRegionDoorPosCache[i];
			break;
		}
		//otherwise make one:
	}
	if(!regionDoor){
		//make new entry then do stuff.
		regionDoor = StructCreate(parse_EntRegionDoorForUIClamping);
		eaPush(&s_eaRegionDoorPosCache, regionDoor);
		regionDoor->target = target;
	}
	//populate it:
	if (playerRegion && playerRegion[0]){
		regionDoor->myRegion = allocAddString(playerRegion);
	}else{
		regionDoor->myRegion = NULL;	//the default region has a NULL name. :(
	}
	if (targetRegion && targetRegion[0]){
		regionDoor->targetRegion = allocAddString(targetRegion);
	}else{
		regionDoor->targetRegion = NULL;	//the default region has a NULL name. :(
	}
	copyVec3(pos, regionDoor->pos);
}


void gclEntUpdateAttach(Entity *e)
{
	if (!SAFE_MEMBER(e, pAttach))
	{
		return;
	}
	if (e->dyn.guidRoot && e->dyn.guidSkeleton)
	{		
		if (e->pAttach->erAttachedTo)
		{			
			Entity *entAttach = entFromEntityRefAnyPartition(e->pAttach->erAttachedTo);
			if (!entAttach || !entAttach->dyn.guidSkeleton)
			{
				return;
			}
			dtNodeSetParentBone(e->dyn.guidRoot, entAttach->dyn.guidSkeleton, e->dyn.guidSkeleton, e->pAttach->pBoneName, dynBitFromName(e->pAttach->pExtraBit));
			mmLog(	e->mm.movement,
					NULL,
					"Setting dynPos (%f, %f, %f) as attachment.",
					vecParamsXYZ(e->pAttach->posOffset));
			dtNodeSetPos(e->dyn.guidRoot, e->pAttach->posOffset);
			dtNodeSetRot(e->dyn.guidRoot, unitquat);
		}
	}
}

// TODO: this stuff is a placeholder for getting an expression context on the client side for the FSM editor
void entFSMGroupsAddFuncs(const char *path, const char *unique_name, ExprContext *context)
{
	ExprFuncTable* funcTable = exprContextCreateFunctionTable();
	exprContextAddFuncsToTableByTag(funcTable, "ai");
	exprContextAddFuncsToTableByTag(funcTable, "ai_powers");
	exprContextAddFuncsToTableByTag(funcTable, "ai_movement");
	exprContextAddFuncsToTableByTag(funcTable, "entity");
	exprContextAddFuncsToTableByTag(funcTable, "entityutil");
	exprContextAddFuncsToTableByTag(funcTable, "gameutil");
	exprContextAddFuncsToTableByTag(funcTable, "util");
	exprContextSetFuncTable(context, funcTable);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetName");
const char *entExprGetName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
    const char *pchName = pEntity ? entGetLocalName(pEntity) : NULL;
    return pchName ? pchName : TranslateMessageKeySafe("InvalidEntityName");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSubName");
const char *entExprGetSubName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	const char *pchName = pEntity ? entGetLocalSubName(pEntity) : NULL;
	return pchName ? pchName : TranslateMessageKeySafe("InvalidEntityName");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSubNameNoDefault");
const char *entExprGetSubNameNoDefault(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	const char *pchName = pEntity ? entGetLocalSubNameNoDebug(pEntity) : NULL;
	return NULL_TO_EMPTY(pchName);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPuppetName");
const char *entGetPuppetName(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity)
{
	const char *pchName = pEntity && pEntity->pSaved && pEntity->pSaved->pPuppetMaster ? pEntity->pSaved->pPuppetMaster->curPuppetName : NULL;
	return pchName ? pchName : TranslateMessageKeySafe("InvalidEntityName");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetNameByRef");
const char *entExprGetNameByRef(ExprContext *pContext, EntityRef iEntRef)
{
    Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
    return entExprGetName(pContext, pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetDisplayName");
const char *entExprGetDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
    const char *pchDisplayName = pEntity ? entGetAccountOrLocalName(pEntity) : NULL;
    return pchDisplayName ? pchDisplayName : TranslateMessageKeySafe("InvalidEntityName");
}

// Function to get the allegiance from an entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetAllegianceFromPlayer);
const char *entExprGetAllegiance(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Function to get the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAllegiance);
const char *entExprGetAllegiance(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	const char* pchAllegiance = pEntity ? REF_STRING_FROM_HANDLE(pEntity->hAllegiance) : NULL;
	return pchAllegiance ? pchAllegiance : "";
}

// Function to set the allegiance on an entity. Calls a remote command on the gameserver to do this
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(EntSetAllegiance);
void entExprSetAllegiance(ExprContext *pContext, const char *pchAllegianceName)
{
	ServerCmd_AllegianceSetValidate(pchAllegianceName);
}

// Function to get the display name of the allegiance from an entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetAllegianceDispNameFromPlayer);
const char *entExprGetAllegianceDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Function to get the display name of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAllegianceDisplayName);
const char *entExprGetAllegianceDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef* pDef = pEntity ? GET_REF(pEntity->hAllegiance) : NULL;
	return pDef ? TranslateDisplayMessage(pDef->displayNameMsg) : "";
}

// Get the short description of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAllegianceDescription);
const char *entExprGetAllegianceDescription(SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef *pDef = pEntity ? GET_REF(pEntity->hAllegiance) : NULL;
	return pDef ? TranslateDisplayMessage(pDef->descriptionMsg) : "";
}

// Get the long description of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetAllegianceDescriptionLong);
const char *entExprGetAllegianceDescriptionLong(SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef *pDef = pEntity ? GET_REF(pEntity->hAllegiance) : NULL;
	return pDef ? TranslateDisplayMessage(pDef->descriptionLongMsg) : "";
}

// Function to get the allegiance from an entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetSubAllegianceFromPlayer);
const char *entExprGetSubAllegiance(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Function to get the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubAllegiance);
const char *entExprGetSubAllegiance(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	const char* pchSubAllegiance = pEntity ? REF_STRING_FROM_HANDLE(pEntity->hSubAllegiance) : NULL;
	return pchSubAllegiance ? pchSubAllegiance : "";
}

// Function to get the display name of the allegiance from an entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetSubAllegianceDispNameFromPlayer);
const char *entExprGetSubAllegianceDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Function to get the display name of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubAllegianceDisplayName);
const char *entExprGetSubAllegianceDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef* pDef = pEntity ? GET_REF(pEntity->hSubAllegiance) : NULL;
	return pDef ? TranslateDisplayMessage(pDef->displayNameMsg) : "";
}

// Get the short description of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubAllegianceDescription);
const char *entExprGetSubAllegianceDescription(SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef *pDef = pEntity ? GET_REF(pEntity->hSubAllegiance) : NULL;
	return pDef ? TranslateDisplayMessage(pDef->descriptionMsg) : "";
}

// Get the long description of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubAllegianceDescriptionLong);
const char *entExprGetSubAllegianceDescriptionLong(SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef *pDef = pEntity ? GET_REF(pEntity->hSubAllegiance) : NULL;
	return pDef ? TranslateDisplayMessage(pDef->descriptionLongMsg) : "";
}

// Function to get the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubOrMainAllegiance);
const char *entExprGetSubOrMainAllegiance(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	const char* pchAllegiance = !pEntity ? NULL : IS_HANDLE_ACTIVE(pEntity->hSubAllegiance) ? REF_STRING_FROM_HANDLE(pEntity->hSubAllegiance) : REF_STRING_FROM_HANDLE(pEntity->hAllegiance);
	return pchAllegiance ? pchAllegiance : "";
}

// Function to get the display name of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubOrMainAllegianceDisplayName);
const char *entExprGetSubOrMainAllegianceDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef* pDef = !pEntity ? NULL : IS_HANDLE_ACTIVE(pEntity->hSubAllegiance) ? GET_REF(pEntity->hSubAllegiance) : GET_REF(pEntity->hAllegiance);
	return pDef ? TranslateDisplayMessage(pDef->displayNameMsg) : "";
}

// Get the short description of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubOrMainAllegianceDescription);
const char *entExprGetSubOrMainAllegianceDescription(SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef* pDef = !pEntity ? NULL : IS_HANDLE_ACTIVE(pEntity->hSubAllegiance) ? GET_REF(pEntity->hSubAllegiance) : GET_REF(pEntity->hAllegiance);
	return pDef ? TranslateDisplayMessage(pDef->descriptionMsg) : "";
}

// Get the long description of the allegiance from an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSubOrMainAllegianceDescriptionLong);
const char *entExprGetSubOrMainAllegianceDescriptionLong(SA_PARAM_OP_VALID Entity *pEntity)
{
	AllegianceDef* pDef = !pEntity ? NULL : IS_HANDLE_ACTIVE(pEntity->hSubAllegiance) ? GET_REF(pEntity->hSubAllegiance) : GET_REF(pEntity->hAllegiance);
	return pDef ? TranslateDisplayMessage(pDef->descriptionLongMsg) : "";
}

// Get the SpeciesDef from the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpecies);
SA_RET_OP_VALID SpeciesDef *entExprGetSpecies(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	if(pEntity && pEntity->pChar) {
		SpeciesDef* pSpeciesDef = GET_REF(pEntity->pChar->hSpecies);
		if(pSpeciesDef) {
			return pSpeciesDef;
		}
		if (pEntity->pSaved)
		{
			PlayerCostumeSlot *pSlot;
			PlayerCostume *pc = NULL;
			pSlot = eaGet(&pEntity->pSaved->costumeData.eaCostumeSlots, pEntity->pSaved->costumeData.iActiveCostume);
			if (pSlot) {
				pc = pSlot->pCostume;
			}
			if (pc)
			{
				pSpeciesDef = GET_REF(pc->hSpecies);
				if(pSpeciesDef) {
					return pSpeciesDef;
				}
			}
		}
		if (pEntity->pCritter)
		{
			CritterDef *pCritter = GET_REF(pEntity->pCritter->critterDef);
			if (pCritter)
			{
				pSpeciesDef = GET_REF(pCritter->hSpecies);
				if(pSpeciesDef) {
					return pSpeciesDef;
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetDisplayNameByRef");
const char *entExprGetDisplayNameByRef(ExprContext *pContext, EntityRef iEntRef)
{
    Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);
    return entExprGetDisplayName(pContext, pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetDescription");
const char *entExprGetDescription(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	// If there is no current description, return an empty string.  I used an empty string
	//  rather than a null string here because returning NULL from a textexpr prevents the SMF gen 
	//  from updating.
    return( (pEntity && pEntity->pSaved && pEntity->pSaved->savedDescription) ? (pEntity->pSaved->savedDescription) : "");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetOwnerEntity");
SA_RET_OP_VALID Entity* entExprGetOwnerEntity(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	return entGetOwner(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetRef");
U32 entExprGetRef(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	if (!pEntity) return 0;
	return (U32)entGetRef(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetFromRef");
SA_RET_OP_VALID Entity *entExprGetFromRef(ExprContext *pContext, U32 iRef)
{
	return entFromEntityRefAnyPartition(iRef);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetFromContainerID");
SA_RET_OP_VALID Entity *entExprGetFromContainerID(ExprContext *pContext, S32 iType, U32 uID)
{
	return entFromContainerIDAnyPartition(iType, uID);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSprintTimeLeftPct");
F32 entExprGetSprintTimeLeftPct(SA_PARAM_OP_VALID Entity* pEntity)
{
	if ( pEntity )
	{
		F32 fUsed, fTotal, fFuel;
		S32 sUsesFuel;
		entGetSprintTimes( pEntity, &fUsed, &fTotal, &sUsesFuel, &fFuel );

		if (sUsesFuel)
		{
			return fFuel / fTotal;
		}

		if ( fTotal > FLT_EPSILON && fTotal >= fUsed )
		{
			return (fTotal - fUsed) / fTotal;
		}
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSprintCooldownTimePct");
F32 entExprGetSprintCooldownTimePct(SA_PARAM_OP_VALID Entity* pEntity)
{
	if ( pEntity )
	{
		F32 fUsed, fTotal;
		
		if(pEntity->mm.cooldownIsFromRunning)
		{
			entGetSprintCooldownTimes(pEntity, &fUsed, &fTotal);
		}
		else if (pEntity->mm.cooldownIsFromAiming)
		{
			entGetAimCooldownTimes(pEntity, &fUsed, &fTotal);
		}
		else
		{
			entGetRollCooldownTimes(pEntity, &fUsed, &fTotal);
		}
		
		if ( fTotal > FLT_EPSILON )
		{
			return fUsed / fTotal;
		}
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsSprinting");
bool entExprIsSprinting(SA_PARAM_OP_VALID Entity* pEntity)
{
	return pEntity ? entIsSprinting(pEntity) : false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsTargetable");
bool entExprIsTargetable(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	if ( pEntity==NULL )
		return false;

    return entIsTargetable(pEntity);
}

// Deprecated, use EntRayCanHitPlayer.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntRayCanHitCachedPlayer");
bool entMouseRayCanHitPlayer(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bExcludePlayer);

// Check if a ray from the mouse can reach an entity. If the second argument is false,
// include the main player entity in the check, otherwise exclude it.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntRayCanHitPlayer");
bool entMouseRayCanHitPlayer(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bExcludePlayer)
{
	static EntityRef s_hPlayerLastEntity = 0;
	static U32 s_uiLastPlayerCheck = 0;
	static EntityRef s_hNotPlayerLastEntity = 0;
	static U32 s_uiNotLastPlayerCheck = 0;
	U32 uiLastCheck = (bExcludePlayer ? s_uiNotLastPlayerCheck : s_uiLastPlayerCheck);
	EntityRef erEnt = pEntity ? entGetRef(pEntity) : 0;

	if (g_ui_State.totalTimeInMs - uiLastCheck > 100) {
		Entity *pNewEnt = getEntityUnderMouse(bExcludePlayer);
		if (bExcludePlayer)
		{
			s_hNotPlayerLastEntity = pNewEnt ? entGetRef(pNewEnt) : 0;
			s_uiNotLastPlayerCheck = g_ui_State.totalTimeInMs;
		}
		else
		{
			s_hPlayerLastEntity = pNewEnt ? entGetRef(pNewEnt) : 0;
			s_uiLastPlayerCheck = g_ui_State.totalTimeInMs;
		}

		if (pNewEnt && pNewEnt->pEntUI)
		{
			// If a mouse ray can hit it, we know it's visible, so update the vis cache too.
			pNewEnt->pEntUI->VisCache.eState = kEntityVisibility_Visible;
			pNewEnt->pEntUI->VisCache.uiLastCheckTimeMs = g_ui_State.totalTimeInMs;
		}
	}

	return (bExcludePlayer ? s_hNotPlayerLastEntity : s_hPlayerLastEntity) == erEnt;
}

// Deprecated, use EntRayCanHit.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntRayCanHitCached");
bool entMouseRayCanHit(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Check if a ray from the mouse can reach an entity, ignoring the main player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntRayCanHit");
bool entMouseRayCanHit(SA_PARAM_OP_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	return pEntity && entMouseRayCanHitPlayer(pContext, pEntity, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntSetTarget");
void entExprSetTarget(ExprContext *pContext, U32 iRef)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt)
		entity_SetTarget(pEnt, iRef);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPrimaryPet");
SA_RET_OP_VALID Entity *entExprGetPrimaryPet(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar && pEntity->pChar->primaryPetRef)
	{
		return entFromEntityRefAnyPartition(pEntity->pChar->primaryPetRef);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetRider");
SA_RET_OP_VALID Entity *entExprGetRider(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity)
{
	return entGetRider(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsPlayerControlled");
bool entExprIsPlayerControlled(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pEntity)
{
	return entGetType(pEntity) == GLOBALTYPE_ENTITYPLAYER || entIsPrimaryPet(pEntity);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsSavedPet");
bool entExprIsSavedPet(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	return pEntity && entGetType(pEntity) == GLOBALTYPE_ENTITYSAVEDPET;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetType");
S32 entExprGetType( SA_PARAM_OP_VALID Entity* pEnt )
{
	if ( pEnt==NULL )
		return 0;

	return entGetType(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetContainerID");
U32 entExprGetContainerID( SA_PARAM_OP_VALID Entity* pEnt )
{
	if ( pEnt==NULL )
		return 0;
	
	return entGetContainerID(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetSubEntity");
SA_RET_OP_VALID Entity* entExprGetSubEntity( SA_PARAM_OP_VALID Entity* pEnt, S32 iType, U32 iContainerID )
{
	if ( pEnt==NULL || iContainerID==0 )
		return NULL;

	return entity_GetSubEntity( PARTITION_CLIENT, pEnt, iType, iContainerID );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntGetSubEntity");
SA_RET_OP_VALID Entity* entGenExprGetSubEntity(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID UIGen *pGen, const char *pchTypeVar, const char *pchContainerVar)
{
	UIGenVarTypeGlob *pType, *pContainer;

	if (!pEnt || !pchTypeVar || !pchContainerVar || !*pchTypeVar || !*pchContainerVar)
		return NULL;

	pType = eaIndexedGetUsingString(&pGen->eaVars, pchTypeVar);
	if (!pType)
		return NULL;

	pContainer = eaIndexedGetUsingString(&pGen->eaVars, pchContainerVar);
	if (!pContainer)
		return NULL;

	return entity_GetSubEntity(PARTITION_CLIENT, pEnt, pType->iInt, pContainer->iInt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetActiveShipEnt");
SA_RET_OP_VALID Entity* entExprGetActiveShipEnt( SA_PARAM_OP_VALID Entity* pEnt )
{
	if ( pEnt==NULL )
		return NULL;

	if (entGetWorldRegionTypeOfEnt(pEnt) == WRT_Ground)
	{
		return entity_GetPuppetEntityByType(pEnt, "Space", NULL, false, true);
	}

	return pEnt;
}

static int disable_edge_of_region_dist = 0;
AUTO_CMD_INT(disable_edge_of_region_dist, DisableEdgeOfRegionDist) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(CommandLine);

//Is now actually distance to edge of playable volume or region
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetDistToEdgeOfRegion");
F32 entExprGetDistToEdgeOfRegion( SA_PARAM_OP_VALID Entity* pEnt )
{
	extern U32 playableVolumeType;
	WorldRegion *pRegion;
	Vec3 vMin, vMax;
	Vec3 vEntPos;
	F32 dist = FLT_MAX;

	if ( pEnt==NULL || disable_edge_of_region_dist)
		return dist;

	entGetPos(pEnt, vEntPos);

	if(pEnt->volumeCache)
	{
		int i;
		bool bVolFound = false;
		const WorldVolume **volumes = wlVolumeCacheGetCachedVolumes(pEnt->volumeCache);
		for (i = 0; i < eaSize(&volumes); ++i)
		{
			Vec3 volMin, volMax;
			const WorldVolume *world_vol = volumes[i];

			if(!wlVolumeIsType(world_vol, playableVolumeType))
				continue;

			wlVolumeGetWorldMinMax(world_vol, volMin, volMax);

			if(	pointBoxCollision(vEntPos, volMin, volMax) && 
				volMin[0] <= volMax[0] && volMin[1] <= volMax[1] && volMin[2] <= volMax[2])
			{
				dist = MIN(dist, vEntPos[0] - volMin[0]);
				dist = MIN(dist, vEntPos[1] - volMin[1]);
				dist = MIN(dist, vEntPos[2] - volMin[2]);
				dist = MIN(dist, volMax[0] - vEntPos[0]);
				dist = MIN(dist, volMax[1] - vEntPos[1]);
				dist = MIN(dist, volMax[2] - vEntPos[2]);
				bVolFound = true;
			}
		}
		if(bVolFound)
			return dist;
	}

	pRegion = worldGetWorldRegionByPos(vEntPos);
	if( !pRegion || !worldRegionGetBounds(pRegion, vMin, vMax))
		return dist;

	dist = MIN(dist, vEntPos[0] - vMin[0]);
	dist = MIN(dist, vEntPos[1] - vMin[1]);
	dist = MIN(dist, vEntPos[2] - vMin[2]);
	dist = MIN(dist, vMax[0] - vEntPos[0]);
	dist = MIN(dist, vMax[1] - vEntPos[1]);
	dist = MIN(dist, vMax[2] - vEntPos[2]);
	
	return dist;
}

AUTO_RUN;
void entRegisterClientFSMGroups(void)
{
	static ExprContext *entFSMContext = NULL;
	static ExprContext *layerFSMContext = NULL;
	static ExprContext *pfsmContext = NULL;
	
	if(!entFSMContext)
		entFSMContext = exprContextCreate();
	//SET_SELFPTR(context, NULL);
	exprContextSetPointerVar(entFSMContext, "Context", entFSMContext, parse_ExprContext, false, true);
	exprContextSetPointerVar(entFSMContext, "me", NULL, parse_Entity, false, true);
	exprContextSetPointerVar(entFSMContext, "curStateTracker", NULL, parse_FSMStateTrackerEntry, false, true);
	exprContextSetPointerVar(entFSMContext, "targetEnt", NULL, parse_Entity, false, true);
	exprContextAddExternVarCategory(entFSMContext, "Encounter", fsmRegisterExternVar, NULL, fsmRegisterExternVarSCType);

	if(!layerFSMContext)
		layerFSMContext = exprContextCreate();
	exprContextSetPointerVar(layerFSMContext, "Context", layerFSMContext, parse_ExprContext, false, true);
	exprContextSetPointerVar(layerFSMContext, "INTERNAL_LayerFSM", NULL, parse_WorldLayerFSM, false, true);
	exprContextSetPointerVar(layerFSMContext, "curStateTracker", NULL, parse_FSMStateTrackerEntry, false, true);
	exprContextAddExternVarCategory(layerFSMContext, "layer", fsmRegisterExternVar, NULL, fsmRegisterExternVarSCType);

	if(!pfsmContext)
	{
		pfsmContext = exprContextCreate();
		exprContextSetPointerVar(layerFSMContext, "Context", layerFSMContext, parse_ExprContext, false, true);
		exprContextSetPointerVar(layerFSMContext, "curStateTracker", NULL, parse_FSMStateTrackerEntry, false, true);
		exprContextSetPointerVar(entFSMContext, "me", NULL, parse_Entity, false, true);
	}

#ifndef NO_EDITORS
	fsmEditorRegisterGroupEx("ai/BScript", "AI", entFSMContext, entFSMGroupsAddFuncs);
	fsmEditorRegisterGroupEx("ai/EncLayer", "EncounterLayerFSMs", layerFSMContext, NULL);
	fsmEditorRegisterGroupEx("ai/PlayerFSM", "PlayerFSMs", pfsmContext, NULL);
#endif
}

extern ParseTable parse_Team[];
#define TYPE_parse_Team Team
extern ParseTable parse_Guild[];
#define TYPE_parse_Guild Guild
extern ParseTable parse_CurrencyExchangeAccountData[];
#define TYPE_parse_CurrencyExchangeAccountData CurrencyExchangeAccountData

AUTO_RUN_LATE;
int RegisterEntityContainers(void)
{
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
		{
			objRegisterNativeSchema(i,parse_Entity, entClientCreateCB,entClientDestroyCB,entClientInitCB,entClientDeInitCB, NULL);
			RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(i), false, parse_Entity, false, false, NULL);
			resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(i), 1, true, resClientRequestSendReferentCommand);
		}
	}
	// Register team while we're at it
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), false, parse_Team, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), 1, true, resClientRequestSendReferentCommand);
	
	// Register guild while we're at it
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), false, parse_Guild, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), 1, true, resClientRequestSendReferentCommand);

	// Register currency exchange while we're at it
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_CURRENCYEXCHANGE), false, parse_CurrencyExchangeAccountData, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_CURRENCYEXCHANGE), 1, true, resClientRequestSendReferentCommand);

	// Register group projects while we're at it
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), false, parse_GroupProjectContainer, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERGUILD), 1, true, resClientRequestSendReferentCommand);
    RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER), false, parse_GroupProjectContainer, false, false, NULL);
    resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GROUPPROJECTCONTAINERPLAYER), 1, true, resClientRequestSendReferentCommand);

	return 1;
}

Entity *entExternGetCommandEntity(CmdContext *context)
{
	return entActivePlayerPtr();
}

#define TOTAL_ENTITY_CAP 145

typedef struct ClientOnlyEntityIter {
	S32 index;
	
	struct {
		U32	inUse : 1;
	} flags;
} ClientOnlyEntityIter;

static ClientOnlyEntityIter clientOnlyIters[10];
static ClientOnlyEntity**	clientOnlyEntities;

ClientOnlyEntity* gclClientOnlyEntityCreate(bool bMakeRef)
{
	static U32 clientRefs = MAX_ENTITIES_PRIVATE+1;
	ClientOnlyEntity* coe;
	
	coe = callocStruct(ClientOnlyEntity);
	coe->entity = StructCreateWithComment(parse_Entity, "Creating client-only entity");

	if(bMakeRef) {
		coe->entity->myRef = clientRefs;
		clientRefs++;
		if( clientRefs >= (1<<MAX_ENTITIES_BITS) ) {
			clientRefs = MAX_ENTITIES_PRIVATE+1;
		}
	}

	eaPush(&clientOnlyEntities, coe);
	
	return coe;
}

void gclClientOnlyEntityDestroy(ClientOnlyEntity** coeInOut)
{
	ClientOnlyEntity*	coe = SAFE_DEREF(coeInOut);
	S32					removeIndex = eaFindAndRemove(&clientOnlyEntities, coe);

	if(	!coe ||
		removeIndex < 0)
	{
		assert(0);
	}
	
	gclCleanupEntity(coe->entity, false);
	
	StructDestroySafe(parse_Entity, &coe->entity);
	
	SAFE_FREE(*coeInOut);
	
	// Decrement iterators that are past the removeIndex.
	
	ARRAY_FOREACH_BEGIN(clientOnlyIters, i);
		if(	clientOnlyIters[i].flags.inUse &&
			clientOnlyIters[i].index >= removeIndex)
		{
			clientOnlyIters[i].index--;
		}
	ARRAY_FOREACH_END;
}

Entity *gclClientOnlyEntFromEntityRef(EntityRef iRef)
{
	int i;
	for ( i=0; i < eaSize(&clientOnlyEntities); i++ ) {
		if(clientOnlyEntities[i]->entity->myRef == iRef) {
			return clientOnlyEntities[i]->entity;
		}
	}
	return NULL;
}

S32 gclClientOnlyEntityIterCreate(U32* iterOut){
	if(!iterOut){
		return 0;
	}
	
	ARRAY_FOREACH_BEGIN(clientOnlyIters, i);
		if(!clientOnlyIters[i].flags.inUse){
			clientOnlyIters[i].flags.inUse = 1;
			clientOnlyIters[i].index = -1;
			*iterOut = i;
			return 1;
		}
	ARRAY_FOREACH_END;
	
	return 0;
}

S32 gclClientOnlyEntityIterDestroy(U32* iterInOut){
	U32 iter = SAFE_DEREF(iterInOut);
	
	if(	iter >= ARRAY_SIZE(clientOnlyIters) ||
		!clientOnlyIters[iter].flags.inUse)
	{
		return 0;
	}
	
	clientOnlyIters[iter].flags.inUse = 0;
	
	return 1;
}

ClientOnlyEntity* gclClientOnlyEntityIterGetNext(U32 iter){
	if(	iter >= ARRAY_SIZE(clientOnlyIters) ||
		!clientOnlyIters[iter].flags.inUse)
	{
		return NULL;
	}
	
	clientOnlyIters[iter].index++;
	
	assert(clientOnlyIters[iter].index >= 0);
	
	if(clientOnlyIters[iter].index < eaSize(&clientOnlyEntities)){
		return clientOnlyEntities[clientOnlyIters[iter].index];
	}
	
	return NULL;
}

void gclClientOnlyEntitiyEnforceCap(U32 uiNumEntities)
{
	int iNumCOEs;
	int iIdxOffset = 0;
	while ( (iNumCOEs = eaSize(&clientOnlyEntities)) > 0 && iNumCOEs + uiNumEntities > TOTAL_ENTITY_CAP)
	{
		if(iIdxOffset >= iNumCOEs) {
			ErrorDetailsf("Entity count %d, Client-only entity count %d", uiNumEntities, iNumCOEs);
			Errorf("Cutscene Entities are pushing us over the entity cap");
			break;
		} else {
			ClientOnlyEntity* toDestroy = clientOnlyEntities[iIdxOffset];
			if(toDestroy->noAutoFree) {
				iIdxOffset++;
				continue;
			}
			gclClientOnlyEntityDestroy(&toDestroy);
		}
	}
}

void gclClientOnlyEntitiyGetCutsceneEnts(Entity ***pppEnts)
{
	int i;
	for ( i=0; i < eaSize(&clientOnlyEntities); i++ ) {
		if(clientOnlyEntities[i]->isCutsceneEnt)
			eaPush(pppEnts, clientOnlyEntities[i]->entity);
	}
}

S32 gclNumClientOnlyEntities(void)
{
	return eaSize(&clientOnlyEntities);
}

//
// Gets the contact info for an entity and a given player
//
ContactInfo* gclEntGetContactInfoForPlayer(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity)
{
	int i, n = 0;
	ContactInfo* ret = NULL;
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(pEntity && info)
	{
		n = eaSize(&info->nearbyContacts);
		for (i = 0; i < n; i++)
		{
			ContactInfo* contactInfo = info->nearbyContacts[i];
			if (contactInfo->entRef == pEntity->myRef)
			{
				ret = contactInfo;
				break;
			}
		}
	}

	return ret;
}

// This is a pretty specific definition of what is a contact, and moreover it is one that disagrees with the server.  If there is contact info
// present on the client, then the server has decided that this is a contact.  Perhaps this function should be called gclEntHasContactIndicator.  [RMARR - 3/2/11]
bool gclEntGetIsContact(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactInfo* pContactInfo = gclEntGetContactInfoForPlayer(pPlayerEnt, pEntity);

	return pContactInfo && pContactInfo->currIndicator != ContactIndicator_NoInfo;
}

CritterInteractInfo *gclEntGetInteractableCritterInfo(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity)
{
	InteractInfo* info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	if(pEntity && info)
	{
		return eaIndexedGetUsingInt(&info->nearbyInteractCritterEnts, pEntity->myRef);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsContact");
bool entExprIsContact(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	Entity *pPlayer = entActivePlayerPtr();
	CONST_EARRAY_OF(ContactInfo) eaNearbyContacts = SAFE_MEMBER3(pPlayer, pPlayer, pInteractInfo, nearbyContacts);
	if (pPlayer && pEntity)
	{
		int i;
		EntityRef iEntRef = pEntity->myRef;
		for (i = 0; i < eaSize(&eaNearbyContacts); i++)
		{
			const ContactInfo *pContact = eaNearbyContacts[i];
			if (iEntRef && iEntRef == SAFE_MEMBER(pContact, entRef))
			{
				return true;
			}
		}
	}
	return false;
}

// Returns true if the second Entity is a friend to the first Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsFriend");
bool gclEntGetIsFriend(SA_PARAM_OP_VALID Entity *pEntitySource, SA_PARAM_OP_VALID Entity *pEntityTarget)
{
	if(pEntitySource && pEntitySource->pChar && pEntityTarget && pEntityTarget->pChar)
	{
		return character_TargetMatchesTypeRequire(PARTITION_CLIENT, pEntitySource->pChar,pEntityTarget->pChar,kTargetType_Friend);
	}
	return false;
}

// Return true if the entity is a friend to the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsFriendly");
bool entExprIsFriendly(SA_PARAM_OP_VALID Entity *pEntity)
{
	return gclEntGetIsFriend(entActivePlayerPtr(), pEntity);
}

// Return true if the entity is a friend to the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsFriend");
bool entExprIsFriendly(SA_PARAM_OP_VALID Entity *pEntity);

// Returns 1 if the second Entity is a foe to the first Entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsFoe");
bool gclEntGetIsFoe(SA_PARAM_OP_VALID Entity *pEntitySource, SA_PARAM_OP_VALID Entity *pEntityTarget)
{
	if(pEntitySource && pEntitySource->pChar && pEntityTarget && pEntityTarget->pChar)
	{
		return character_TargetIsFoe(PARTITION_CLIENT,pEntitySource->pChar,pEntityTarget->pChar);
	}
	return false;
}

// Returns true if the given faction is a foe to the passed in entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsFactionFoe");
bool gclEntGetIsFactionFoe(SA_PARAM_OP_VALID Entity *pEntity, const char *pchFactionName)
{
	if (pEntity)
	{
		CritterFaction *faction;
		CritterFaction *entFaction;
		ANALYSIS_ASSUME(pEntity);
		faction = pchFactionName ? RefSystem_ReferentFromString(g_hCritterFactionDict, pchFactionName) : NULL;
		entFaction = entGetFaction(pEntity);
		if (faction && entFaction)
		{
			ANALYSIS_ASSUME(faction);
			ANALYSIS_ASSUME(entFaction);
			return faction_GetRelation(entFaction, faction) == kEntityRelation_Foe;
		}
		return false;
	}
	
	return false;
}

// Return true if the entity is a foe of the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsHostile");
bool entExprIsHostile(SA_PARAM_OP_VALID Entity *pEntity)
{
	return gclEntGetIsFoe(entActivePlayerPtr(), pEntity);
}

// Return true if the entity is a foe of the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsEnemy");
bool entExprIsHostile(SA_PARAM_OP_VALID Entity *pEntity);


// Returns 1 if the second Entity is on same PvP team as first entity (only valid on PvP maps)
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsPvPOpponent");
bool gclEntGetIsPvPOpponent(SA_PARAM_OP_VALID Entity *pEntitySource, SA_PARAM_OP_VALID Entity *pEntityTarget)
{
	CritterFaction *pSrcFaction;
	CritterFaction *pTargetFaction;

	if (!pEntitySource || !pEntityTarget || zmapInfoGetMapType(NULL) != ZMTYPE_PVP)
	{
		return false;
	}

	pSrcFaction = GET_REF(pEntitySource->hFactionOverride);
	pTargetFaction = GET_REF(pEntityTarget->hFactionOverride);
	
	if(pSrcFaction && pSrcFaction->pchName && pTargetFaction && pTargetFaction->pchName)
	{
		return (stricmp(pSrcFaction->pchName, pTargetFaction->pchName) != 0);
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerCanFollowEntByRef");
bool entExprPlayerCanFollowEntByRef(ExprContext *pContext, U32 uiEntRef)
{
	return gclPlayerControl_AllowFollowTargetByRef(uiEntRef);
}

ContainerID g_iRequestedEntContID = 0;
static const char *g_pchEntityUIGen = NULL;
static char *g_pchDescVarName = NULL;

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntDescriptionRequest");
void entExpr_RequestEntDescription(SA_PARAM_NN_VALID UIGen *pGen, char *pchVarName, EntityRef iEntRef)
{
	
	Entity *pEnt = entFromEntityRefAnyPartition(iEntRef);

	if(pEnt && 
		pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER &&
		pEnt->pSaved && 
		pEnt->pSaved->savedName && pEnt->pSaved->savedName[0] &&
		pEnt->pPlayer &&
		pEnt->pPlayer->publicAccountName && pEnt->pPlayer->publicAccountName[0])
	{
		char *pchCharID = NULL;
		estrStackCreate(&pchCharID);

		estrPrintf(&pchCharID, "%s@%s", pEnt->pSaved->savedName, pEnt->pPlayer->publicAccountName);
		
		g_iRequestedEntContID = pEnt->myContainerID;
		g_pchEntityUIGen = pGen->pchName;
		if(g_pchDescVarName)
			SAFE_FREE(g_pchDescVarName);
		g_pchDescVarName = strdup(pchVarName);

		ServerCmd_entCmd_GetDescription(pchCharID);

		estrDestroy(&pchCharID);
	}
}

AUTO_COMMAND ACMD_NAME("EntDescriptionReturn") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void entCmd_EntDescriptionReturn(ContainerID iContID, const char *pchDescription)
{
	if(iContID == g_iRequestedEntContID && g_pchEntityUIGen && g_pchDescVarName)
	{
		char *pchFilteredDescription = NULL;
		Entity *pEntity = entActivePlayerPtr();
		ChatConfig *pConfig = ChatCommon_GetChatConfig(pEntity);
		UIGen *pGen = ui_GenFind(g_pchEntityUIGen, kUIGenTypeNone);
		UIGenVarTypeGlob *pGlob = UI_GEN_READY(pGen) ? eaIndexedGetUsingString(&pGen->eaVars, g_pchDescVarName) : NULL;

		if (!pConfig || pConfig->bProfanityFilter)
		{
			strdup_alloca(pchFilteredDescription, pchDescription);
			ReplaceAnyWordProfane(pchFilteredDescription);
		}

		if(pGen && pGlob)
		{
			estrCopy2(&pGlob->pchString, pchFilteredDescription ? pchFilteredDescription : pchDescription);
		}
	}

	g_pchEntityUIGen = NULL;
	SAFE_FREE(g_pchDescVarName);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntDescriptionRequestCancel");
void entExpr_EntDescriptionRequestCancel(void)
{
	g_iRequestedEntContID = 0;
	g_pchEntityUIGen = NULL;
	SAFE_FREE(g_pchDescVarName);
}

// Get the primary mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetPrimaryMission");
const char *exprEntGetPrimaryMission(SA_PARAM_OP_VALID Entity *pEntity)
{
	const char *pcPrimaryMission = entGetPrimaryMission(pEntity);
	return NULL_TO_EMPTY(pcPrimaryMission);
}

bool gclEntIsPrimaryMission(SA_PARAM_NN_VALID Entity *pEntity, const char *pcMission)
{
	if(pcMission && pcMission[0] != 0)
	{
		const char * pcPrimaryMission = entGetPrimaryMission(pEntity);
		if(pcPrimaryMission && stricmp(pcPrimaryMission, pcMission) == 0)
		{
			return true;		
		}
	}

	return false;
}

// Is this mission the primary mission of the team? Added solo mission too.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsPrimaryMission");
bool teamExpr_IsPrimaryMission(SA_PARAM_NN_VALID Entity *pEntity, const char *pcMission)
{
	return gclEntIsPrimaryMission(pEntity, pcMission);
}

// Is this mission the primary mission of the team? Added solo mission too.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsPrimaryMissionOrSubmission");
bool teamExpr_IsPrimaryMissionOrSubmission(SA_PARAM_NN_VALID Entity *pEntity, const char *pcMission)
{
	if(pcMission && pcMission[0] != 0)
	{
		const char *pcColon = strchr(pcMission, ':');
		int iLen = pcColon ? pcColon - pcMission : -1;
		const char *pcPrimaryMission = entGetPrimaryMission(pEntity);
		if (iLen >= 0)
		{
			return pcPrimaryMission && (strnicmp(pcPrimaryMission, pcMission, iLen) == 0);
		}
		else 
		{
			return pcPrimaryMission && (stricmp(pcPrimaryMission, pcMission) == 0);
		}
	}

	return false;
}

// Can this entity change the primary mission (team leader or solo) 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CanChangePrimaryMission");
bool Mission_CanChangePrimaryMission(SA_PARAM_NN_VALID Entity *pEntity)
{

	if(team_IsMember(pEntity))
	{
		if(team_IsTeamLeader(pEntity))
		{
			return true;
		}
	}
	else
	{
		return true;
	}

	return false;
}

// Is this mission hidden (on the hud)? 
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsMissionHidden");
bool MissionExpr_IsMissionHidden(SA_PARAM_NN_VALID Entity *pEntity, const char *pcMission)
{
	if(pcMission && pcMission[0] != 0)
	{
		MissionInfo* info = mission_GetInfoFromPlayer(pEntity);
		if(info)
		{
			S32 i;
			for(i = 0; i < eaSize(&info->missions); ++i)	
			{
				MissionDef *pMisDef = mission_GetDef(info->missions[i]);
				if(pMisDef && stricmp_safe(pcMission, pMisDef->name) == 0)
				{
					return info->missions[i]->bHidden;
				}
			}
		}		
	}

	return false;
}
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerCanUseThisItemTag");
bool entExprPlayerCanUseThisItemTag(const char* pchItemTag)
{
	Entity *pEnt = entActivePlayerPtr();

	if(pEnt && pEnt->pPlayer)
	{
		S32 index = StaticDefineIntGetInt(ItemTagEnum, pchItemTag);
		if(index >= 0)
		{
			if(!Item_ItemTagRequiresSpecialization(index) || entity_PlayerHasSkillSpecialization(pEnt, index))
			{
				return true;
			}
		}	
	}
	
	return false;
}

AUTO_STRUCT;
typedef struct DoorDestinationData
{
	WorldInteractionNode* pNode;	AST(UNOWNED LATEBIND)
	const char* pchMapDisplayName;	AST(UNOWNED)
	ZoneMapType eMapType;
	S32	iNumInstances;
	S32 iNumNonFullInstances;
	S32	iNumPlayers;
	S32	iNumEnabledOpenInstancing;
	S32 iNumTeammates;
	S32 iNumGuildmates;
	S32 iNumFriends;
	S32 iNumMissions;
	bool bHasUsefulInfo;
} DoorDestinationData;


static void gclFillDoorDestinationData(DoorDestinationData* pData, Entity* pEnt, NodeSummary* pNodeData, S32 iPropIndex)
{
	S32 i, j;
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pEnt);
	Team* pTeam = team_GetTeam(pEnt);
	
	for (i = eaSize(&pNodeData->eaDestinations)-1; i >= 0; i--)
	{
		MapSummary* pMapData = pNodeData->eaDestinations[i];

		if (iPropIndex >= 0 && pMapData->iPropIndex != iPropIndex)
			continue;

		//TODO(MK): might want to check if the difficulty is close enough to the player's, somehow...
		pData->iNumEnabledOpenInstancing += pMapData->iNumEnabledOpenInstancing;

		if (pTeam)
		{
			for (j = eaSize(&pTeam->eaMembers)-1; j >= 0; j--)
			{
				TeamMember* pMember = pTeam->eaMembers[j];
					
				if (pMember->pcMapName == pMapData->pchMapName &&
					pMember->pcMapVars == pMapData->pchMapVars)
				{
					pData->iNumTeammates++;
					pData->bHasUsefulInfo = true;
				}
			}
		}

		if (guild_WithGuild(pEnt))
		{
			Guild* pGuild = guild_GetGuild(pEnt);
			if (pGuild)
			{
				for (j = eaSize(&pGuild->eaMembers)-1; j >= 0; j--)
				{
					GuildMember* pMember = pGuild->eaMembers[j];

					if (pMember->pcMapName == pMapData->pchMapName &&
						pMember->pcMapVars == pMapData->pchMapVars)
					{
						pData->iNumGuildmates++;
						pData->bHasUsefulInfo = true;
					}
				}
			}
		}

		if (pInfo && iPropIndex >= 0)
		{
			for (j = eaSize(&pInfo->missions)-1; j >= 0; j--)
			{
				Mission* pMission = pInfo->missions[j];
				if (mission_HasMissionOrSubMissionOnMap(pMission, pMapData->pchMapName, pMapData->pchMapVars, true))
				{
					pData->iNumMissions++;
					pData->bHasUsefulInfo = true;
				}
			}
		}

		if (iPropIndex >= 0)
		{
			//TODO(MK): This will not work for namespaced maps
			ZoneMapInfo* pZoneInfo = worldGetZoneMapByPublicName(pMapData->pchMapName);
			DisplayMessage* pDisplayMessage = zmapInfoGetDisplayNameMessage(pZoneInfo);

			if (pDisplayMessage)
			{
				pData->pchMapDisplayName = entTranslateDisplayMessage(pEnt, *pDisplayMessage);
			}
			pData->eMapType = zmapInfoGetMapType(pZoneInfo);
			break;
		}
	}

	if (pInfo && iPropIndex < 0)
	{
		for (j = eaSize(&pInfo->missions)-1; j >= 0; j--)
		{
			Mission* pMission = pInfo->missions[j];
			for (i = eaSize(&pNodeData->eaDestinations)-1; i >= 0; i--)
			{
				MapSummary* pMapData = pNodeData->eaDestinations[i];
				if (mission_HasMissionOrSubMissionOnMap(pMission, pMapData->pchMapName, pMapData->pchMapVars, true))
				{
					pData->iNumMissions++;
					pData->bHasUsefulInfo = true;
					break;
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenPlayerGetDestinationDataFromNode");
void entExprGenPlayerGetDestinationDataFromNode(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_NN_VALID WorldInteractionNode* pNode)
{
	Entity* pEnt = entActivePlayerPtr();
	const char* pchNodeKey = wlInteractionNodeGetKey(pNode);
	S32 n = pEnt ? eaIndexedFindUsingString(&pEnt->pPlayer->InteractStatus.ppDoorStatusNodes,pchNodeKey) : -1;
	DoorDestinationData* pData = ui_GenGetPointer(pGen, parse_DoorDestinationData, NULL);

	if (!pData)
	{
		pData = StructCreate(parse_DoorDestinationData);
	}
	else
	{
		StructReset(parse_DoorDestinationData, pData);
	}
	pData->pNode = pNode;

	if (pEnt && n >= 0)
	{
		NodeSummary* pNodeData = pEnt->pPlayer->InteractStatus.ppDoorStatusNodes[n];
		if (pNodeData)
		{
			gclFillDoorDestinationData(pData, pEnt, pNodeData, -1);
		}
	}
	ui_GenSetManagedPointer(pGen, pData, parse_DoorDestinationData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenPlayerGetDestinationDataFromInteractOption");
void entExprGenPlayerGetDestinationDataFromInteractOption(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID InteractOption* pOption)
{
	Entity* pEnt = entActivePlayerPtr();
	DoorDestinationData* pData = ui_GenGetPointer(pGen, parse_DoorDestinationData, NULL);
	WorldInteractionNode* pNode = pOption ? GET_REF(pOption->hNode) : NULL;

	if (!pData)
	{
		pData = StructCreate(parse_DoorDestinationData);
	}
	else
	{
		StructReset(parse_DoorDestinationData, pData);
	}
	pData->pNode = pNode;

	if (pEnt && pNode)
	{
		const char* pchNodeKey = wlInteractionNodeGetKey(pNode);
		S32 n = pEnt ? eaIndexedFindUsingString(&pEnt->pPlayer->InteractStatus.ppDoorStatusNodes,pchNodeKey) : -1;

		if (n >= 0)
		{
			NodeSummary* pNodeData = pEnt->pPlayer->InteractStatus.ppDoorStatusNodes[n];
			if (pNodeData)
			{
				gclFillDoorDestinationData(pData, pEnt, pNodeData, pOption->iIndex);
			}
		}
	}
	ui_GenSetManagedPointer(pGen, pData, parse_DoorDestinationData, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerHasVisitedMap);
bool entExprPlayerHasVisitedMap(const char* pchMapName)
{
	Entity* pEnt = entActivePlayerPtr();

	return ( pEnt && pEnt->pPlayer && eaIndexedFindUsingString(&pEnt->pPlayer->pVisitedMaps->eaMaps, pchMapName) >= 0 );
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerGetTargetEntity);
SA_RET_OP_VALID Entity* entExprPlayerGetTargetEntity( void )
{
	Entity* pEnt = entActivePlayerPtr();
	return entity_GetTarget(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerGetTargetEntityRef);
U32 entExprPlayerGetTargetEntityRef( void )
{
	Entity* pEnt = entActivePlayerPtr();
	return entity_GetTargetRef(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerGetAssistTargetEntity);
SA_RET_OP_VALID Entity* entExprPlayerGetAssistTargetEntity( void )
{
	Entity* pEnt = entActivePlayerPtr();
	return entity_GetAssistTarget(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerUsesProxAssistTarget);
bool entExprPlayerUsesProxAssistTarget( void )
{
	Entity* pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt && pPlayerEnt->pChar)
	{
		CharacterClass *pClass =  GET_REF(pPlayerEnt->pChar->hClass);

		return (pClass && !!pClass->bUseProximityTargetingAssistEnt);
	}

	return false;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetDuelRequesterName);
const char* entExprEntGetDuelRequesterName(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER3(pEnt, pChar, pvpDuelState, requester_name);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetTeamDuelRequesterName);
const char* entExprEntGetTeamDuelRequesterName(SA_PARAM_OP_VALID Entity *pEnt)
{
	return SAFE_MEMBER3(pEnt, pChar, pvpTeamDuelFlag, requester_name);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetTargetRef);
U32 entExprEntGetTargetRef(SA_PARAM_OP_VALID Entity *pEnt)
{
	return entity_GetTargetRef(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(PlayerGetTargetObjectRef);
const char* entExprPlayerGetTargetObjectRef( void )
{
	Entity* pEnt = entActivePlayerPtr();
	return pEnt && pEnt->pChar ? REF_STRING_FROM_HANDLE(pEnt->pChar->currentTargetHandle) : NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntityHasCreatorNode);
bool entExprEntityHasCreatorNode( SA_PARAM_OP_VALID Entity* pEnt )
{
	return pEnt ? GET_REF(pEnt->hCreatorNode) != NULL : false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetPlayerType);
int entExprGetPlayerType( SA_PARAM_OP_VALID Entity* pEnt )
{
	if (pEnt && pEnt->pPlayer)
	{
		return pEnt->pPlayer->playerType;
	}
	return kPlayerType_None;
}

AUTO_STRUCT;
typedef struct OfflineEntityData
{
	U32 uiPetID;			AST(KEY)
	S32	iEntType;
	char *pchClassType;
	U32 uiLastUpdateTime;
	Entity *pPet;
	REF_TO(Entity) hRequest;	AST(COPYDICT(ENTITYPLAYER))
}OfflineEntityData;

static OfflineEntityData **geaOfflineEntityData = NULL;
static OfflineEntityData **geaEntityCopyData = NULL;
static OfflineEntityData **geaEntityRequestQueue = NULL;
static U32 uiRequestElapsedTimerID = 0;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(RecieveOfflineCopy) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void savedpet_RecieveOfflineCopy(Entity *pClientEntity, U32 uiPetID, EntityStruct *pOfflineEntity)
{
	int i;
	OfflineEntityData *oed = NULL;

	if (!pClientEntity) return;
	if (!pOfflineEntity) return;
	if (!pOfflineEntity->pEntity) return;

	for (i = eaSize(&geaOfflineEntityData)-1; i >= 0; --i)
	{
		if (geaOfflineEntityData[i]->uiPetID == uiPetID)
		{
			oed = geaOfflineEntityData[i];
			break;
		}
	}

	if (!oed)
	{
		oed = StructCreate(parse_OfflineEntityData);
		oed->uiPetID = uiPetID;
		eaPush(&geaOfflineEntityData, oed);
	}

	oed->uiLastUpdateTime = timeSecondsSince2000();
	if (oed->pPet) StructDestroy(parse_Entity, oed->pPet);
	oed->pPet = StructCloneWithComment(parse_Entity, pOfflineEntity->pEntity, "Client side copy of offline ent in savedpet_ReceiveOfflineCopy");
	
	if (oed->pPet && oed->pPet->pChar && !oed->pPet->pChar->pEntParent)
	{
		oed->pPet->pChar->pEntParent = oed->pPet;
	}
}

Entity *savedpet_GetOfflineCopy(U32 uiPetID)
{
	int i;
	OfflineEntityData *oed = NULL;

	for (i = eaSize(&geaOfflineEntityData)-1; i >= 0; --i)
	{
		if (geaOfflineEntityData[i]->uiPetID == uiPetID)
		{
			U32 curTime = timeSecondsSince2000();
			if (geaOfflineEntityData[i]->uiLastUpdateTime + 3 <= curTime)
			{
				geaOfflineEntityData[i]->uiLastUpdateTime = curTime;
				ServerCmd_RequestOfflineCopy(uiPetID);
			}
			return geaOfflineEntityData[i]->pPet;
		}
	}

	oed = StructCreate(parse_OfflineEntityData);
	oed->uiPetID = uiPetID;
	oed->uiLastUpdateTime = timeSecondsSince2000();
	eaPush(&geaOfflineEntityData, oed);

	ServerCmd_RequestOfflineCopy(uiPetID);
	return NULL;
}

Entity *savedpet_GetOfflineOrNotCopy(U32 uiPetID)
{
	OfflineEntityData *oed = NULL;
	Entity *result = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET, uiPetID);

	if (result) return result;
	return savedpet_GetOfflineCopy(uiPetID);
}


static void entcopy_PruneCopyList(void)
{
	if (eaSize(&geaEntityCopyData) > 500)
	{
		int i;
		U32 lowest = eaSize(&geaEntityCopyData)-2;
		U32 lowestvalue = geaEntityCopyData[lowest]->uiLastUpdateTime;
		for (i = eaSize(&geaEntityCopyData)-3; i >= 0; --i)
		{
			if (geaEntityCopyData[i]->uiLastUpdateTime < lowestvalue)
			{
				lowestvalue = geaEntityCopyData[i]->uiLastUpdateTime;
				lowest = i;
			}
		}
		StructDestroy(parse_OfflineEntityData, geaEntityCopyData[lowest]);
		eaRemove(&geaEntityCopyData,lowest);
	}
}

static bool oed_IsEntity(OfflineEntityData *pOED, ContainerID iEntID, GlobalType iEntType, const char *pchClassType)
{
	return pOED
		&& pOED->uiPetID == iEntID
		&& pOED->iEntType == iEntType
		&& (
			(pOED->pchClassType == NULL && pchClassType == NULL)
			|| stricmp_safe(pOED->pchClassType, pchClassType) == 0
		);
}

static S32 oed_FindEntity(OfflineEntityData ***peaOED, ContainerID iEntID, GlobalType iEntType, const char *pchClassType)
{
	S32 i, n = eaSize(peaOED);
	for (i = 0; i < n; i++)
		if (oed_IsEntity((*peaOED)[i], iEntID, iEntType, pchClassType))
			return i;
	return -1;
}

static OfflineEntityData *oed_GetEntity(OfflineEntityData ***peaOED, ContainerID iEntID, GlobalType iEntType, const char *pchClassType)
{
	S32 i = oed_FindEntity(peaOED, iEntID, iEntType, pchClassType);
	if (i < 0)
		return NULL;
	return (*peaOED)[i];
}

static OfflineEntityData *oed_RemoveEntity(OfflineEntityData ***peaOED, ContainerID iEntID, GlobalType iEntType, const char *pchClassType)
{
	S32 i = oed_FindEntity(peaOED, iEntID, iEntType, pchClassType);
	if (i < 0)
		return NULL;
	return eaRemove(peaOED, i);
}

static bool s_bRequestEntCopyFromQueueDebug = false;
AUTO_CMD_INT(s_bRequestEntCopyFromQueueDebug, EntCopyDebug) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug) ACMD_CMDLINEORPUBLIC;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(RecieveEntCopyWithType) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void entity_RecieveEntCopyWithType(Entity *pClientEntity, int iEntID, int iEntType, char *pchClassType, EntityStruct *pEntityCopy)
{
	OfflineEntityData *oed = NULL;

	if (!pClientEntity || !pEntityCopy || !pEntityCopy->pEntity)
		return;

	oed = oed_GetEntity(&geaEntityCopyData, iEntID, iEntType, pchClassType);
	if (!oed)
	{
		entcopy_PruneCopyList();

		oed = StructCreate(parse_OfflineEntityData);
		oed->uiPetID = iEntID;
		oed->iEntType = iEntType;
		if (pchClassType)
			oed->pchClassType = StructAllocString(pchClassType);
		eaPush(&geaEntityCopyData, oed);
	}

	oed->uiLastUpdateTime = timeSecondsSince2000();
	if (oed->pPet)
		StructDestroy(parse_Entity, oed->pPet);

	oed->pPet = StructCloneWithComment(parse_Entity, pEntityCopy->pEntity, "Client side offline copy ent in entity_ReceiveEntCopyWithType");

	if (oed->pPet->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(oed->pPet);

		if (!oed->pPet->pChar->pEntParent)
			oed->pPet->pChar->pEntParent = oed->pPet;

		// Reset the powers array since the ppPowers is not sent over network
		character_ResetPowersArray(entGetPartitionIdx(oed->pPet), oed->pPet->pChar, pExtract);
	}

	// Remove from request queue
	oed = oed_RemoveEntity(&geaEntityRequestQueue, iEntID, iEntType, pchClassType);
	if (oed)
	{
		if (s_bRequestEntCopyFromQueueDebug)
			printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Received %s %d in %ums\n", StaticDefineIntRevLookup(GlobalTypeEnum, iEntType), iEntID, gGCLState.totalElapsedTimeMs - oed->uiLastUpdateTime);
		oed->pPet = NULL;
		StructDestroy(parse_OfflineEntityData, oed);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_NAME(RecieveEntCopy) ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void entity_RecieveEntCopy(Entity *pClientEntity, int iEntID, int iEntType, EntityStruct *pEntityCopy)
{
	entity_RecieveEntCopyWithType(pClientEntity, iEntID, iEntType, NULL, pEntityCopy);
}

static void entity_RequestEntCopyFromQueue(void)
{
	const U32 c_uCopyTimeout = 60000; // fail after 60 seconds
	const U32 c_uWaitTimeout = 20000; // fail after 20 seconds

	// Special entity pointers used as markers
	static Entity s_Timeout = {1};
	static Entity s_Requesting[10] = {0}; // try once every ~6 seconds
	static Entity s_Waiting[10] = {0}; // try once every ~2 seconds

	OfflineEntityData *pHead = eaHead(&geaEntityRequestQueue);
	Entity *pReference;
	U32 uElapsed, uTry;
	int iSize = eaSize(&geaEntityRequestQueue);
	bool bWaiting;

	if (!pHead)
		return;

	if (iSize > 1)
	{
		int i;
		for (i = 0; i < iSize; i++)
		{
			if (pHead->pPet != &s_Timeout)
			{
				break;
			}

			eaMove(&geaEntityRequestQueue, eaSize(&geaEntityRequestQueue) - 1, 0);
			pHead = eaHead(&geaEntityRequestQueue);
		}
	}

	if (pHead->uiLastUpdateTime == 0)
	{
		// Start requesting
		pHead->uiLastUpdateTime = gGCLState.totalElapsedTimeMs;
	}

	// Start with elapsed time
	uElapsed = gGCLState.totalElapsedTimeMs - pHead->uiLastUpdateTime;
	bWaiting = &s_Waiting[0] <= pHead->pPet && pHead->pPet <= &s_Waiting[ARRAY_SIZE(s_Waiting) - 1];

	if (!IS_HANDLE_ACTIVE(pHead->hRequest) || GET_REF(pHead->hRequest))
	{
		if (bWaiting)
		{
			// No longer waiting for entity reference, reset timer
			uElapsed = 0;
			pHead->uiLastUpdateTime = gGCLState.totalElapsedTimeMs;
			pHead->pPet = NULL;
		}

		uTry = uElapsed / (c_uCopyTimeout / ARRAY_SIZE(s_Requesting));
		pReference = uTry < ARRAY_SIZE(s_Requesting) ? &s_Requesting[uTry] : NULL;
	}
	else
	{
		uTry = uElapsed / (c_uWaitTimeout / ARRAY_SIZE(s_Waiting));
		pReference = uTry < ARRAY_SIZE(s_Waiting) ? &s_Waiting[uTry] : NULL;
	}

	if (!pHead->pPet && pReference || pReference && pHead->pPet != pReference)
	{
		if (!IS_HANDLE_ACTIVE(pHead->hRequest) || GET_REF(pHead->hRequest))
		{
			// Entity is ready for request
			if (s_bRequestEntCopyFromQueueDebug)
			{
				if (pHead->pPet)
					printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Requesting %s %d at %ums (attempt %d)\n", StaticDefineIntRevLookup(GlobalTypeEnum, pHead->iEntType), pHead->uiPetID, uElapsed, uTry);
				else
					printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Requesting %s %d at %ums\n", StaticDefineIntRevLookup(GlobalTypeEnum, pHead->iEntType), pHead->uiPetID, uElapsed);
			}

			// Make a new request for the entity
			if (pHead->pchClassType)
				ServerCmd_RequestEntCopyByType(pHead->uiPetID, pHead->iEntType, pHead->pchClassType);
			else
				ServerCmd_RequestEntCopy(pHead->uiPetID, pHead->iEntType);
		}
		else
		{
			// Still waiting for reference
			if (s_bRequestEntCopyFromQueueDebug)
			{
				if (pHead->pPet)
					printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Waiting on %s %d at %ums (attempt %d)\n", StaticDefineIntRevLookup(GlobalTypeEnum, pHead->iEntType), pHead->uiPetID, uElapsed, uTry);
				else
					printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Waiting on %s %d at %ums\n", StaticDefineIntRevLookup(GlobalTypeEnum, pHead->iEntType), pHead->uiPetID, uElapsed);
			}

			// Move to end of queue
			eaMove(&geaEntityRequestQueue, eaSize(&geaEntityRequestQueue) - 1, 0);
		}

		pHead->pPet = pReference;
	}
	else if (!pReference)
	{
		// Request timed out
		if (pHead->pPet != &s_Timeout)
		{
			pHead->pPet = &s_Timeout;

			if (s_bRequestEntCopyFromQueueDebug)
			{
				if (bWaiting)
					printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Wait timeout of %s %d in %ums\n", StaticDefineIntRevLookup(GlobalTypeEnum, pHead->iEntType), pHead->uiPetID, uElapsed);
				else
					printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Request timeout of %s %d in %ums\n", StaticDefineIntRevLookup(GlobalTypeEnum, pHead->iEntType), pHead->uiPetID, uElapsed);
			}
		}
	}
}

static void entity_RequestEntCopy(int iEntID, int iEntType, const char *pchClassType)
{
	OfflineEntityData *oed = oed_GetEntity(&geaEntityRequestQueue, iEntID, iEntType, pchClassType);

	if (!oed)
	{
		if (s_bRequestEntCopyFromQueueDebug)
			printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Starting request of %s %d\n", StaticDefineIntRevLookup(GlobalTypeEnum, iEntType), iEntID);

		oed = StructCreate(parse_OfflineEntityData);
		oed->uiPetID = iEntID;
		oed->iEntType = iEntType;
		oed->pchClassType = StructAllocString(EMPTY_TO_NULL(pchClassType));
		eaPush(&geaEntityRequestQueue, oed);

		if (iEntType == GLOBALTYPE_ENTITYPLAYER)
		{
			char idBuf[128];
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(iEntType), ContainerIDToString(iEntID, idBuf), oed->hRequest);
			if (!GET_REF(oed->hRequest))
				ServerCmd_GetReadyToRequestEntCopy(iEntID, iEntType);
		}
	}

	// Reset status if necessary
	if (oed->pPet && oed->pPet->myRef)
	{
		if (s_bRequestEntCopyFromQueueDebug)
			printfColor(COLOR_BLUE | COLOR_BRIGHT, "[EntCopy] Restarting request of %s %d\n", StaticDefineIntRevLookup(GlobalTypeEnum, iEntType), iEntID);

		oed->uiLastUpdateTime = 0;
		oed->pPet = NULL;
	}

	entity_RequestEntCopyFromQueue();
}

SA_RET_OP_VALID Entity *entity_GetEntFromIDCopyFromServerByTypeEx(int iEntID, int iEntType, const char *pchClassType, bool bUpdate)
{
	const U32 c_uRefreshInterval = 600; // cache for 10 minutes
	U32 uNow = timeSecondsSince2000();
	OfflineEntityData *oed;

	if (iEntID == 0)
		return NULL;
	if (iEntType != GLOBALTYPE_ENTITYPLAYER && iEntType != GLOBALTYPE_ENTITYSAVEDPET)
		return entFromContainerIDAnyPartition(iEntType, iEntID);

	oed = oed_GetEntity(&geaEntityCopyData, iEntID, iEntType, pchClassType);
	if (!oed)
	{
		entcopy_PruneCopyList();

		oed = StructCreate(parse_OfflineEntityData);
		oed->uiPetID = iEntID;
		oed->iEntType = iEntType;
		oed->pchClassType = StructAllocString(EMPTY_TO_NULL(pchClassType));
		eaPush(&geaEntityCopyData, oed);
	}

	if (oed->uiLastUpdateTime + c_uRefreshInterval < uNow || bUpdate)
	{
		oed->uiLastUpdateTime = uNow;
		entity_RequestEntCopy(iEntID, iEntType, pchClassType);
	}
	else
	{
		entity_RequestEntCopyFromQueue();
	}

	return oed->pPet;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntFromIDCopyFromServerByType");
SA_RET_OP_VALID Entity *entity_GetEntFromIDCopyFromServerByType(int iEntID, int iEntType, const char *pchClassType)
{
	return entity_GetEntFromIDCopyFromServerByTypeEx(iEntID, iEntType, pchClassType, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntFromIDCopyByType");
SA_RET_OP_VALID Entity *entity_GetEntFromIDCopyByType(int iEntID, int iEntType, const char *pchClassType)
{
	return entity_GetEntFromIDCopyFromServerByTypeEx(iEntID, iEntType, pchClassType, false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntFromIDCopyFromServer");
SA_RET_OP_VALID Entity *entity_GetEntFromIDCopyFromServer(int iEntID, int iEntType)
{
	return entity_GetEntFromIDCopyFromServerByType(iEntID, iEntType, NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntFromIDCopy");
SA_RET_OP_VALID Entity *entity_GetEntFromIDCopy(int iEntID, int iEntType)
{
	return entity_GetEntFromIDCopyByType(iEntID, iEntType, NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntCopyFromServerByType");
SA_RET_OP_VALID Entity *entity_GetEntCopyFromServerByType(U32 uiRef, const char *pchClassType)
{
	Entity *e = entFromEntityRefAnyPartition(uiRef);
	if (e)
	{
		return entity_GetEntFromIDCopyFromServerByType(entGetContainerID(e), entGetType(e), pchClassType);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntCopyByType");
SA_RET_OP_VALID Entity *entity_GetEntCopyByType(U32 uiRef, const char *pchClassType)
{
	Entity *e = entFromEntityRefAnyPartition(uiRef);
	if (e)
	{
		return entity_GetEntFromIDCopyByType(entGetContainerID(e), entGetType(e), pchClassType);
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntCopyFromServer");
SA_RET_OP_VALID Entity *entity_GetEntCopyFromServer(U32 uiRef)
{
	return entity_GetEntCopyFromServerByType(uiRef, NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetEntCopy");
SA_RET_OP_VALID Entity *entity_GetEntCopy(U32 uiRef)
{
	return entity_GetEntCopyByType(uiRef, NULL);
}

static Entity* gclEntGetPuppetEntityOrCopyByType(SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType)
{
	PuppetEntity* pPuppet = pEntity ? entity_GetPuppetByType(pEntity, pchClassType, NULL, true) : NULL;
	Entity* pPuppetEntity = pPuppet ? GET_REF(pPuppet->hEntityRef) : NULL;

	if (pEntity && !pPuppetEntity)
	{
		ContainerID uEntID = entGetContainerID(pEntity);
		GlobalType eEntType = entGetType(pEntity);
		pPuppetEntity = entity_GetEntFromIDCopyByType(uEntID, eEntType, pchClassType);
	}
	return pPuppetEntity;
}

// Returns the class of the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClass);
const char* exprEntGetClass(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if (pClass && pClass->pchName)
			return pClass->pchName;
		else
			return REF_STRING_FROM_HANDLE(pEntity->pChar->hClass);
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassCategorySet);
SA_RET_OP_VALID CharClassCategorySet *exprEntGetClassCategorySet(SA_PARAM_OP_VALID Entity *pEntity)
{
	CharacterClass *pClass = SAFE_GET_REF2(pEntity, pChar, hClass);
	return CharClassCategorySet_getCategorySetFromEntity(pEntity, SAFE_MEMBER(pClass, eType));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ClassCategorySet);
SA_RET_OP_VALID CharClassCategorySet *exprClassCategorySet(const char* pchSet)
{
	return RefSystem_ReferentFromString(g_hCharacterClassCategorySetDict, pchSet);
}

// Returns whether or not the player has a puppet in the given set
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntHasActivePuppetOfSet);
bool exprEntHasActivePuppetOfSet(SA_PARAM_OP_VALID Entity *pEntity, const char* pchSet)
{
	CharClassCategorySet *pSet = RefSystem_ReferentFromString(g_hCharacterClassCategorySetDict, pchSet);
	if (pSet)
	{
		PuppetMaster *pPuppetMaster = SAFE_MEMBER2(pEntity, pSaved, pPuppetMaster);
		int i, iSize = pPuppetMaster ? eaSize(&pPuppetMaster->ppPuppets) : 0;
		for (i = 0; i < iSize; i++)
		{
			PuppetEntity *pPuppetEntity = pPuppetMaster->ppPuppets[i];
			if (pPuppetEntity->eState == PUPPETSTATE_ACTIVE)
			{
				Entity* pPuppet = GET_REF(pPuppetEntity->hEntityRef);
				CharacterClass *pClass = SAFE_GET_REF2(pPuppet, pChar, hClass);
				if (pClass && eaiFind(&pSet->eaCategories, pClass->eCategory) >= 0)
				{
					return true;
				}
			}
		}
	}
	return false;
}

// Returns the CharacterClass struct of the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassStruct);
SA_RET_OP_VALID CharacterClass* exprEntGetClassStruct(SA_PARAM_OP_VALID Entity *pEntity)
{
	return SAFE_GET_REF2(pEntity, pChar, hClass);
}

// Returns the class of the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassEnum);
S32 exprEntGetClassEnum(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if (pClass)
			return pClass->eType;
	}
	return 0;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassDisplayName);
const char* exprEntGetClassDispName(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if (pClass)
		{
			return TranslateDisplayMessage(pClass->msgDisplayName);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassCategory);
int exprEntGetClassCategory(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if (pClass)
		{
			return pClass->eCategory;
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassCategoryName);
const char* exprEntGetClassCategoryName(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (pEntity && pEntity->pChar)
	{
		CharacterClass *pClass = GET_REF(pEntity->pChar->hClass);
		if (pClass)
		{
			Message *pMessage = StaticDefineGetMessage(CharClassCategoryEnum, pClass->eCategory); 
			return TranslateMessagePtr(pMessage);;
		}
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetClassDisplayNameByType);
const char* exprEntGetClassDispNameByType(SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType)
{
	if (pEntity)
	{
		Entity* pPuppetEntity = gclEntGetPuppetEntityOrCopyByType(pEntity, pchClassType);
		if (pPuppetEntity)
		{
			return exprEntGetClassDispName(pPuppetEntity);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetNameByType);
const char* exprEntGetNameByType(SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType)
{
	if (pEntity)
	{
		Entity* pPuppetEntity = gclEntGetPuppetEntityOrCopyByType(pEntity, pchClassType);
		if (pPuppetEntity && pPuppetEntity->pSaved)
		{
			return pPuppetEntity->pSaved->savedName;
		}
	}
	return "";
}

// Get the logical name of the species
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesName);
const char* exprEntGetSpeciesName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	SpeciesDef *pSpecies = entExprGetSpecies(pContext, pEntity);
	const char *pchResult = NULL;
	if (pSpecies)
	{
		pchResult = pSpecies->pcName;
	}
	return NULL_TO_EMPTY(pchResult);
}

// DEPRECATED: Use EntGetSpeciesName instead
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenGetSpeciesName);
const char* exprGenGetSpeciesName(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity)
{
	return exprEntGetSpeciesName(pContext, pEntity);
}

// Get the actual SpeciesName field of the species
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesNameActual);
const char* exprEntGetSpeciesNameActual(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	SpeciesDef *pSpecies = entExprGetSpecies(pContext, pEntity);
	const char *pchResult = NULL;
	if (pSpecies)
	{
		pchResult = pSpecies->pcSpeciesName;
	}
	return NULL_TO_EMPTY(pchResult);
}

// DEPRECATED: Use EntGetSpeciesDisplayName instead
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenGetSpeciesDisplayName);
const char* exprEntGetSpeciesDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Get the species display name from the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesDisplayName);
const char* exprEntGetSpeciesDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	SpeciesDef *pSpecies = entExprGetSpecies(pContext, pEntity);
	const char *pchResult = NULL;
	if (pSpecies)
	{
		pchResult = TranslateDisplayMessage(pSpecies->displayNameMsg);
	}
	return NULL_TO_EMPTY(pchResult);
}

// Get the species description from the entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesDescription);
const char *gclExprEntGetSpeciesDescription(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	SpeciesDef *pSpecies = entExprGetSpecies(pContext, pEntity);
	const char *pchResult = NULL;
	if (pSpecies)
	{
		pchResult = TranslateDisplayMessage(pSpecies->descriptionMsg);
	}
	return NULL_TO_EMPTY(pchResult);
}

// DEPRECATED: Use EntGetSpeciesGenderDisplayName instead
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenGetSpeciesGenderDisplayName);
const char* exprEntGetSpeciesGenderDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity);

// Get the species gender name from the species
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesGenderDisplayName);
const char* exprEntGetSpeciesGenderDisplayName(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	SpeciesDef *pSpecies = entExprGetSpecies(pContext, pEntity);
	const char *pchResult = NULL;
	if (pSpecies)
	{
		pchResult = TranslateDisplayMessage(pSpecies->genderNameMsg);
	}
	return NULL_TO_EMPTY(pchResult);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenGetSpeciesDisplayNameByType);
const char* exprEntGetSpeciesDisplayNameByType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType);

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesDisplayNameByType);
const char* exprEntGetSpeciesDisplayNameByType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType)
{
	if (pEntity)
	{
		Entity* pPuppetEntity = gclEntGetPuppetEntityOrCopyByType(pEntity, pchClassType);
		if(pPuppetEntity) {
			return exprEntGetSpeciesDisplayName(pContext, pPuppetEntity);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenGetSpeciesGenderDisplayNameByType);
const char* exprEntGetSpeciesGenderDisplayNameByType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType);

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetSpeciesGenderDisplayNameByType);
const char* exprEntGetSpeciesGenderDisplayNameByType(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char *pchClassType)
{
	if (pEntity)
	{
		Entity* pPuppetEntity = gclEntGetPuppetEntityOrCopyByType(pEntity, pchClassType);
		if(pPuppetEntity) {
			return exprEntGetSpeciesGenderDisplayName(pContext, pPuppetEntity);
		}
	}
	return "";
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetItemQuality");
S32 exprEntGetItemQuality(SA_PARAM_OP_VALID Entity *pEnt)
{
	return pEnt ? inv_ent_getEntityItemQuality(pEnt) : kItemQuality_None;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetSubEntityItemQuality");
S32 exprEntGetSubEntityItemQuality(SA_PARAM_OP_VALID Entity *pEnt, U32 iType, U32 iContainerID)
{
	pEnt = pEnt ? entity_GetSubEntity(PARTITION_CLIENT, pEnt, iType, iContainerID) : NULL;
	return pEnt ? inv_ent_getEntityItemQuality(pEnt) : kItemQuality_None;
}

F32 gclEntity_GetInteractRange(Entity *entPlayer, Entity *entCritter, U32 uNodeInteractDist)
{
	U32 iDist = 0;

	if (entCritter) {
		if(entCritter->pCritter)
			iDist = entCritter->pCritter->uInteractDist;
	} else {
		iDist = uNodeInteractDist;
	} 

	if(iDist)
		return iDist;

	iDist = entity_GetCurrentRegionInteractDist(entPlayer);
	if(iDist)
		return iDist;

	if (entCritter) {
		return INTERACT_RANGE;
	} else {
		return DEFAULT_NODE_INTERACT_DIST;
	}
}


// Return true if the entity is a foe of the player.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsInSameAITeamWith");
bool entExprIsInSameAITeamWith(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID Entity *pOtherEntity)
{
	if (pEntity == NULL || pEntity->iAICombatTeamID == 0 || pOtherEntity == NULL || pOtherEntity->iAICombatTeamID == 0)
	{
		return false;
	}
	return pEntity->iAICombatTeamID == pOtherEntity->iAICombatTeamID;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("PlayerIsInAimMode");
bool entExprPlayerIsInAimMode(void)
{
	Entity* pEnt = entActivePlayerPtr();
	return pEnt && entIsAiming(pEnt);
}

// Do not use this. Use EntGetGenderValue instead.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenPlayerGenderEnum);
int gclExprEntGetGenderValue(SA_PARAM_OP_VALID Entity* pEnt);

// Get the gender as an integer. Do not use this, use EntGetGender. The value returned is meaningless.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntGetGenderValue);
int gclExprEntGetGenderValue(SA_PARAM_OP_VALID Entity *pEnt)
{
	Gender eGender = SAFE_MEMBER(pEnt, eGender);
	if (eGender == Gender_Unknown && SAFE_MEMBER(pEnt, pChar))
	{
		SpeciesDef *pSpecies = GET_REF(pEnt->pChar->hSpecies);
		if (pSpecies)
			eGender = pSpecies->eGender;
	}
	return eGender;
}

// Do not use this. Use SpeciesFindForGender instead.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSpeciesNameFromGender);
const char *gclExprSpeciesFindForGender(const char *pchSpeciesName, int iGender);

// Find a name of a SpeciesDef that is for a specific gender,
// using the common species name or another SpeciesDef name as the
// reference group.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(SpeciesFindForGender);
const char *gclExprSpeciesFindForGender(const char *pchSpeciesName, int iGender)
{
	S32 i;
	DictionaryEArrayStruct* pArray = resDictGetEArrayStruct("SpeciesDef");
	const char *pchName = pchSpeciesName;
	SpeciesDef *pSpecies;

	for (i = 0; i < eaSize(&pArray->ppReferents); i++)
	{
		pSpecies = eaGet(&pArray->ppReferents, i);
		if (pSpecies && !stricmp(pSpecies->pcName, pchSpeciesName))
			pchName = pSpecies->pcSpeciesName;
	}

	for (i = 0; i < eaSize(&pArray->ppReferents); i++)
	{
		pSpecies = eaGet(&pArray->ppReferents, i);
		if (pSpecies && !stricmp(pSpecies->pcSpeciesName, pchName) && pSpecies->eGender == iGender)
			return pSpecies->pcName;
	}

	return "";
}

static bool gclIsEnemyToLocalPlayer(const char *pchFaction)
{
	CritterFaction *worldFXFaction = critter_FactionGetByName(pchFaction);
	Entity *eActivePlayer = entActivePlayerPtr();
	if (worldFXFaction && eActivePlayer)
	{
		CritterFaction *entFaction = entGetFaction(eActivePlayer);
		EntityRelation relation;
		ANALYSIS_ASSUME(worldFXFaction != NULL);
		relation = faction_GetRelation(worldFXFaction, entFaction);
		return (kEntityRelation_Foe == relation);
	}

	return false;
}

// Creates a WLCostume from a PlayerCostume and adds it to the costume dictionary
WLCostume* gclEntity_CreateWLCostumeFromPlayerCostume(Entity* pEnt, PlayerCostume* pCostume, bool bForceUpdate)
{
	WLCostume* pWLCostume;
	const char* pchCostumeName = NULL;
	char* pchPrefix = NULL;
	GlobalType eEntType = pEnt ? entGetType(pEnt) : 0;
	ContainerID uEntID = pEnt ? entGetContainerID(pEnt) : 0;

	if (pCostume)
	{
		// Prefix for headshot costumes
		pchPrefix = "Costume.";
		// Get the full headshot costume name
		pchCostumeName = costumeGenerate_CreateWLCostumeName(pCostume,pchPrefix,eEntType,uEntID,0);
		// Check to see if the costume is already in the dictionary
		pWLCostume = RefSystem_ReferentFromString("Costume", pchCostumeName);
	}
	else
	{
		return NULL;
	}

	if (!pWLCostume || bForceUpdate) {
		WLCostume** eaSubCostumes = NULL;
		SpeciesDef* pSpecies = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hSpecies) : NULL;
		CharacterClass* pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
		PCSlotType* pSlotType = costumeEntity_GetActiveSavedSlotType(pEnt);

		// Create the world layer costume
		pWLCostume = costumeGenerate_CreateWLCostume(pCostume,
													 pSpecies,
													 pClass,
													 NULL,
													 pSlotType,
													 NULL, 
													 NULL, 
													 pchPrefix, 
													 eEntType,
													 uEntID, 
													 false, 
													 &eaSubCostumes);
		if (pWLCostume)
		{
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
				wlCostumePushSubCostume(pSubCostume, pWLCostume);
			FOR_EACH_END;

			// Put costume into the dictionary once it is complete, else destroy and try again next time
			if (pWLCostume->bComplete) {
				pWLCostume->pcName = allocAddString(pchCostumeName);
				wlCostumeAddToDictionary(pWLCostume, pchCostumeName);
			} else {
				StructDestroy(parse_WLCostume, pWLCostume);
				pWLCostume = NULL;
			}
		}
	}
	return pWLCostume;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(MacroExec, MacroRun);
void gclPlayerExecMacro(U32 uMacroID)
{
	Entity* pEnt = entActivePlayerPtr();
	S32 iMacroIdx = entity_FindMacroByID(pEnt, uMacroID);
	if (iMacroIdx >= 0)
	{
		globCmdParse(pEnt->pPlayer->pUI->eaMacros[iMacroIdx]->pchMacro);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPlayerMacroList);
void gclGenExprGetPlayerMacroList(SA_PARAM_NN_VALID UIGen* pGen)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pUI)
	{
		ui_GenSetList(pGen, &pEnt->pPlayer->pUI->eaMacros, parse_PlayerMacro);
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
}

static void gclCreateMacroEditCommandsFromString(MacroEditData* pMacroData, const char* pchMacroString)
{
	MacroCommand* pCommand;
	const char* pchCurr;
	const char* pchLast = pchMacroString;
	
	while (pchCurr = strstr(pchLast, "$$"))
	{
		pCommand = StructCreate(parse_MacroCommand);
		estrConcat(&pCommand->estrCommand, pchLast, pchCurr-pchLast);
		estrTrimLeadingAndTrailingWhitespace(&pCommand->estrCommand);
		eaPush(&pMacroData->eaCommands, pCommand);
		pchLast = pchCurr + 2;
	}
	pCommand = StructCreate(parse_MacroCommand);
	estrCopy2(&pCommand->estrCommand, pchLast);
	estrTrimLeadingAndTrailingWhitespace(&pCommand->estrCommand);
	eaPush(&pMacroData->eaCommands, pCommand);
}

// If uMacroID is valid, fill it with an existing PlayerMacro,
// otherwise just create an empty MacroEditData structure
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditCreateData);
void gclExprMacroEditCreateData(U32 uMacroID)
{
	Entity* pEnt = entActivePlayerPtr();
	S32 iMacroIdx = entity_FindMacroByID(pEnt, uMacroID);
	
	if (s_pMacroEditData)
	{
		StructReset(parse_MacroEditData, s_pMacroEditData);
	}
	else
	{
		s_pMacroEditData = StructCreate(parse_MacroEditData);
	}
	if (iMacroIdx >= 0)
	{
		PlayerMacro* pMacro = pEnt->pPlayer->pUI->eaMacros[iMacroIdx];
		s_pMacroEditData->uMacroID = pMacro->uMacroID;
		s_pMacroEditData->pchDescription = StructAllocString(pMacro->pchDescription);
		s_pMacroEditData->pchIcon = allocAddString(pMacro->pchIcon);
		gclCreateMacroEditCommandsFromString(s_pMacroEditData, pMacro->pchMacro);
	}
} 

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditSetDescription);
bool gclExprMacroEditSetDescription(const char* pchDescription)
{
	int iDescLen = pchDescription ? (int)strlen(pchDescription) : 0;
	if (s_pMacroEditData && iDescLen <= PLAYER_MACRO_DESC_LENGTH_MAX)
	{
		StructCopyString(&s_pMacroEditData->pchDescription, pchDescription);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditSetIcon);
bool gclExprMacroEditSetIcon(const char* pchIcon)
{
	pchIcon = gclGetBestIconName(pchIcon, NULL);
	if (s_pMacroEditData && texFind(pchIcon, false))
	{
		s_pMacroEditData->pchIcon = allocAddString(pchIcon);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditSaveData);
bool gclExprMacroEditSaveData(void)
{
	bool bSuccess = false;
	if (s_pMacroEditData && eaSize(&s_pMacroEditData->eaCommands))
	{
		S32 i, iLen;
		char* estrMacro = NULL;
		estrStackCreate(&estrMacro);

		// Create the macro string
		for (i = 0; i < eaSize(&s_pMacroEditData->eaCommands); i++)
		{
			MacroCommand* pMacroCommand = s_pMacroEditData->eaCommands[i];

			if (i != 0)
				estrAppend2(&estrMacro, " $$ ");

			estrAppend(&estrMacro, &pMacroCommand->estrCommand);
		}
		iLen = estrLength(&estrMacro);
		
		// Validate that the macro string isn't too long
		if (iLen && iLen <= PLAYER_MACRO_LENGTH_MAX)
		{
			ServerCmd_Macro(s_pMacroEditData->uMacroID, 
							estrMacro, 
							s_pMacroEditData->pchDescription, 
							s_pMacroEditData->pchIcon);
			bSuccess = true;
		}
		estrDestroy(&estrMacro);
	}
	return bSuccess;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetMacroEditCommandList);
void gclExprGenGetMacroEditCommandList(SA_PARAM_NN_VALID UIGen* pGen)
{
	if (s_pMacroEditData)
	{
		ui_GenSetList(pGen, &s_pMacroEditData->eaCommands, parse_MacroCommand);
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
}

// If iCmdIdx < 0, then create a new command, otherwise modify an existing one
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditModifyCommand);
void gclExprMacroEditModifyCommand(const char* pchCommand, S32 iCmdIdx)
{
	if (s_pMacroEditData && pchCommand && pchCommand[0])
	{
		if (iCmdIdx < 0)
		{
			MacroCommand* pMacroCommand = StructCreate(parse_MacroCommand);
			estrCopy2(&pMacroCommand->estrCommand, pchCommand);
			estrTrimLeadingAndTrailingWhitespace(&pMacroCommand->estrCommand);
			eaPush(&s_pMacroEditData->eaCommands, pMacroCommand);
		}
		else
		{
			MacroCommand* pMacroCommand = eaGet(&s_pMacroEditData->eaCommands, iCmdIdx);
			if (pMacroCommand)
			{
				estrCopy2(&pMacroCommand->estrCommand, pchCommand);
				estrTrimLeadingAndTrailingWhitespace(&pMacroCommand->estrCommand);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditRemoveCommand);
void gclExprMacroEditRemoveCommand(S32 iCmdIdx)
{
	if (s_pMacroEditData && eaGet(&s_pMacroEditData->eaCommands, iCmdIdx))
	{
		StructDestroy(parse_MacroCommand, eaRemove(&s_pMacroEditData->eaCommands, iCmdIdx));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(MacroEditClearData);
void gclExprMacroEditClearData(void)
{
	StructDestroySafe(parse_MacroEditData, &s_pMacroEditData);
}


static void notifySendTacticalDisabled(Entity *pEnt, const char *messageKey)
{
	char *pchTemp = NULL;
	estrStackCreate(&pchTemp);
	FormatMessageKey(&pchTemp,messageKey,STRFMT_END);
	notify_NotifySend(pEnt, kNotifyType_TacticalAimDisabled, pchTemp, NULL, NULL);
	estrDestroy(&pchTemp);
}

void gclEntity_NotifyIfAimUnavailable(Entity *pEnt)
{
	if (pEnt && pEnt->pChar)
	{
		if (!pEnt->mm.mrTactical)
		{
			if (!mmIsAttachedToClient(pEnt->mm.movement))
			{
				notifySendTacticalDisabled(pEnt,"TacticalNotify.EntityDetached");
				return;
			}
			return;		
		}
		else
		{
			if (mrTacticalHasDisableID(pEnt->mm.mrTactical, TACTICAL_REQUIREMENTS_UID))
			{	// right now assuming the requirements are due to aiming, since it's the only thing hooked up to it for now
				notifySendTacticalDisabled(pEnt,"TacticalNotify.AimRequirements");
				return;
			}
		}
		

		// if aim disabled due to character disable
		if (g_CombatConfig.tactical.bAimDisableDuringPowerDisableAttrib && pEnt->pChar->pattrBasic->fDisable > 0)
		{
			notifySendTacticalDisabled(pEnt,"TacticalNotify.CharacterDisabled");
			return;
		}
		
		if(g_CombatConfig.tactical.aim.eAimCostAttrib && IS_NORMAL_ATTRIB(g_CombatConfig.tactical.aim.eAimCostAttrib))
		{
			F32 fAimCostAttrib = *F32PTR_OF_ATTRIB(pEnt->pChar->pattrBasic, g_CombatConfig.tactical.aim.eAimCostAttrib);
			if (fAimCostAttrib <= 0.f)
			{
				notifySendTacticalDisabled(pEnt,"TacticalNotify.AimCost");
				return;
			}
		}
		
		if (pmKnockIsActive(pEnt))
		{
			notifySendTacticalDisabled(pEnt,"TacticalNotify.CharacterKnocked");
			return;
		}

	}

}


static F32 gclEntityScreenBounding_GetAdjustmentRate(EntityScreenBoundingBoxAccelConfig *pConfig, F32 fLen, F32 fVelocity, F32 fDeltaTime)
{
	F32 fRate; 

	fRate = fVelocity + pConfig->fBaseRate;

	if (pConfig->fMaxRate)
		fRate = MIN(fRate, pConfig->fMaxRate);
	
	fRate *= fDeltaTime;

	if (fRate > fLen)
		fRate = fLen;

	return fRate;
}

void gclEntityScreenBounding_UpdateAndGetAdjustedTargetBoxBounds(Entity* e, Vec3 vBoundMinInOut, Vec3 vBoundMaxInOut, bool bSnap)
{
	F32 fDeltaTime = gGCLState.frameElapsedTime;
	EntityScreenBoundingBoxAccelConfig *pConfig = gProjectGameClientConfig.pScreenBoundingAccelConfig;
	Vec3 vDir;
	F32 fRate, fLenMin, fLenMax;

	if (!e || !e->pEntUI)
		return;

	
	if (vec3IsZero(e->pEntUI->vBoxMin) && vec3IsZero(e->pEntUI->vBoxMax))
	{
		// uninitialized targeting box, set it and return
		copyVec3(vBoundMinInOut, e->pEntUI->vBoxMin);
		copyVec3(vBoundMaxInOut, e->pEntUI->vBoxMax);
		return;
	}

	if (e->pEntUI->uiLastTime == gGCLState.totalElapsedTimeMs)
	{
		copyVec3(e->pEntUI->vBoxMin, vBoundMinInOut);
		copyVec3(e->pEntUI->vBoxMax, vBoundMaxInOut);
		return;
	}

	e->pEntUI->uiLastTime = gGCLState.totalElapsedTimeMs;

	// do the calculations for the minimum
	subVec3(vBoundMinInOut, e->pEntUI->vBoxMin, vDir);
	fLenMin = normalVec3(vDir);
	if (bSnap) {
		copyVec3(vBoundMinInOut, e->pEntUI->vBoxMin);
	} else {
		fRate = gclEntityScreenBounding_GetAdjustmentRate(pConfig, fLenMin, e->pEntUI->fBoundingAdjustVelocity, fDeltaTime);
		if (fRate)
			scaleAddVec3(vDir, fRate, e->pEntUI->vBoxMin, e->pEntUI->vBoxMin);
	}
	
	// do the calculations for the maximum
	subVec3(vBoundMaxInOut, e->pEntUI->vBoxMax, vDir);
	fLenMax = normalVec3(vDir);
	if (bSnap) {
		copyVec3(vBoundMaxInOut, e->pEntUI->vBoxMax);
	} else {		
		fRate = gclEntityScreenBounding_GetAdjustmentRate(pConfig, fLenMax, e->pEntUI->fBoundingAdjustVelocity, fDeltaTime);
		if (fRate)
			scaleAddVec3(vDir, fRate, e->pEntUI->vBoxMax, e->pEntUI->vBoxMax);
	}

	// see if we should start or stop acceleration
	if (pConfig->fAcceleration)
	{
		if (e->pEntUI->fBoundingAdjustVelocity)
		{
			if (fLenMin < pConfig->fAccelerateStopThreshold && 
				fLenMax < pConfig->fAccelerateStopThreshold)
			{
				e->pEntUI->fBoundingAdjustVelocity = 0.f;
			}
			else
			{
				e->pEntUI->fBoundingAdjustVelocity += pConfig->fAcceleration * fDeltaTime;
				if (pConfig->fMaxRate)
					MIN1(e->pEntUI->fBoundingAdjustVelocity, pConfig->fMaxRate);
			}
		}
		else
		{
			if (fLenMin >= pConfig->fAccelerateStartThreshold || 
				fLenMax >= pConfig->fAccelerateStartThreshold)
			{
				e->pEntUI->fBoundingAdjustVelocity = pConfig->fAcceleration * fDeltaTime;
			}
		}
	}


	copyVec3(e->pEntUI->vBoxMin, vBoundMinInOut);
	copyVec3(e->pEntUI->vBoxMax, vBoundMaxInOut);
}

AUTO_STRUCT;
typedef struct PendingLootGlowEvent
{
	EntityRef erEnt;
	const char* pchFXStart;	AST(UNOWNED)
	U32 uiTimeExpire;
} PendingLootGlowEvent;

static PendingLootGlowEvent** s_eaPendingLootGlowEnts = NULL;

void gclEntityAddPendingLootGlow(EntityRef iDeadEntRef, const char* pchFXStart)
{
	PendingLootGlowEvent* pPending = StructCreate(parse_PendingLootGlowEvent);
	pPending->erEnt = iDeadEntRef;
	pPending->pchFXStart = allocAddString(pchFXStart);
	pPending->uiTimeExpire = pmTimestamp(2);
	eaPush(&s_eaPendingLootGlowEnts, pPending);
}

void gclCheckPendingLootGlowEnts()
{
	if (eaSize(&s_eaPendingLootGlowEnts) > 0)
	{
		int i;
		Entity* pPlayerEnt = entActivePlayerPtr();
		for (i = eaSize(&s_eaPendingLootGlowEnts)-1; i >= 0; i--)
		{
			Entity* pEnt = entFromEntityRefAnyPartition(s_eaPendingLootGlowEnts[i]->erEnt);
			bool bRemove = false;
			if (pEnt && 
				entCheckFlag(pEnt, ENTITYFLAG_DEAD) &&
				pEnt->pCritter && 
				inv_HasLoot(pEnt))
			{
				if (reward_MyDrop(pPlayerEnt, pEnt))
				{
					dtFxManAddMaintainedFx(pEnt->dyn.guidFxMan, s_eaPendingLootGlowEnts[i]->pchFXStart, NULL, 0, 0, eDynFxSource_HardCoded);
					pEnt->pCritter->pcLootGlowFX = s_eaPendingLootGlowEnts[i]->pchFXStart;
				}
				bRemove = true;
			}
			else if (s_eaPendingLootGlowEnts[i]->uiTimeExpire <= pmTimestamp(0))
			{
				bRemove = true;
			}
			if (bRemove)
			{
				StructDestroy(parse_PendingLootGlowEvent, s_eaPendingLootGlowEnts[i]);
				eaRemove(&s_eaPendingLootGlowEnts, i);
			}
		}
	}
}

static void gclDumpMakeFileName(char **esPath, char *pcFileName)
{
	U32 i;
	Entity* pPlayerEnt = entActivePlayerPtr();
	if(pPlayerEnt)
	{
		char *esFilename = NULL;
		estrPrintf(&esFilename, "%s_%s", pPlayerEnt->debugName, timeGetLocalSystemStyleStringFromSecondsSince2000(timeSecondsSince2000()));
		// replace . and :
		for(i = 0; i < estrLength(&esFilename); ++i)
		{
			if((esFilename[i] >= ' ' && esFilename[i] <'0') || (esFilename[i] > '9' && esFilename[i] <'A'))
			{
				esFilename[i] = '_';
			}
		}

		estrPrintf(esPath, "c:\\queueserverdump\\%s_%s.txt",pcFileName, esFilename);
		estrDestroy(&esFilename);

	}
	else
	{
		estrPrintf(esPath, "c:\\queueserverdump\\%s_%d.txt", pcFileName, timeSecondsSince2000());		
	}
}

// write out queue instance 
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(7);
void gclDumpQueueInstance(QueueInstance *pInstance)
{
	if(pInstance)	
	{
		char *esPath = NULL;
		gclDumpMakeFileName(&esPath, "qIns");
		ParserWriteTextFile(esPath, parse_QueueInstance, pInstance, 0, 0);
		estrDestroy(&esPath);
	}
}

// write out queue info
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(7);
void gclDumpQueueInfo(QueueInfo *pInfo)
{
	if(pInfo)	
	{
		char *esPath = NULL;
		gclDumpMakeFileName(&esPath, "qInfo");
		ParserWriteTextFile(esPath, parse_QueueInfo, pInfo, 0, 0);
		estrDestroy(&esPath);
	}
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void gclPlayChangedCostumePartFX(S32 eBagID, S32 iSlot)
{
	int i;
	Entity* pEnt = entActivePlayerPtr();
	WLCostume* pWLCostume = SAFE_GET_REF(pEnt, hWLCostume);
	for (i = 0; i < eaSize(&g_ItemConfig.eaBagToFxMaps); i++)
	{
		if (g_ItemConfig.eaBagToFxMaps[i]->eID == eBagID && 
			((g_ItemConfig.eaBagToFxMaps[i]->eSlotType == kSlotType_Any) ||
			(g_ItemConfig.eaBagToFxMaps[i]->eSlotType == kSlotType_Primary && iSlot == 0) ||
			(g_ItemConfig.eaBagToFxMaps[i]->eSlotType == kSlotType_Secondary && iSlot != 0)))
		{

			dtAddFxAutoRetry(pEnt->dyn.guidFxMan, g_ItemConfig.eaBagToFxMaps[i]->pchFXName, NULL, 0, 0, 0.0, 0, eDynFxSource_HardCoded, NULL, NULL);

			return;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetLastMap");
const char *gclEntExprGetLastMap(SA_PARAM_OP_VALID Entity *pEnt)
{
	SavedMapDescription *pLastMap = entity_GetLastMap(pEnt);
	if (pLastMap)
		return pLastMap->mapDescription;
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetLastMapDisplayName");
const char *gclEntExprGetLastMapDisplayName(SA_PARAM_OP_VALID Entity *pEnt)
{
	const char *pchMap = gclEntExprGetLastMap(pEnt);
	ZoneMapInfo *pZone = RefSystem_ReferentFromString("ZoneMapInfo", pchMap);
	if (pZone)
		return TranslateMessagePtr(zmapInfoGetDisplayNameMessagePtr(pZone));
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AllegianceCanReturnFromMap");
bool gclEntExprAllegianceCanTranswarp(SA_PARAM_OP_VALID Entity *pEnt)
{
	AllegianceDef *pAllegiance = SAFE_GET_REF(pEnt, hSubAllegiance);

	if (!pAllegiance || !pAllegiance->pWarpRestrict)
	{
		pAllegiance = SAFE_GET_REF(pEnt, hAllegiance);
	}

	if (pAllegiance && pAllegiance->pWarpRestrict)
	{
		MissionInfo *pInfo;

		if (entity_GetSavedExpLevel(pEnt) < pAllegiance->pWarpRestrict->iRequiredLevel)
			return false;

		pInfo = mission_GetInfoFromPlayer(pEnt);
		if (!pInfo || !eaIndexedGetUsingString(&pInfo->completedMissions, pAllegiance->pWarpRestrict->pchRequiredMission))
			return false;
	}
	return true;
}

#include "AutoGen/gclEntity_h_ast.c"
#include "AutoGen/gclEntity_c_ast.c"
// TODO: end
