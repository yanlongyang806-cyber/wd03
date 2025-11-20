
#include "beaconDebug.h"
#include "Combat/ClientTargeting.h"
#include "EditorManager.h"
#include "GameClientLib.h"
#include "gclEntity.h"
#include "GenericMesh.h"
#include "GfxCamera.h"
#include "GfxDebug.h"
#include "GfxPrimitive.h"
#include "LineDist.h"
#include "net/netpacketutil.h"
#include "PhysicsSDK.h"
#include "Player.h"
#include "gclUIGenMap.h"
#include "TimedCallback.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

// Cheating libification because this is code debugging the below...
#include "../WorldLib/beaconClientServerPrivate.h"
#include "../WorldLib/AutoGen/beaconClientServerPrivate_h_ast.h"

// For the debug window!

// GDebug
#include "gDebug.h"

#include "../Common/AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ASTLine", BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ASTPoint", BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ASTBox", BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ASTQuad", BUDGET_Editors););

void worldDebugAddPointClient(Entity *e, const Vec3 point, U32 color);
void worldDebugAddLineClient(Entity *e, const Vec3 start, const Vec3 end, U32 color);
void worldDebugAddBoxClient(Entity *e, const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color);
void worldDebugAddTriClient(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled);
void worldDebugAddQuadClient(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color);

U32 g_uptodate = 1;
U32 g_notloaded = 0;
NetComm *g_beacon_comm;
NetLink *g_beacon_link;

typedef struct Debug3DInfo {
	ASTPoint **points;
	ASTLine **lines;
	ASTTri **tris;
	ASTBox **boxes;
	ASTQuad **quads;
	GMesh **meshes;
} Debug3DInfo;

Debug3DInfo debug3d;

#define DEF_ENUM(enum) {#enum, enum}

StaticDefineInt s_bcndbglist[] = {
	DEFINE_INT	
	DEF_ENUM(bdBeacons),
	DEF_ENUM(bdNearby),
	DEF_ENUM(bdGround),
	DEF_ENUM(bdRaised),
	DEFINE_END
};

#undef DEF_ENUM

typedef struct BeaconDebugGUI
{
	BeaconDebugFlags flags;

	UIWindow *window;

	UIRadioButtonGroup *modes;

	UIRadioButton *beacon;
	UIRadioButton *nearby;
	UIRadioButton *ground;
	UIRadioButton *raised;
	UIRadioButton *subblock;
	UIRadioButton *galaxy;
	UIRadioButton *cluster;

	UIButton *pathMode;
	UIButton *pathStart;
	UIButton *pathReset;

	UILabel *distance;
	UISlider *slider;

	UILabel *status;
	UIProgressBar *loading;

	UILabel *help;
	UILabel *help2;

	U32 path_mode_mouse : 1;
} BeaconDebugGUI;

typedef struct BeaconizerDebugGUI {
	UIWindow *window;

	UISlider *orig_point;
	UISlider *near_point;
	UICheckButton *draw_gen;
	UICheckButton *draw_combat;
	UICheckButton *draw_walk;
	UICheckButton *draw_prune;
	UICheckButton *draw_pre_rebuild;
	UICheckButton *draw_post_rebuild;
	UIButton *setdebugpos;
	UIButton *cleardbg;

	Vec3 debugpos;
	F32 orig_dist;
	F32 near_dist;
} BeaconizerDebugGUI;

BeaconDebugGUI gbdGUI;
BeaconizerDebugGUI gbizerGUI;

void beaconDebugRadioToggle(UIAnyWidget *widget, UserData data)
{
	widget = ui_RadioButtonGroupGetActive(gbdGUI.modes);
	if(widget == gbdGUI.beacon)
	{
		ui_LabelSetText(gbdGUI.help, "Purple hash marks are beacons.");
		ui_LabelSetText(gbdGUI.help2, "Aren't they pretty?");
		beaconDebugSetFlag(bdBeacons);
	}
	if(widget == gbdGUI.nearby)
	{
		ui_LabelSetText(gbdGUI.help, "Draws paths outwards centered on nearest");
		ui_LabelSetText(gbdGUI.help2, "beacon to player.  Green = close.  Red = far.");
		beaconDebugSetFlag(bdNearby);
	}
	if(widget == gbdGUI.ground)
	{
		ui_LabelSetText(gbdGUI.help, "Draws ground connections from beacon to beacon.");
		ui_LabelSetText(gbdGUI.help2, "Red = bidirectional, Purple = Start, Green = End.");
		beaconDebugSetFlag(bdGround);
	}
	if(widget == gbdGUI.raised)
	{
		ui_LabelSetText(gbdGUI.help, "Draws raised connections from beacon to beacon.");
		ui_LabelSetText(gbdGUI.help2, "Green = low connection.  Blue = high connection.");
		beaconDebugSetFlag(bdRaised);
	}	
	if(widget == gbdGUI.subblock)
	{
		ui_LabelSetText(gbdGUI.help, "Colors beacons according to subblock");
		ui_LabelSetText(gbdGUI.help2, "");
		beaconDebugSetFlag(bdSubblock);
	}
	if(widget == gbdGUI.galaxy)
	{
		ui_LabelSetText(gbdGUI.help, "Colors beacons according to galaxy");
		ui_LabelSetText(gbdGUI.help2, "Slider determines galaxy level");
		beaconDebugSetFlag(bdGalaxy);
	}
	if(widget == gbdGUI.cluster)
	{
		ui_LabelSetText(gbdGUI.help, "Draws cluster connections from source to dest.");
		ui_LabelSetText(gbdGUI.help2, "Blue = src.  Black = dest.");
		beaconDebugSetFlag(bdCluster);
	}	

	if(widget == gbdGUI.galaxy)
	{
		ui_SliderSetRange(gbdGUI.slider, 0, 19, 1);
		ui_SliderSetValue(gbdGUI.slider, 0);
	}
	else
	{
		ui_SliderSetRange(gbdGUI.slider, 0, 100, 0);
	}
}

void beaconDebugSliderChange(UIAnyWidget *widget, bool bFinished, UserData data)
{
	char text[MAX_PATH];
	beaconDebugSetLevel(ui_FloatSliderGetValue(gbdGUI.slider));

	sprintf(text, "Dist: %2.0f", ui_FloatSliderGetValue(gbdGUI.slider));
	ui_LabelSetText(gbdGUI.distance, text);
}

BeaconDebugInfo* beaconDebugGetInfo(void)
{
	Entity *ent = entActivePlayerPtr();
	PlayerDebug* debug = ent ? entGetPlayerDebug(ent, false) : NULL;

	if(debug)
	{
		return debug->bcnDebugInfo;
	}

	return NULL;
}

void beaconDebugPathModeChange(UIAnyWidget *widget, UserData data)
{
	if(gbdGUI.path_mode_mouse)
	{
		ui_ButtonSetText(gbdGUI.pathMode, "Player");
		cmdCmdOnClick(1, "", "");
	}
	else
	{
		ui_ButtonSetText(gbdGUI.pathMode, "Mouse");
		cmdCmdOnClick(1, "beaconDebugSetEndPoint <pos>", "");
	}

	gbdGUI.path_mode_mouse = !gbdGUI.path_mode_mouse;
}

void beaconDebugPathReset(UIAnyWidget *widget, UserData data)
{
	BeaconDebugInfo *info = beaconDebugGetInfo();

	if(info)
	{
		ServerCmd_beaconDebugResetPathSend();
	}
}

AUTO_COMMAND;
void beaconDebugSetStartAndSwitch(Vec3 pos)
{
	ServerCmd_beaconDebugSetStartPoint(pos);
	cmdCmdOnClick(1, "beaconDebugSetEndPoint <pos>", "");
}

void beaconDebugPathStart(UIAnyWidget *widget, UserData data)
{
	BeaconDebugInfo *info = beaconDebugGetInfo();

	if(gbdGUI.path_mode_mouse)
	{
		cmdCmdOnClick(1, "beaconDebugSetStartAndSwitch <pos>", "");
	}
	else
	{
		Entity *ent = entActivePlayerPtr();
		Vec3 pos;

		if(info && ent)
		{
			entGetPos(ent, pos);
			ServerCmd_beaconDebugSetStartPoint(pos);
		}
	}
}

void beaconMessageCallback(int numBeacons, int curBeacons)
{
	if(numBeacons > 0)
	{
		ui_ProgressBarSet(gbdGUI.loading, (F32)curBeacons/numBeacons);
		if(numBeacons < curBeacons)
		{
			ui_LabelSetText(gbdGUI.status, "Beacons");
		}
		else
		{
			ui_LabelSetText(gbdGUI.status, "Streaming");
		}
	}
}

void beaconDebugMapLoad(ZoneMap *map)
{
	beaconDebugMapLoadWorldClient(map);
}

void beaconDebugUninit(void)
{
	ui_WidgetQueueFree(UI_WIDGET(gbdGUI.window));
	ZeroStruct(&gbdGUI);
}

void beaconDebugMapUnload(void)
{
	beaconDebugMapUnloadWorldClient();

	beaconDebugUninit();
}

void beaconDrawHelper(Entity *player, void *userdata, DrawLineFunc linefunc, void *linedata, DrawSphereFunc spherefunc, void* spheredata)
{
#if !_XBOX && !_PS3
	PlayerDebug *debug;
	BeaconDebugInfo *info;
	Vec3 pos;

	if(!player)
		return;

	debug = entGetPlayerDebug(player, 0);

	if(!debug)
		return;

	info = debug->bcnDebugInfo;

	if(gGCLState.bUseFreeCamera)
		gfxGetActiveCameraPos(pos);
	else
		entGetPos(player, pos);
	beaconDrawDebug(info, pos, linefunc, linedata, spherefunc, spheredata);
#endif
}

void beaconDebugClearALWarnings(TimedCallback *pCallback, F32 timeSinceLastCallback, void *userdata)
{
	gfxDebugClearAccessLevelCmdWarnings();
}

void beaconDebugHide(void)
{
	beaconDebugEnable(0);
	ui_MapRemoveDebugDrawer(beaconDrawHelper);
	ui_WindowHide(gbdGUI.window);

	TimedCallback_RunAt(beaconDebugClearALWarnings, NULL, timeSecondsSince2000()+2);
}

bool beaconDebugClose(UIAnyWidget *widget, UserData data)
{
	beaconDebugHide();

	return true;
}

static void beaconDebugInit(void)
{
	gbdGUI.window = ui_WindowCreate("Beacon Debug", 10, 10, 400, 80);

	ui_WindowSetCloseCallback(gbdGUI.window, beaconDebugClose, NULL);

	// Set up radio buttons
	gbdGUI.modes = ui_RadioButtonGroupCreate();

	gbdGUI.beacon = ui_RadioButtonCreate(0*65, 0, "Beacon", gbdGUI.modes);
	gbdGUI.nearby = ui_RadioButtonCreate(1*65, 0, "Nearby", gbdGUI.modes);
	gbdGUI.ground = ui_RadioButtonCreate(2*65, 0, "Ground", gbdGUI.modes);
	gbdGUI.raised = ui_RadioButtonCreate(3*65, 0, "Raised", gbdGUI.modes);
	gbdGUI.subblock = ui_RadioButtonCreate(4*65-5, 0, "Subblock", gbdGUI.modes);
	gbdGUI.galaxy = ui_RadioButtonCreate(5*65-5, 0, "Galaxy", gbdGUI.modes);
	gbdGUI.cluster = ui_RadioButtonCreate(6*65, 0, "Cluster", gbdGUI.modes);

	ui_RadioButtonSetToggledCallback(gbdGUI.beacon, beaconDebugRadioToggle, NULL);
	ui_RadioButtonSetToggledCallback(gbdGUI.nearby, beaconDebugRadioToggle, NULL);
	ui_RadioButtonSetToggledCallback(gbdGUI.ground, beaconDebugRadioToggle, NULL);
	ui_RadioButtonSetToggledCallback(gbdGUI.raised, beaconDebugRadioToggle, NULL);
	ui_RadioButtonSetToggledCallback(gbdGUI.subblock, beaconDebugRadioToggle, NULL);
	ui_RadioButtonSetToggledCallback(gbdGUI.galaxy, beaconDebugRadioToggle, NULL);
	ui_RadioButtonSetToggledCallback(gbdGUI.cluster, beaconDebugRadioToggle, NULL);

	ui_WindowAddChild(gbdGUI.window, gbdGUI.beacon);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.nearby);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.ground);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.raised);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.subblock);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.galaxy);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.cluster);

	// Set up slider
	gbdGUI.distance = ui_LabelCreate("Dist: 100", 0, 20);
	gbdGUI.slider = ui_FloatSliderCreate(60, 20, 235, 0, 200, 100);

	ui_SliderSetChangedCallback(gbdGUI.slider, beaconDebugSliderChange, NULL);

	ui_WindowAddChild(gbdGUI.window, gbdGUI.slider);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.distance);

	// Set up help!
	gbdGUI.help = ui_LabelCreate("", 0, 40);
	gbdGUI.help2 = ui_LabelCreate("", 0, 50);

	ui_WindowAddChild(gbdGUI.window, gbdGUI.help);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.help2);

	// Set up loading info
	gbdGUI.loading = ui_ProgressBarCreate(70, 65, 225);
	gbdGUI.status = ui_LabelCreate("Beacons:", 0, 65);

	ui_WindowAddChild(gbdGUI.window, gbdGUI.loading);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.status);

	// Set up path info
	gbdGUI.pathMode = ui_ButtonCreate("Player", 300, 25, beaconDebugPathModeChange, NULL);
	gbdGUI.pathReset = ui_ButtonCreate("Clear", 300, 50, beaconDebugPathReset, NULL);
	gbdGUI.pathStart = ui_ButtonCreate("Start", 350, 50, beaconDebugPathStart, NULL);

	ui_WindowAddChild(gbdGUI.window, gbdGUI.pathMode);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.pathReset);
	ui_WindowAddChild(gbdGUI.window, gbdGUI.pathStart);

	beaconSetMessageCallback(beaconMessageCallback);

	// Set up defaults
	ServerCmd_beaconSetBeaconDebug();

	ui_RadioButtonGroupSetActiveAndCallback(gbdGUI.modes, gbdGUI.nearby);
	beaconDebugSetLevel(100);
	beaconDebugEnable(1);

	worldLibSetBcnCallbacks(beaconDebugMapUnload, beaconDebugMapLoad);
}

void beaconDebugShowHide(void)
{
	if(ui_WindowIsVisible(gbdGUI.window))
	{
		beaconDebugHide();
	}
	else
	{
		beaconDebugEnable(1);
		ui_MapAddDebugDrawer(beaconDrawHelper, NULL);
		ui_WindowShow(gbdGUI.window);
	}
}

AUTO_COMMAND ACMD_NAME(bcnDebug);
void beaconDebug(void)
{
	if(!gbdGUI.window)
	{
		beaconDebugInit();
	}

	beaconDebugShowHide();
}

typedef struct DebugPhase {
	union {
		struct {
			ASTColor **colors;
			ASTPoint **points;
			ASTLine **lines;
			ASTTri **tris;
			ASTPath **paths;
		} structs;
		void **arrays[5];
	};
} DebugPhase;

DebugPhase debug_phases[BDO_MAX];

void beaconDebugProcessDataMessage(Packet *pkt)
{
	int type;
	int pktCheck = 0;
	if(pktEnd(pkt))
	{
		return;
	}

	type = pktGetBits(pkt, 32);

	while(!pktEnd(pkt))
	{
		int ptiindex;

#ifdef BCNDBG_VERIFY_PKT
		pktCheck = pktGetBits(pkt, 16);
		assert(pktCheck==0xDEAD);
#endif

		ptiindex = pktGetBits(pkt, 32);

		if(ptiindex<0 || ptiindex>=4)
		{
			assert(0);
		}
		else
		{
			void *str = StructAllocVoid(debugptis[ptiindex]);
			void *instr = pktGetStruct(pkt, debugptis[ptiindex]);
			StructCopyAllVoid(debugptis[ptiindex], instr, str);
			eaPush(&debug_phases[type].arrays[ptiindex], str);
		}
#ifdef BCNDBG_VERIFY_PKT
		pktCheck = pktGetBits(pkt, 16);
		assert(pktCheck==0xFEED);
#endif
	}
}

void beaconDebugProcessResetMessage(Packet *pkt)
{
	int i, j;

	for(j=0; j<ARRAY_SIZE(debug_phases); j++)
	{
		for(i=0; i<ARRAY_SIZE(debug_phases[j].arrays); i++)
		{
			eaClearStructVoid(&debug_phases[j].arrays[i], debugptis[i]);
		}
	}
}

//typedef void PacketCallback(Packet *pkt,int cmd,NetLink* link,void *user_data);
void beaconDebugHandleMessage(Packet *pkt, int cmd, NetLink* link, void *user_data)
{
	switch(cmd)
	{
		xcase BDMSG_DATA: {
			beaconDebugProcessDataMessage(pkt);
		}

		xcase BDMSG_RESET: {
			beaconDebugProcessResetMessage(pkt);
		}
	}
}

//typedef void LinkCallback(NetLink* link,void *user_data);
void beaconDebugHandleConnect(NetLink* link,void *user_data)
{
	printf("Got connection.\n");
}

//typedef void LinkCallback(NetLink* link,void *user_data);
void beaconDebugHandleDisconnect(NetLink* link,void *user_data)
{
	
}

static void beaconInitDebugConnection(void)
{
	if(!g_beacon_comm)
	{
		g_beacon_comm = commCreate(0, 1);
	}
	linkFlushAndClose(&g_beacon_link, "New Connection");
}

void beaconSendDebugPos(UIAnyWidget *widget, UserData unused)
{
	if(g_beacon_link)
	{
		Vec3 pos;
		Entity *player = entActivePlayerPtr();
		if(player)
		{
			Packet *pak = pktCreate(g_beacon_link, BDMSG_POS);

			entGetPos(player, pos);
			pktSendVec3(pak, pos);
			pktSend(&pak);
		}
	}
}

void beaconDebugClearPhases(void)
{
	int i;

	for(i=0; i<ARRAY_SIZE(debug_phases); i++)
	{
		eaDestroyStruct(&debug_phases[i].structs.lines, parse_ASTLine);
		eaDestroyStruct(&debug_phases[i].structs.points, parse_ASTPoint);
		eaDestroyStruct(&debug_phases[i].structs.tris, parse_ASTTri);
		eaDestroyStruct(&debug_phases[i].structs.paths, parse_ASTPath);
		eaDestroyStruct(&debug_phases[i].structs.colors, parse_ASTColor);
	}
}

void beaconClearDraw(UIAnyWidget *widget, UserData data)
{
	beaconDebugClearPhases();
}

bool gbizerClose(UIAnyWidget *window, UserData data)
{
	beaconDebugClearPhases();

	return true;
}

void beaconOpenDebugUI(void)
{
	if(gbizerGUI.window)
	{
		ui_WindowClose(gbizerGUI.window);
		ZeroStruct(&gbizerGUI);
	}
	else
	{
		gbizerGUI.window = ui_WindowCreate("BeaconizerDebug", 5, 5, 400, 100);
		ui_WindowSetCloseCallback(gbizerGUI.window, gbizerClose, NULL);

		gbizerGUI.draw_combat = ui_CheckButtonCreate(5, 5, "Combat", 0);
		gbizerGUI.draw_gen = ui_CheckButtonCreate(65, 5, "Gen", 0);
		gbizerGUI.draw_walk	= ui_CheckButtonCreate(125, 5, "Walk", 0);
		gbizerGUI.draw_prune = ui_CheckButtonCreate(185, 5, "Prune", 0);
		gbizerGUI.draw_pre_rebuild = ui_CheckButtonCreate(245, 5, "Pre", 0);
		gbizerGUI.draw_post_rebuild = ui_CheckButtonCreate(305, 5, "Post", 0);
		gbizerGUI.near_point = ui_FloatSliderCreate(5, 25, 140, 0, 300, 100);
		gbizerGUI.orig_point = ui_FloatSliderCreate(155, 25, 140, 0, 300, 100);
		gbizerGUI.setdebugpos = ui_ButtonCreate("DbgPos", 305, 25, beaconSendDebugPos, NULL);
		gbizerGUI.cleardbg = ui_ButtonCreate("ClearDraw", 305, 45, beaconClearDraw, NULL);

		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.draw_combat);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.draw_gen);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.draw_walk);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.draw_prune);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.draw_pre_rebuild);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.draw_post_rebuild);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.near_point);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.orig_point);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.setdebugpos);
		ui_WindowAddChild(gbizerGUI.window, gbizerGUI.cleardbg);

		ui_WindowShow(gbizerGUI.window);
	}
}

AUTO_COMMAND ACMD_NAME(beaconizerDebug);
void beaconDebugBeaconizer(void)
{
	beaconInitDebugConnection();
	g_beacon_link = commConnect(g_beacon_comm, LINKTYPE_UNSPEC, 0, "127.0.0.1", BEACON_SERVER_DEBUG_PORT, beaconDebugHandleMessage, 
								beaconDebugHandleConnect, beaconDebugHandleDisconnect, 0);

	beaconOpenDebugUI();
}

AUTO_COMMAND ACMD_NAME(beaconizerDebugRemote);
void beaconDebugBeaconizerRemote(char *ipstr)
{
	beaconInitDebugConnection();
	g_beacon_link = commConnect(g_beacon_comm, LINKTYPE_UNSPEC, 0, ipstr, BEACON_SERVER_DEBUG_PORT, beaconDebugHandleMessage, 
								beaconDebugHandleConnect, beaconDebugHandleDisconnect, 0);

	beaconOpenDebugUI();
}

void drawMeshes(void ***meshArray, Vec3 playerPos, F32 dist)
{
	int i;

	for(i=0; i<eaSize(meshArray); i++)
	{
		GMesh *mesh = (*meshArray)[i];

		gfxDrawGMesh(mesh, ARGBToColor(0xFF00FF00), true);
	}
}

static int fillQuads = 0;
AUTO_CMD_INT(fillQuads, fillQuads);

void drawQuads(void ***quadArray, Vec3 playerPos, F32 dist)
{
	int i;
	int line_width = !fillQuads;
	for(i=0; i<eaSize(quadArray); i++)
	{
		ASTQuad *quad = (*quadArray)[i];
		// Wow, is this really worth it?  :-D Maybe I'll build two CTris for each one...
		if(pointLineDistSquared(playerPos, quad->p1, quad->p2, NULL)<SQR(dist) ||
			pointLineDistSquared(playerPos, quad->p2, quad->p3, NULL)<SQR(dist) ||
			pointLineDistSquared(playerPos, quad->p3, quad->p4, NULL)<SQR(dist) ||
			pointLineDistSquared(playerPos, quad->p4, quad->p1, NULL)<SQR(dist))
			gfxDrawQuad3D(quad->p1, quad->p2, quad->p3, quad->p4, ARGBToColor(quad->c), line_width);
	}
}

void drawTris(void ***triArray, Vec3 playerPos, F32 dist)
{
	int i;

	for(i=0; i<eaSize(triArray); i++)
	{
		ASTTri *tri = (*triArray)[i];
		gfxDrawTriangle3D_3ARGBEx(tri->p1, tri->p2, tri->p3, tri->c, tri->c, tri->c, 1.0f, tri->filled);
	}
}

void drawBoxes(void ***boxarray, Vec3 playerPos, F32 dist1, F32 dist2)
{
	Vec3 center = {0};
	int i;

	for(i=0; i<eaSize(boxarray); i++)
	{
		ASTBox *box = (*boxarray)[i];

		if(!playerPos || 1 /*???*/)
		{
			gfxDrawBox3DARGB(box->local_min, box->local_max, box->world_mat, box->c, 0);
		}
	}
}

void WorldDrawLine3D(Vec3 world_src, int color_src, Vec3 world_dst, int color_dst, DebugDrawUserData *userdata)
{
	gfxDrawLine3D_2ARGB(world_src, world_dst, color_src, color_dst);
}


static void drawLinesEx(void ***linearray, Vec3 playerpos, F32 dist, DrawLineFunc draw_func, void *data)
{
	int i;

	for(i=0; i<eaSize(linearray); i++)
	{
		ASTLine *line = (*linearray)[i];

		if(!playerpos || pointLineDistSquared(playerpos, line->p1, line->p2, NULL)<SQR(dist))
		{
			Vec3 start, end;
			copyVec3(line->p1, start);
			copyVec3(line->p2, end);

			draw_func(start, line->c, end, 0xFFFFFFFF, data);
		}
	}
}

__forceinline static void drawLines(void ***linearray, Vec3 playerpos, F32 dist)
{
	drawLinesEx(linearray, playerpos, dist, WorldDrawLine3D, NULL);
}

void drawPoints(void ***ptarray, Vec3 playerpos, F32 dist)
{
	int i;

	for(i=0; i<eaSize(ptarray); i++)
	{
		ASTPoint *point = (*ptarray)[i];
		Vec3 min, max;
		Vec3 offset = {.1, .1, .1};

		if(!playerpos || distance3Squared(playerpos, point->p)<SQR(dist))
		{
			subVec3(point->p, offset, min);
			addVec3(point->p, offset, max);

			gfxDrawBox3DARGB(min, max, unitmat, point->c, 0);
		}
	}
}

void beaconDrawBeaconizerDebug(Vec3 playerpos)
{
	static ASTLine **lines = NULL;
	static ASTPoint **points = NULL;

	if(gbizerGUI.orig_point)
	{
		gbizerGUI.orig_dist = ui_FloatSliderGetValue(gbizerGUI.orig_point);
	}

	if(gbizerGUI.near_point)
	{
		gbizerGUI.near_dist = ui_FloatSliderGetValue(gbizerGUI.near_point);
	}

	if(gbizerGUI.draw_gen && ui_CheckButtonGetState(gbizerGUI.draw_gen))
	{
		drawPoints(&debug_phases[BDO_GEN].structs.points, playerpos, gbizerGUI.near_dist);
		drawLines(&debug_phases[BDO_GEN].structs.lines, playerpos, gbizerGUI.near_dist);
	}
	
	if(gbizerGUI.draw_combat && ui_CheckButtonGetState(gbizerGUI.draw_combat))
	{
		drawPoints(&debug_phases[BDO_COMBAT].structs.points, playerpos, gbizerGUI.near_dist);
		drawLines(&debug_phases[BDO_COMBAT].structs.lines, playerpos, gbizerGUI.near_dist);
	}

	if(gbizerGUI.draw_walk && ui_CheckButtonGetState(gbizerGUI.draw_walk))
	{
		drawPoints(&debug_phases[BDO_WALK].structs.points, playerpos, gbizerGUI.near_dist);
		drawLines(&debug_phases[BDO_WALK].structs.lines, playerpos, gbizerGUI.near_dist);
	}

	if(gbizerGUI.draw_prune && ui_CheckButtonGetState(gbizerGUI.draw_prune))
	{
		drawPoints(&debug_phases[BDO_PRUNE].structs.points, playerpos, gbizerGUI.near_dist);
		drawLines(&debug_phases[BDO_PRUNE].structs.lines, playerpos, gbizerGUI.near_dist);
	}

	if(gbizerGUI.draw_pre_rebuild && ui_CheckButtonGetState(gbizerGUI.draw_pre_rebuild))
	{
		drawPoints(&debug_phases[BDO_PRE_REBUILD].structs.points, playerpos, gbizerGUI.near_dist);
		drawLines(&debug_phases[BDO_PRE_REBUILD].structs.lines, playerpos, gbizerGUI.near_dist);
	}

	if(gbizerGUI.draw_post_rebuild && ui_CheckButtonGetState(gbizerGUI.draw_post_rebuild))
	{
		drawPoints(&debug_phases[BDO_POST_REBUILD].structs.points, playerpos, gbizerGUI.near_dist);
		drawLines(&debug_phases[BDO_POST_REBUILD].structs.lines, playerpos, gbizerGUI.near_dist);
	}
}

static void beaconDebugDrawLineHelper(Vec3 v1, int c1, Vec3 v2, int c2, void* userdata)
{
	wlDrawLine3D_2(v1, c1, v2, c2);
}

static void beaconDebugDrawSphereHelper(Vec3 pos, F32 radius, int color, void *userdata)
{
	gfxDrawSphere3DARGB(pos, radius, 20, color, 0);
}

void gclBeaconOncePerFrame(Entity *e, Vec3 player_pos)
{
	int view = /*UserIsInGroup("Design") ||*/ UserIsInGroup("BeaconGroup");
	
	if(emIsEditorActive())
	{
		return;
	}

	if(g_beacon_comm)
	{
		commMonitor(g_beacon_comm);
		linkFlush(g_beacon_link);
	}

	if(!isProductionMode())
	{
		if(g_notloaded)
		{
			if(showDevUI() && view)
				gfxDebugPrintfQueue("Warning: Beacons not loaded on server.  AI may be (really) stupid.");
		}
		else
		{
			if(showDevUI() && !g_uptodate && view)
			{
				gfxDebugPrintfQueue("Warning: Beacons out of date.  AI may not act as desired.");
			}
		}
	}

	if(e->pPlayer && e->pPlayer->accessLevel >= 9)
	{
		PlayerDebug* debug = entGetPlayerDebug(e, false);
		BeaconDebugInfo *info = debug ? debug->bcnDebugInfo : NULL;

		if(info && beaconDebugIsEnabled())
		{
			if(!gbdGUI.path_mode_mouse)
			{
				ServerCmd_beaconDebugSetEndPoint(player_pos);
			}
#if !PLATFORM_CONSOLE
			beaconDrawDebug(info, player_pos, beaconDebugDrawLineHelper, NULL, beaconDebugDrawSphereHelper, NULL);
#endif
		}

		beaconDrawBeaconizerDebug(player_pos);
	}
}

void debug3DOncePerFrame(Vec3 pos)
{
	Vec3 cam_pos;
	gfxGetActiveCameraPos(cam_pos);
	drawPoints(&debug3d.points, cam_pos, 300);
	drawLines(&debug3d.lines, cam_pos, 300);
	drawBoxes(&debug3d.boxes, cam_pos, 300, 300);
	drawTris(&debug3d.tris, cam_pos, 300);
	drawQuads(&debug3d.quads, cam_pos, 300);
	drawMeshes(&debug3d.meshes, cam_pos, 300);
}

void debug3DDrawMapUIFunc(Entity *pPlayer, void *userdata, DrawLineFunc draw_func, DebugDrawUserData *data, DrawSphereFunc sphere_func, DebugDrawUserData *sphere_data)
{
	drawLinesEx(&debug3d.lines, NULL, 0.f, draw_func, data);
}

void gclWorldDebugOncePerFrame(void)
{
	Entity *e = entActivePlayerPtr();

	wlSetDrawPointClientFunc(worldDebugAddPointClient);
	wlSetDrawLineClientFunc(worldDebugAddLineClient);
	wlSetDrawBoxClientFunc(worldDebugAddBoxClient);
	wlSetDrawTriClientFunc(worldDebugAddTriClient);
	wlSetDrawQuadClientFunc(worldDebugAddQuadClient);

	if(e && e->pPlayer)
	{
		Vec3 player_pos;

		entGetPos(e, player_pos);
		gclBeaconOncePerFrame(e, player_pos);
		debug3DOncePerFrame(player_pos);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD;
void beaconIsCurrentMapUptoDateClient(U32 result, U32 notloaded)
{
	g_uptodate = !!result;
	g_notloaded = notloaded;
	if(isProductionMode())
	{
		return;
	}
	if(g_uptodate)
	{
		gfxStatusPrintf("Current beacon file is up to date.");
	}
	else
	{
		gfxStatusPrintf("Beacon file is not up to date.");
	}
}

AUTO_COMMAND;
void beaconDebugSetStartPoint(Vec3 pos)
{
	Entity *ent = entActivePlayerPtr();
	PlayerDebug* debug = ent ? entGetPlayerDebug(ent, false) : NULL;

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;
	
		if(info)
		{
			copyVec3(pos, info->startPos);
			info->sendPath = 1;

			ServerCmd_beaconDebugSetStartPoint(pos);
		}
	}
}

AUTO_COMMAND;
void beaconDebugResetPathSend(void)
{
	Entity *ent = entActivePlayerPtr();
	PlayerDebug* debug = ent ? entGetPlayerDebug(ent, false) : NULL;

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;
		info->sendPath = 0;

		ServerCmd_beaconDebugResetPathSend();
	}
}

AUTO_COMMAND;
void beaconDebugSetPathJumpHeight(F32 height)
{
	Entity *ent = entActivePlayerPtr();
	PlayerDebug* debug = ent ? entGetPlayerDebug(ent, false) : NULL;

	if(debug)
	{
		BeaconDebugInfo *info = debug->bcnDebugInfo;
		info->pathJumpHeight = height;

		ServerCmd_beaconDebugSetPathJumpHeight(height);
	}
}

void worldDebugAddPoint(const Vec3 point, U32 color)
{
	ASTPoint *pt = StructAlloc(parse_ASTPoint);

	copyVec3(point, pt->p);
	pt->c = color;

	eaPush(&debug3d.points, pt);
}

void worldDebugAddLine(const Vec3 start, const Vec3 end, U32 color)
{
	ASTLine *line = StructAlloc(parse_ASTLine);

	copyVec3(start, line->p1);
	copyVec3(end, line->p2);
	line->c = color;

	eaPush(&debug3d.lines, line);
}

void worldDebugAddBox(const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color)
{
	ASTBox *box = StructAlloc(parse_ASTBox);

	copyVec3(local_min, box->local_min);
	copyVec3(local_max, box->local_max);
	copyMat4(mat, box->world_mat);
	box->c = color;

	eaPush(&debug3d.boxes, box);
}

void worldDebugAddTri(const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled)
{
	ASTTri *tri = StructAlloc(parse_ASTTri);

	copyVec3(p1, tri->p1);
	copyVec3(p2, tri->p2);
	copyVec3(p3, tri->p3);

	tri->c = color;
	tri->filled = filled;

	eaPush(&debug3d.tris, tri);
}

void worldDebugAddQuad(const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color)
{
	ASTQuad *quad = StructAlloc(parse_ASTQuad);

	copyVec3(p1, quad->p1);
	copyVec3(p2, quad->p2);
	copyVec3(p3, quad->p3);
	copyVec3(p4, quad->p4);
	quad->c = color;

	eaPush(&debug3d.quads, quad);
}

void worldDebugAddGMesh(const GMesh *gmesh)
{
	GMesh *copy = calloc(1, sizeof(GMesh));

	gmeshCopy(copy, gmesh, 0);
	
	eaPush(&debug3d.meshes, copy);
}

AUTO_COMMAND;
void worldDebugAddPointClient(Entity *e, const Vec3 point, U32 color)
{
	worldDebugAddPoint(point, color);
}

AUTO_COMMAND;
void worldDebugAddLineClient(Entity *e, const Vec3 start, const Vec3 end, U32 color)
{
	worldDebugAddLine(start, end, color);
}

AUTO_COMMAND;
void worldDebugAddLineOffsetClient(Entity* e, const Vec3 start, const Vec3 offset, U32 color)
{
	Vec3 end;

	addVec3(start, offset, end);
	worldDebugAddLine(start, end, color);
}

AUTO_COMMAND;
void worldDebugAddLineDirLenClient(Entity* e, const Vec3 start, const Vec3 dir, F32 len, U32 color)
{
	Vec3 end;

	scaleAddVec3(dir, len, start, end);
	worldDebugAddLine(start, end, color);
}

AUTO_COMMAND;
void worldDebugAddLineYawLenClient(Entity* e, const Vec3 start, F32 yaw, F32 len, U32 color)
{
	Vec3 offset;

	offset[0] = sinf(yaw) * len;
	offset[1] = 0;
	offset[2] = cosf(yaw) * len;

	worldDebugAddLineOffsetClient(e, start, offset, color);
}

AUTO_COMMAND;
void worldDebugAddLineEntYawLenClient(Entity* e, const char* startEntStr, F32 yaw, F32 len, U32 color)
{
	Vec3 start;
	Entity *startEnt = entGetClientTarget(e, startEntStr, NULL);

	if(!startEnt)
		return;

	entGetPos(startEnt, start);

	worldDebugAddLineYawLenClient(e, start, yaw, len, color);
}

AUTO_COMMAND;
void worldDebugAddBoxClient(Entity *e, const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color)
{
	worldDebugAddBox(local_min, local_max, mat, color);
}

AUTO_COMMAND;
void worldDebugAddTriClient(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled)
{
	worldDebugAddTri(p1, p2, p3, color, filled);
}

AUTO_COMMAND;
void worldDebugAddQuadClient(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color)
{
	worldDebugAddQuad(p1, p2, p3, p4, color);
}

void worldDebugFreeGMesh(GMesh *mesh)
{
	gmeshFreeData(mesh);
	free(mesh);
}

AUTO_COMMAND;
void worldDebugClear(void)
{
	eaClearStruct(&debug3d.points, parse_ASTPoint);
	eaClearStruct(&debug3d.lines, parse_ASTLine);
	eaClearStruct(&debug3d.tris, parse_ASTTri);
	eaClearStruct(&debug3d.boxes, parse_ASTBox);
	eaClearStruct(&debug3d.quads, parse_ASTQuad);
	eaClearEx(&debug3d.meshes, worldDebugFreeGMesh);
}

AUTO_COMMAND;
void worldDebugDrawOnUIMap(int bEnable)
{
	if (bEnable)
	{
		ui_MapAddDebugDrawer(debug3DDrawMapUIFunc, NULL);
	}
	else
	{
		ui_MapRemoveDebugDrawer(debug3DDrawMapUIFunc);
	}
}


Debugger *worldDebugger;
DebuggerType wpcType;
DebuggerType phaseType;

typedef struct WorldPrimCollection {
	const char *name;

	ASTPoint **points;
	ASTLine **lines;
	ASTBox **box;
} WorldPrimCollection;

typedef struct WorldDebugPhase {
	const char *name;

	WorldPrimCollection **collections;
} WorldDebugPhase;

typedef struct WorldDebugUI {
	REF_TO(DebuggerObject) phases;
} WorldDebugUI;

WorldDebugUI worldDebugUI;

void worldPrimCollDebugMsgHandler(DebuggerObjectMsg *msg)
{
	WorldPrimCollection *wpc = msg->obj_data;
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			snprintf_s(msg->getName.out.name, msg->getName.out.len, "%s", wpc->name);
		}
	}
}

void worldPhaseDebugMsgHandler(DebuggerObjectMsg *msg)
{
	WorldDebugPhase *wdp = msg->obj_data;
	switch(msg->type)
	{
		xcase DOMSG_GETNAME: {
			snprintf_s(msg->getName.out.name, msg->getName.out.len, "%s", wdp->name);
		}
	}
}

void worldDebuggerInit(void)
{
	phaseType = gDebugRegisterDebugObjectType("WorldDbgPhase", worldPhaseDebugMsgHandler);
	wpcType = gDebugRegisterDebugObjectType("WorldPrimColl", worldPrimCollDebugMsgHandler);

	gDebuggerAddRoot(worldDebugger, "Root", NULL, worldDebugUI.phases);
}

AUTO_RUN;
void worldDebugInit(void)
{
	worldDebugger = gDebugRegisterDebugger("World");
	gDebuggerSetInitCallback(worldDebugger, worldDebuggerInit);
}

typedef struct TestFluid {
	WorldCollFluid *wcFluid;
	PSDKFluidParticle *particles;
	U32 numParticles;
	int color;
} TestFluid;

struct {
	WorldCollIntegration *wci;
	WorldCollScene *wcScene;
	WorldCollActor *wcActor;
	PSDKMeshDesc boxMeshDesc;

	Vec3 centerPos;

	TestFluid **testFluids;
	TestFluid **queuedFluidDestroy;
	PSDKFluidDesc **queuedFluids;

	Vec3 scenePos;
	Vec3 sceneBoxHalf;
} testPhysics;

struct {
	UIWindow *window;

#define FLUID_PARAM(var)				\
	UILabel *##var##Label;				\
	UISliderTextEntry *##var##Slider;	\
	F32	var

	FLUID_PARAM(emitterRate);
	FLUID_PARAM(maxParticles);
	FLUID_PARAM(fadeInTime);
	FLUID_PARAM(kernelRadiusMultiplier);
	FLUID_PARAM(motionLimit);
	FLUID_PARAM(restDensity);
	FLUID_PARAM(linearDensity);
	FLUID_PARAM(collDistMult);
	FLUID_PARAM(damping);
	FLUID_PARAM(viscosity);
	FLUID_PARAM(stiffness);
	FLUID_PARAM(restitution);
	FLUID_PARAM(dynamicFriction);
	FLUID_PARAM(surfaceTension);

	UILabel			*enableGravityLabel;
	UICheckButton	*enableGravityCheck;

#undef FLUID_PARAM

	UIButton *createFluid;
	UIButton *destroyFluid;

	UIButton *createScene;

	U32 dirty : 1;
	U32 needCreateScene : 1;
} fluidUI;

int wdDrawFluid;
AUTO_CMD_INT(wdDrawFluid, wdDrawFluid);

void makeInvertedBox(PSDKMeshDesc *meshDesc, Vec3 min, Vec3 max)
{
	int i, j, k;
	static Vec3 verts[8];
	static U32 tris[36] = {	0,3,2,
							0,1,3,
							0,2,6,
							0,6,4,
							1,5,7,
							1,7,3,
							0,5,1,
							0,4,5,
							2,7,6,
							2,3,7,
							4,7,5,
							4,6,7};		// Inverted box indices

	for(i=0; i<2; i++)
	{
		for(j=0; j<2; j++)
		{
			for(k=0; k<2; k++)
			{
				verts[i*4+j*2+k][0] = !k ? min[0] : max[0];
				verts[i*4+j*2+k][1] = !j ? min[1] : max[1];
				verts[i*4+j*2+k][2] = !i ? min[2] : max[2];
			}
		}
	}

	meshDesc->vertArray = verts;
	meshDesc->vertCount = ARRAY_SIZE(verts);
	meshDesc->triArray = tris;
	meshDesc->triCount = ARRAY_SIZE(tris)/3;
}

void gclWorldDebugIntegrationMsgHandler(const WorldCollIntegrationMsg* msg)
{
	switch(msg->msgType)
	{
		xcase WCI_MSG_FG_BEFORE_SIM_SLEEPS:
		{
		}

		xcase WCI_MSG_NOBG_WHILE_SIM_SLEEPS:
		{
			Vec3 box_min, box_max;
			Vec3 boxHalfSize = {30,30,30};

			if(!testPhysics.boxMeshDesc.vertArray)
			{
				scaleVec3(boxHalfSize, -1, box_min);
				makeInvertedBox(&testPhysics.boxMeshDesc, box_min, boxHalfSize);
				testPhysics.boxMeshDesc.no_thread = true;
			}

			if(fluidUI.needCreateScene)
			{
				PSDKActorDesc *actorDesc = NULL;
				PSDKCookedMesh *mesh = NULL;
				Mat4 boxMat;

				fluidUI.needCreateScene = 0;
				copyVec3(testPhysics.centerPos, testPhysics.scenePos);
				copyVec3(boxHalfSize, testPhysics.sceneBoxHalf);

				wcActorDestroy(msg, &testPhysics.wcActor);
				wcSceneDestroy(msg, &testPhysics.wcScene);

				wcSceneCreate(msg, &testPhysics.wcScene, 1, defaultGravity, "WorldDebug");

				wcSceneUpdateWorldCollObjectsBegin(msg, testPhysics.wcScene);

				wcSceneGatherWorldCollObjectsByRadius(	msg,
														testPhysics.wcScene,
														worldGetActiveColl(PARTITION_CLIENT),
														testPhysics.scenePos,
														500.0f);

				wcSceneUpdateWorldCollObjectsEnd(msg, testPhysics.wcScene);

				psdkCookedMeshCreate(&mesh, &testPhysics.boxMeshDesc);
				psdkActorDescCreate(&actorDesc);
				copyMat4(unitmat, boxMat);
				copyVec3(testPhysics.scenePos, boxMat[3]);
				psdkActorDescAddMesh(actorDesc, mesh, boxMat, 1, 0, WC_FILTER_BITS_WORLD_STANDARD, WC_SHAPEGROUP_WORLD_BASIC, 0);

				wcActorCreate(msg, testPhysics.wcScene, &testPhysics.wcActor, NULL, actorDesc, NULL, 0, 0, 0);

				psdkActorDescDestroy(&actorDesc);
			}

			if(eaSize(&testPhysics.queuedFluids))
			{
				FOR_EACH_IN_EARRAY(testPhysics.queuedFluids, PSDKFluidDesc, fluidDesc)
				{
					TestFluid *fluid = calloc(1, sizeof(TestFluid));

					fluid->particles = calloc(fluidDesc->maxParticles, sizeof(PSDKFluidParticle));
					fluid->numParticles = 0;

					fluidDesc->particleData = fluid->particles;
					fluidDesc->particleCount = &fluid->numParticles;

					if(wcFluidCreate(msg, testPhysics.wcScene, &fluid->wcFluid, NULL, fluidDesc))
					{
						eaPush(&testPhysics.testFluids, fluid);
					}
					else
					{
						free(fluid->particles);
						free(fluid);
					}
				}
				FOR_EACH_END

				eaClear(&testPhysics.queuedFluids);
			}

			if(eaSize(&testPhysics.queuedFluidDestroy))
			{
				FOR_EACH_IN_EARRAY(testPhysics.queuedFluidDestroy, TestFluid, fluid)
				{
					wcFluidDestroy(msg, &fluid->wcFluid);
					free(fluid->particles);
					free(fluid);
				}
				FOR_EACH_END

				eaClear(&testPhysics.queuedFluidDestroy);
			}


			FOR_EACH_IN_EARRAY(testPhysics.testFluids, TestFluid, fluid)
			{
				U32 i;
				Vec3 offset = {0.1, 0.1, 0.1};
				int color = fluid->color ? fluid->color : 0xffff0000;

				if(fluidUI.dirty)
				{
					fluidUI.dirty = 0;
					wcFluidSetMaxParticles(msg, fluid->wcFluid, fluidUI.maxParticles);
					wcFluidSetDamping(msg, fluid->wcFluid, fluidUI.damping);
					wcFluidSetStiffness(msg, fluid->wcFluid, fluidUI.stiffness);
					wcFluidSetViscosity(msg, fluid->wcFluid, fluidUI.viscosity);
				}

				if(wdDrawFluid)
				{
					for(i=0; i<fluid->numParticles; i++)
					{
						PSDKFluidParticle *particle = &fluid->particles[i];
						addVec3(particle->pos, offset, box_max);
						scaleAddVec3(offset, -1, particle->pos, box_min);

						gfxDrawBox3D(box_min, box_max, unitmat, ARGBToColor(color), 0);
					}
				}
			}
			FOR_EACH_END

			addVec3(testPhysics.scenePos, testPhysics.sceneBoxHalf, box_min);
			scaleAddVec3(testPhysics.sceneBoxHalf, -1, testPhysics.scenePos, box_max);
			gfxDrawBox3D(box_min, box_max, unitmat, ARGBToColor(0xff00ff00), 0);
		}

		xcase WCI_MSG_FG_AFTER_SIM_WAKES:
		{
		}

		xcase WCI_MSG_BG_BETWEEN_SIM:
		{
			
		}
	}
}

void gclFluidTestInit(void)
{
	if(!testPhysics.wci)
	{
		wcIntegrationCreate(&testPhysics.wci, gclWorldDebugIntegrationMsgHandler, NULL, "WorldDebug");
	}
}

static void wcFluidDebugSliderChanged(UISliderTextEntry *slider, bool bFinished, UserData unused)
{
#define SLIDER_TEST(var)													\
		fluidUI.##var = ui_SliderTextEntryGetValue(fluidUI.##var##Slider);	\
		fluidUI.dirty = true												
	SLIDER_TEST(damping);
	SLIDER_TEST(stiffness);
	SLIDER_TEST(viscosity);
	SLIDER_TEST(maxParticles);
	SLIDER_TEST(surfaceTension);
	SLIDER_TEST(fadeInTime);

#undef SLIDER_TEST
}

void wcFluidDebugUIDestroyFluid(UIAnyWidget *widget, UserData unused)
{
	eaPushEArray(&testPhysics.queuedFluidDestroy, &testPhysics.testFluids);

	eaClear(&testPhysics.testFluids);
}

void wcFluidDebugUICreateFluid(UIAnyWidget *widget, UserData unused)
{
	Entity *player = entActivePlayerPtr();
	PSDKFluidDesc *fluidDesc = NULL;
	PSDKFluidEmitterDesc *emitterDesc = NULL;

	gclFluidTestInit();

	if(!player)
		return;

	if(!psdkFluidDescCreate(&fluidDesc))
		return;

	if(!psdkFluidEmitterDescCreate(&emitterDesc))
	{
		//psdkFluidDescDestroy(&fluidDesc);
		return;
	}

	wcFluidDebugUIDestroyFluid(NULL, NULL);

	entGetPos(player, emitterDesc->center_pos);	emitterDesc->center_pos[1] += 5;
	emitterDesc->rate							= ui_SliderTextEntryGetValue(fluidUI.emitterRateSlider);
	eaPush(&fluidDesc->emitters, emitterDesc);

	fluidDesc->maxParticles						= ui_SliderTextEntryGetValue(fluidUI.maxParticlesSlider);
	fluidDesc->fadeInTime						= ui_SliderTextEntryGetValue(fluidUI.fadeInTimeSlider);
	fluidDesc->restDensity						= ui_SliderTextEntryGetValue(fluidUI.restDensitySlider);
	fluidDesc->motionLimitMultiplier			= ui_SliderTextEntryGetValue(fluidUI.motionLimitSlider);
	fluidDesc->collisionDistanceMultiplier		= ui_SliderTextEntryGetValue(fluidUI.collDistMultSlider);
	fluidDesc->restParticlesPerMeter			= ui_SliderTextEntryGetValue(fluidUI.linearDensitySlider);
	fluidDesc->kernelRadiusMultiplier			= ui_SliderTextEntryGetValue(fluidUI.kernelRadiusMultiplierSlider);
	fluidDesc->stiffness						= ui_SliderTextEntryGetValue(fluidUI.stiffnessSlider);
	fluidDesc->viscosity						= ui_SliderTextEntryGetValue(fluidUI.viscositySlider);
	fluidDesc->damping							= ui_SliderTextEntryGetValue(fluidUI.dampingSlider);
	fluidDesc->restitutionForStaticShapes		= ui_SliderTextEntryGetValue(fluidUI.restitutionSlider);
	fluidDesc->dynamicFrictionForStaticShapes	= ui_SliderTextEntryGetValue(fluidUI.dynamicFrictionSlider);
	fluidDesc->surfaceTension					= ui_SliderTextEntryGetValue(fluidUI.surfaceTensionSlider);
	fluidDesc->disableGravity					= ui_CheckButtonGetState(fluidUI.enableGravityCheck);

	eaPush(&testPhysics.queuedFluids, fluidDesc);
}

void wcFluidDebugUICreateScene(UIAnyWidget *widget, UserData unused)
{
	Entity *e = entActivePlayerPtr();

	if(!e)
		return;

	entGetPos(e, testPhysics.centerPos);
	fluidUI.needCreateScene = true;
}

bool wcFluidDebugUIClose(UIWindow *window, UserData unused)
{
	ZeroStruct(&fluidUI);

	return true;
}

AUTO_COMMAND;
void wcFluidDebugUI(void)
{
	gclFluidTestInit();

	if(!fluidUI.window)
	{
		F32 y;
		fluidUI.window = ui_WindowCreate("Fluid Params", 0, 0, 400, 600);
		ui_WindowSetCloseCallback(fluidUI.window, wcFluidDebugUIClose, NULL);

#define CREATE_SLIDER(name, var, min, max, def)														\
			fluidUI.##var##Label = ui_LabelCreate(name, 0, y);										\
			fluidUI.##var##Slider = ui_SliderTextEntryCreate(#def, min, max, 150, y, 200);			\
			ui_SliderTextEntrySetChangedCallback(fluidUI.##var##Slider, wcFluidDebugSliderChanged, NULL);	\
			ui_WindowAddChild(fluidUI.window, fluidUI.##var##Slider);								\
			ui_WindowAddChild(fluidUI.window, fluidUI.##var##Label);								\
			y += 20;

		y=0;
		CREATE_SLIDER("Emitter Rate", emitterRate, 1, 1000, 200);
		CREATE_SLIDER("Max Particles", maxParticles, 1000, 10000, 2000);
		CREATE_SLIDER("Fade In Time", fadeInTime, 0, 10, 5);
		CREATE_SLIDER("Rest Density", restDensity, 1, 10000, 10);
		CREATE_SLIDER("Linear Density", linearDensity, 0.01, 20, 0.25);
		CREATE_SLIDER("Motion Limit", motionLimit, 0.01, 10, 0.1);
		CREATE_SLIDER("Kernel Radius Mult", kernelRadiusMultiplier, 0.1, 10, 1);
		CREATE_SLIDER("Coll Dist Mult", collDistMult, 0, 30, 0.25);
		CREATE_SLIDER("Damping", damping, 0, 200, 30);
		CREATE_SLIDER("Viscosity", viscosity, 0, 200, 0.1);
		CREATE_SLIDER("Stiffness", stiffness, 1, 200, 10);
		CREATE_SLIDER("Restitution", restitution, 0, 1, 0);
		CREATE_SLIDER("Dynamic Friction", dynamicFriction, 0, 1, 0.1);
		CREATE_SLIDER("Surface Tension", surfaceTension, 0, 10, 0);

#undef CREATE_SLIDER

		fluidUI.enableGravityLabel = ui_LabelCreate("Enable Gravity", 0, y);
		fluidUI.enableGravityCheck = ui_CheckButtonCreate(150, y, "", false);
		ui_WindowAddChild(fluidUI.window, fluidUI.enableGravityLabel);
		ui_WindowAddChild(fluidUI.window, fluidUI.enableGravityCheck);
		y += 20;

		fluidUI.createFluid = ui_ButtonCreate("Create Fluid", 0, y, wcFluidDebugUICreateFluid, NULL);
		fluidUI.destroyFluid = ui_ButtonCreate("Destroy Fluid", 150, y, wcFluidDebugUIDestroyFluid, NULL);
		ui_WindowAddChild(fluidUI.window, fluidUI.createFluid);
		ui_WindowAddChild(fluidUI.window, fluidUI.destroyFluid);

		y += 20;
		fluidUI.createScene = ui_ButtonCreate("Create Scene", 0, y, wcFluidDebugUICreateScene, NULL);
		ui_WindowAddChild(fluidUI.window, fluidUI.createScene);

		ui_WindowShow(fluidUI.window);
	}
	else
	{
		ui_WindowClose(fluidUI.window);
	}
}
