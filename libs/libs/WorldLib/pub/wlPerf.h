#pragma once

// This struct used to be part of FrameCounts in GraphicsLib.h.  However, GraphicsLib is not aware of some of these things.  It is easy to imagine wanting to track any
// number of things that are lower-level than GraphicsLib.  Possibly, the real solution is to move FrameCounts, possibly into UtilitiesLib, but the downside of that
// is that then UtilitiesLib would refer to things that are specific to higher level systems, so perhaps some sort of aggregating solution with the ultimate struct being
// in GameClientLib is actually the correct solution. [RMARR - 2/22/12]
// Note! time_misc contains essentially the entire frame (thought that is not guaranteed depending on GSM behavior) minus everything specifically tracked below,
// to identify otherwise untracked but significant CPU use.
// Note! Please update the handling of time subsets in gfxResetFrameCounters if you add time tracking subsets.
AUTO_STRUCT;
typedef struct WorldPerfFrameCounts
{
	S64 time_misc; // Everything not specifically tracked below, so miscellaneous.
	S64 time_draw;
	S64 time_queue;
	S64 time_queue_world;
	S64 time_anim; // cloth and skel are subsets of this time
	S64 time_wait_gpu;
	S64 time_net;
	S64 time_ui;
	S64 time_cloth;
	S64 time_skel;
	S64 time_fx;	// not including cloth
	S64 time_sound;
	S64 time_physics; // Separate from misc; tracks the physics thread.

	// Note: this array must match the layout of GfxGPUTimes and EGfxPerfCounter in RdrState.h.
	F32 time_gpu[10];

	S64 inside_brackets;
} WorldPerfFrameCounts;


extern ParseTable parse_WorldPerfFrameCounts[];
#define TYPE_parse_WorldPerfFrameCounts WorldPerfFrameCounts

typedef struct WorldPerfInfo
{
	WorldPerfFrameCounts * pCounts;

	bool in_time_misc;
	bool in_time_draw;
	bool in_time_queue;
	bool in_time_queue_world;
	bool in_time_anim;
	bool in_time_wait_gpu;
	bool in_time_net;
	bool in_time_ui;
	bool in_time_fx;
	bool in_time_sound;
	bool in_time_physics;
	int in_time_cloth;
	int in_time_skel;

	bool	assert_on_misnested_budgets;
} WorldPerfInfo;

extern WorldPerfInfo world_perf_info;

void wlPerfStartAnimBudget(void);
void wlPerfEndAnimBudget(void);
void wlPerfStartNetBudget(void);
void wlPerfEndNetBudget(void);
void wlPerfStartUIBudget(void);
void wlPerfEndUIBudget(void);
void wlPerfStartMiscBudget(void);
void wlPerfEndMiscBudget(void);
void wlPerfStartFXBudget(void);
void wlPerfEndFXBudget(void);
void wlPerfStartQueueBudget(void);
void wlPerfEndQueueBudget(void);
void wlPerfStartQueueWorldBudget(void);
void wlPerfEndQueueWorldBudget(void);
void wlPerfStartDrawBudget(void);
void wlPerfEndDrawBudget(void);
void wlPerfStartPhysicsBudget(S64 * time_physics_budget_start);
void wlPerfEndPhysicsBudget(S64 time_physics_budget_start);
void wlPerfStartSoundBudget(void);
void wlPerfEndSoundBudget(void);
void wlPerfStartWaitGPUBudget(void);
void wlPerfEndWaitGPUBudget(void);

void wlPerfStartClothBudget(WorldPerfFrameCounts * pTimeStamp);
void wlPerfEndClothBudget(WorldPerfFrameCounts * pTimeStamp);
void wlPerfStartSkelBudget(WorldPerfFrameCounts * pTimeStamp);
void wlPerfEndSkelBudget(WorldPerfFrameCounts * pTimeStamp);

void wlPerfSortOutAnimTime(void);

void wlPerfSetAssertOnMisnestedBudgets(bool bAssert);

S64 wlPerfGetPerfCyclesPerSecond();
F32 wlPerfGetMsPerTick();