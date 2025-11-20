#include "WorldColl.h"
#include "wlBeacon.h"
#include "WorldLib.h"
#include "Entity.h"
#include "GlobalComm.h"
#include "GameServerLib.h"
#include "EntDebugMenu.h"
#include "beacon.h"
#include "BeaconDebug.h"
#include "WorldGrid.h"
#include "beaconPath.h"
#include "ControllerScriptingSupport.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "../worldlib/AutoGen/beaconDebug_h_ast.h"
#include "Quat.h"
#include "logging.h"
#include "Player.h"

typedef struct GSLBeaconDebugState {
	int logbcns;
	int log_index;
} GSLBeaconDebugState;

GSLBeaconDebugState gsl_bcn_debug_state;

AUTO_COMMAND;
void sendCRCToOtherServer(int cid)
{
	U32 crc = wcGenerateCollCRC(0, 0, 1, NULL);

	RemoteCommand_compareCRCFromOther(GLOBALTYPE_GAMESERVER, cid, crc);
}

AUTO_COMMAND_REMOTE;
void compareCRCFromOther(U32 other_crc)
{
	U32 crc = wcGenerateCollCRC(0, 0, 1, NULL);

	if(crc != other_crc)
	{
		ControllerScript_Failed("World Collision CRC Production Mode / Non-Production mode failed.");
	} 
	else
	{
		ControllerScript_Succeeded();
	}
}

AUTO_COMMAND ACMD_NAME(bcnCheckFile) ACMD_SERVERONLY;
void beaconIsCurrentMapUptoDate(Entity *clientEnt)
{
#if !defined(_XBOX) && defined(GAMESERVER)
	U32 uptodate = 0;
	S32 procVersion = 0;

	U32 crc = wcGenerateCollCRC(0, 0, 1, NULL);

	uptodate = crc == beaconFileGetCRC(&procVersion) && procVersion >= beaconFileGetProcVersion();

	ClientCmd_beaconIsCurrentMapUptoDateClient(clientEnt, uptodate, wlDontLoadBeacons());
#endif
}

AUTO_COMMAND ACMD_SERVERONLY;
void beaconWriteDetailedCRC(void)
{
	wcGenerateCollCRC(0, 1, 1, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconIsCurrentMapUptoDateRemote(Entity *clientEnt)
{
	beaconIsCurrentMapUptoDate(clientEnt);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD;
void beaconSetBeaconDebug(Entity *ent)
{
	BcnDebugger *newDebugger;
	PlayerDebug* debug = entGetPlayerDebug(ent, true);

	if(!debug)
		return;

	PERFINFO_AUTO_START_FUNC();

	if(!debug->bcnDebugInfo)
	{
		debug->bcnDebugInfo = StructAlloc(parse_BeaconDebugInfo);
		entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
	}

	newDebugger = callocStruct(BcnDebugger);
	newDebugger->partitionIdx = entGetPartitionIdx(ent);

	eaiSetSize(&newDebugger->connsSent, combatBeaconArray.size);
	newDebugger->max_dist = 400;
	entGetPos(ent, newDebugger->pos);
	newDebugger->ref = entGetRef(ent);
	newDebugger->KBpf = 20;
	newDebugger->max_pkt_size = newDebugger->KBpf * 1024;

	eaPush(&bcnDebuggers, newDebugger);

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD;
void beaconDebugStop(Entity *ent)
{
	int i;
	EntityRef ref = entGetRef(ent);

	for(i=0; i<eaSize(&bcnDebuggers); i++)
	{
		BcnDebugger *dbger = bcnDebuggers[i];
		if(dbger->ref==ref)
		{
			eaiDestroy(&dbger->connsSent);

			eaRemoveFast(&bcnDebuggers, i);
		}
	}
}

BcnDebugger* beaconDebugFindDebugger(EntityRef ref)
{
	int i;

	for(i=0; i<eaSize(&bcnDebuggers); i++)
	{
		if(bcnDebuggers[i]->ref==ref)
		{
			return bcnDebuggers[i];
		}
	}

	return NULL;
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconSetBeaconSpeed(Entity *ent, F32 KBpf)
{
	EntityRef ref = entGetRef(ent);
	BcnDebugger *dbger = NULL;

	dbger = beaconDebugFindDebugger(ref);

	if(dbger)
	{
		dbger->KBpf = KBpf;
		dbger->max_pkt_size = dbger->KBpf * 1024;
	}
}

void beaconDebugDestroyDebugger(BcnDebugger *dbger)
{
	eaiDestroy(&dbger->connsSent);

	free(dbger);
}

void beaconDebugMapUnloadServer(void)
{
	eaDestroyEx(&bcnDebuggers, beaconDebugDestroyDebugger);
}

void beaconDebugMapLoadServer(ZoneMap *zmap)
{
	//eaDestroyEx(&bcnDebuggers, beaconDebugDestroyDebugger);
}

void beaconDebugOncePerFrame(F32 timeElapsed)
{
	int i;
	static int init = 0;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!init)
	{
		worldLibSetBcnCallbacks(beaconDebugMapUnloadServer, beaconDebugMapLoadServer);
	}

	if(eaSize(&bcnDebuggers))
		beaconDebugEnable(true);
	else
		beaconDebugEnable(false);

	for(i=0; i<eaSize(&bcnDebuggers); i++)
	{
		BcnDebugger *dbger = bcnDebuggers[i];

		if(dbger->ref)
		{
			Entity *ent = entFromEntityRefAnyPartition(dbger->ref);
			PlayerDebug* debug = entGetPlayerDebug(ent, false);

			if(!ent || !debug)
			{
				eaRemoveFast(&bcnDebuggers, i);
				i--;
			}
			else
			{
				// Good ent, let's give him some beacons or connections
				ClientLink *cl = entGetClientLink(ent);
				if(!cl)
				{
					eaRemoveFast(&bcnDebuggers, i);
					i--;
				}
				else
				{
					Packet *pkt = pktCreate(cl->netLink, TOCLIENT_DEBUG_BEACONSTUFF);

					entGetPos(ent, dbger->pos);
					beaconDebugUpdateDebugger(dbger, pkt, timeElapsed);

					pktSend(&pkt);
				}

				if(debug->bcnDebugInfo)
				{
					BeaconDebugInfo *info = debug->bcnDebugInfo;

					if(info->sendPath)
					{
						static Vec3 last_start_pos;
						static Vec3 last_end_pos;
						if(	!sameVec3(last_start_pos, info->startPos) ||
							!sameVec3(last_end_pos, info->endPos))
						{
							NavSearchResultType result;
							NavPath path = {0};

							beaconSetPathFindEntity(entGetRef(ent), info->pathJumpHeight, info->entHeight);
							result = beaconPathFind(entGetPartitionIdx(ent), &path, info->startPos, info->endPos, NULL);

							eaDestroyStruct(&info->path.nodes, parse_BeaconDebugPathNode);
							if(result == NAV_RESULT_SUCCESS)
							{
								int j;
								for(j=0; j<eaSize(&path.waypoints); j++)
								{
									NavPathWaypoint *wp = path.waypoints[j];
									BeaconDebugPathNode *node = StructAlloc(parse_BeaconDebugPathNode);

									copyVec3(wp->beacon->pos, node->pos);
									node->type = wp->connectType;

									eaPush(&info->path.nodes, node);
								}
							}

							navPathClear(&path);
							entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
						}
					}
				}
			}
		}
	}

	beaconDebugFrameEnd();

	if(gsl_bcn_debug_state.logbcns)
	{
		for(i=0; i<4000 && gsl_bcn_debug_state.log_index<combatBeaconArray.size; i++, gsl_bcn_debug_state.log_index++)
		{
			Beacon *b = combatBeaconArray.storage[gsl_bcn_debug_state.log_index];
			objLog(LOG_BEACON, 0, 0, 0, NULL, &b->pos, NULL, "Bcn", NULL, NULL);
		}

		if(gsl_bcn_debug_state.log_index==combatBeaconArray.size)
		{
			gsl_bcn_debug_state.logbcns = 0;
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND;
void bcnLogBeacons(void)
{
	gsl_bcn_debug_state.log_index = 0;
	gsl_bcn_debug_state.logbcns = 1;
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconDebugSetStartPoint(Entity *ent, Vec3 pos)
{
	PlayerDebug* debug = entGetPlayerDebug(ent, false);

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;

		if(info)
		{
			copyVec3(pos, info->startPos);
			info->sendPath = 1;
			entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconDebugSetEndPoint(Entity *ent, Vec3 pos)
{
	PlayerDebug* debug = entGetPlayerDebug(ent, false);

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;

		if(info)
		{
			copyVec3(pos, info->endPos);
			entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconDebugResetPathSend(Entity *ent)
{
	PlayerDebug* debug = entGetPlayerDebug(ent, false);

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;

		if(info)
		{
			info->sendPath = 0;
			entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconDebugSetPathJumpHeight(Entity *ent, F32 height)
{
	PlayerDebug* debug = entGetPlayerDebug(ent, false);

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;

		if(info)
		{
			info->pathJumpHeight = height;
			entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD;
void beaconDebugSetPathEntHeight(Entity *ent, F32 height)
{
	PlayerDebug* debug = entGetPlayerDebug(ent, false);

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;

		if(info)
		{
			info->entHeight = height;
			entity_SetDirtyBit(ent, parse_Player, ent->pPlayer, false);
		}
	}
}

AUTO_COMMAND;
void wcRayCapPerfTest(Entity *ent)
{
	int i;
	Vec3 startpos = {0};
	Vec3 endlocs[360];
	Quat q;
	Vec3 dir = {0, 0, 1}, tempvec = {0, 0, 1};
	Mat4 mat;
	Vec3 local_min = {-1.5, 0, -1.5}, local_max = {1.5, 6, 1.5};
	int iPartitionIdx = entGetPartitionIdx(ent);

	copyMat4(unitmat, mat);
	
	axisAngleToQuat(upvec, 2*PI/ARRAY_SIZE(endlocs), q);
	quatNormalize(q);
	
	if(ent)
	{
		entGetPos(ent, startpos);
	}

	copyVec3(startpos, mat[3]);

	for(i=0; i<ARRAY_SIZE(endlocs); i++)
	{
		quatRotateVec3(q, dir, tempvec);
		copyVec3(tempvec, dir);

		scaleAddVec3(dir, 100, startpos, endlocs[i]);
	}

	timerRecordStart("raytest");
	for(i=0; i<ARRAY_SIZE(endlocs); i++)
	{
		wcCapsuleCollide(worldGetActiveColl(iPartitionIdx), startpos, endlocs[i], WC_QUERY_BITS_WORLD_ALL, NULL);
	}
	
	for(i=0; i<ARRAY_SIZE(endlocs); i++)
	{
		wcRayCollide(worldGetActiveColl(iPartitionIdx), startpos, endlocs[i], WC_QUERY_BITS_WORLD_ALL, NULL);
	}

	for(i=0; i<ARRAY_SIZE(endlocs); i++)
	{
		wcBoxCollide(worldGetActiveColl(iPartitionIdx), local_min, local_max, mat, endlocs[i], WC_QUERY_BITS_WORLD_ALL, NULL);
	}
	timerRecordEnd();
}

int beaconPathEntrySort(const BeaconPathLogEntry **left, const BeaconPathLogEntry **right)
{
	return (*left)->totalCycles > (*right)->totalCycles ? 1 : -1;
}

void beaconCreateDebugMenu(Entity *e, DebugMenuItem* groupRoot)
{
	PlayerDebug* debug = entGetPlayerDebug(e, false);
	debugmenu_AddNewCommand(groupRoot, "Show path perf info", "showpathperf$$delayedcmd 5 mmm");

	if(debug && debug->showPathPerfCmds)
	{
		int i;
		U32 colors[] = {0xFF00FF00, 0xFF00FF7F, 0xFF00FFFF, 0xFF007FFF, 0xFF0000FF, 0xFFFF00FF, 0xFFFF0000};
		DebugMenuItem *path_perf = debugmenu_AddNewCommandGroup(groupRoot, "Pathing Perf", "Hi there", 0);

		PERFINFO_AUTO_START_FUNC();
		for(i=0; i<beaconPathLogSetsGetSize(); i++)
		{
			int j;
			BeaconPathLogEntrySet *set = bcnPathLogSets[i];
			char set_text[512];
			DebugMenuItem *set_item;

			if(set)
			{
				sprintf(set_text, "%"FORM_LL"d - %"FORM_LL"d Cycles", beaconPathLogGetSetLimitMin(i), beaconPathLogGetSetLimitMax(i));
				set_item = debugmenu_AddNewCommandGroup(path_perf, set_text, "What?!", 0);

				eaQSort(set->entries, beaconPathEntrySort);

				for(j=0; j<eaSize(&set->entries); j++)
				{
					BeaconPathLogEntry *entry = set->entries[j];
					DebugMenuItem *entry_item = NULL;
					char entry_text[512];
					char disp_text[512];
					Entity *pathE = entFromEntityRefAnyPartition(entry->entityRef);

					if(!pathE && debug->showPathPerfLiving)
						continue;

					sprintf(disp_text, "Cycles: %"FORM_LL"d/%"FORM_LL"d/%"FORM_LL"d/%"FORM_LL"d", 
										entry->totalCycles, entry->findSource, entry->findTarget, entry->findPath);
					sprintf(entry_text, "%s\n"
										"EntRef: %d | src: %.2f %.2f %.2f | dst: %.2f %.2f %.2f\n"
										"File: %s:%d\n"
										"Trivia: %s", 
										disp_text,							 
										entry->entityRef, vecParamsXYZ(entry->sourcePos), vecParamsXYZ(entry->targetPos),
										entry->ownerFile, entry->ownerLine,
										entry->triviaStr ? entry->triviaStr : "");
					entry_item = debugmenu_AddNewCommandGroup(set_item, disp_text, entry_text, 0);

					sprintf(entry_text, "setpos %f %f %f", vecParamsXYZ(entry->sourcePos));
					debugmenu_AddNewCommand(entry_item, "Teleport to Source", entry_text);
					sprintf(entry_text, "setpos %f %f %f", vecParamsXYZ(entry->targetPos));
					debugmenu_AddNewCommand(entry_item, "Teleport to Target", entry_text);
					if(entry->entityRef)
					{
						sprintf(entry_text, "ec me setposatent %d", entry->entityRef);
						debugmenu_AddNewCommand(entry_item, "Teleport to Critter", entry_text);
					}
					sprintf(entry_text, "worldDebugAddLineClient %f %f %f %f %f %f %u", vecParamsXYZ(entry->sourcePos), vecParamsXYZ(entry->targetPos), colors[i]);
					debugmenu_AddNewCommand(entry_item, "Draw Line", entry_text);
				}
			}
		}
		PERFINFO_AUTO_STOP();
	}
}

AUTO_COMMAND;
void showPathPerf(Entity *e)
{
	PlayerDebug* debug = entGetPlayerDebug(e, true);
	if(debug)
	{
		debug->showPathPerfCmds = !debug->showPathPerfCmds;
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}

AUTO_COMMAND;
void showPathPerfLivingOnly(Entity *e)
{
	PlayerDebug* debug = entGetPlayerDebug(e, true);
	if(debug)
	{
		debug->showPathPerfLiving = !debug->showPathPerfLiving;
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}

AUTO_RUN;
void worldRegisterDebugMenu(void)
{
	debugmenu_RegisterNewGroup("Path Perf", beaconCreateDebugMenu);
}

AUTO_RUN;
void worldDebugSetClientCmds(void)
{
	wlSetDrawPointClientFunc(ClientCmd_worldDebugAddPointServer);
	wlSetDrawLineClientFunc(ClientCmd_worldDebugAddLineServer);
	wlSetDrawBoxClientFunc(ClientCmd_worldDebugAddBoxServer);
	wlSetDrawTriClientFunc(ClientCmd_worldDebugAddTriServer);
	wlSetDrawQuadClientFunc(ClientCmd_worldDebugAddQuadServer);
}

AUTO_COMMAND;
void beaconSetRebuildFlag(Entity *e)
{
	if(e)
	{
		bcnSetRebuildFlag(entGetPartitionIdx(e));
	}
}

AUTO_COMMAND ACMD_SERVERONLY;
void bcnRebuild(Entity *e)
{
	if(e)
		beaconCheckBlocksNeedRebuild(entGetPartitionIdx(e));
	else
		printf("No partition");
}

AUTO_COMMAND ACMD_SERVERONLY;
void bcnRebuildEnable(int enabled)
{
	gEnableRebuild = !!enabled;
}