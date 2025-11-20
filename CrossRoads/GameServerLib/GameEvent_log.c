/***************************************************************************
 *     Copyright (c) 2009-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#include "estring.h"
#include "timing.h"

#include "GameEvent.h"
#include "wlEncounter.h"

#include "GameEvent_h_ast.h"       // EventTypeEnum
#include "GlobalTypeEnum_h_ast.h"  // ContainerTypeEnum
#include "entEnums_h_ast.h"        // CritterRankEnum
#include "Encounter_enums_h_ast.h" // EncounterStateEnum
#include "itemEnums_h_ast.h"	   // ItemCategoriesEnum
#include "MinigameCommon_h_ast.h"  // MinigameTypeEnum
#include "mission_enums_h_ast.h"   // MissionStateEnum, MissionTypeEnum

// A bunch of macros to handle a bunch of boilerplate stuff below

// The EOL is added at when we append the next line.
#define EOL "\\r\\n"

// The logic for emitting floats is stolen from the textparser code for floats
#define EMIT_FLOAT(ff)	{float af = ABS(ff); if (af < 10000.f && af >= 1.f) { estrConcatf(ppch, "%.6f", ff); } else { estrConcatf(ppch, "%.6g", ff); }}

// Helpers for appending a field onto an estring.
#define FIELD(name) if(p->pch##name) { estrAppend2(ppch, EOL #name " "); estrAppendEscaped(ppch,p->pch##name); }
#define FIELD_SCOPED(fieldname) if(eaSize(&p->ea##fieldname##ScopeNames) > 0) { estrAppend2(ppch, EOL #fieldname " "); estrAppendEscaped(ppch, p->ea##fieldname##ScopeNames[0]->name); }
#define FIELD_QUOTED(name) if(p->pch##name) { char *pos=strchr(p->pch##name,' '); estrAppend2(ppch, EOL #name " "); if(pos) estrAppend2(ppch, "<&"); estrAppendEscaped(ppch,p->pch##name); if(pos) estrAppend2(ppch, "&>");}
#define FIELD_QUOTED_ALWAYS(name) if(p->pch##name) { estrAppend2(ppch, EOL #name " <&"); estrAppendEscaped(ppch,p->pch##name); estrAppend2(ppch, "&>");}
#define FIELD_INT(name, value) if(p->value) { estrAppend2(ppch, EOL #name " "); estrConcatf(ppch,"%d", p->value); }
#define FIELD_FLOAT(name, value) if(p->value) { estrAppend2(ppch, EOL #name " "); EMIT_FLOAT(p->value); }
#define FIELD_ENUM(name, value, type) if(p->value != -1) \
	{ \
		const char *pchTmp = StaticDefineIntRevLookup(type##Enum, p->value); \
		if(pchTmp) \
		{ \
			estrAppend2(ppch, EOL #name " "); \
			estrAppend2(ppch, pchTmp); \
		} \
	}
#define ARRAY_ENUM(name, list, type) if(p->list) \
	{ \
		int i; \
		estrAppend2(ppch, EOL #name " "); \
		for (i = 0; i < ea32Size((U32**)&p->list); i++) \
		{ \
			const char *pchTmp = StaticDefineIntRevLookup(type##Enum, p->list[i]); \
			if(pchTmp) \
			{ \
				estrAppend2(ppch, pchTmp); \
			} \
		} \
	}


#define FIELD_LOCAL(name, value) if(value) { estrAppend2(ppch, EOL #name " "); estrAppend2(ppch,value); }

// Helper for appending an arbitrary string to the estring.
#define APPEND(name) estrAppend2(ppch, EOL name);

static void AppendParticipantInfo(GameEventParticipant *p, char **ppch)
{
	if(p && ppch)
	{
		FIELD_QUOTED_ALWAYS(DebugName);
		FIELD_INT(ContainerID, iContainerID);
		FIELD_ENUM(ContainerType, eContainerType, GlobalType);

		if(p->bIsPlayer)
		{
			FIELD(AccountName);
			APPEND("IsPlayer 1");
		}
		else
		{
			FIELD(ActorName);
			FIELD(CritterName);
			FIELD(CritterGroupName);
			FIELD(Rank);
			FIELD_ENUM(RegionType, eRegionType, WorldRegionType);

			FIELD_SCOPED(StaticEnc);
			FIELD(EncounterName);
			FIELD_SCOPED(EncGroup);
		}
		FIELD(FactionName);

		FIELD_QUOTED_ALWAYS(LogString);

		FIELD(ObjectName);

		FIELD_INT(LevelCombat, iLevelCombat);
		FIELD_INT(LevelReal, iLevelReal);

		FIELD_INT(HasCredit, bHasCredit);
		FIELD_FLOAT(CreditPercentage, fCreditPercentage);
		FIELD_INT(HasTeamCredit, bHasTeamCredit);
		FIELD_FLOAT(TeamCreditPercentage, fTeamCreditPercentage);

		// FIELD(TeamID, teamID) no team identifier?
	}
}

static void AppendParticipants(GameEvent *ev, char **ppch)
{
	int i, cnt;

	cnt = eaSize(&ev->eaSources);
	for(i=0; i < cnt; i++)
	{
		APPEND("Sources" EOL "{");
		AppendParticipantInfo(ev->eaSources[i], ppch);
		APPEND("}");
	}

	cnt = eaSize(&ev->eaTargets);
	for(i=0; i < cnt; i++)
	{
		APPEND("Targets" EOL "{");
		AppendParticipantInfo(ev->eaTargets[i], ppch);
		APPEND("}");
	}
}

void gameevent_WriteEventEscapedFaster(GameEvent *p, char **ppch)
{
	const char *pchType;

	PERFINFO_AUTO_START_FUNC();

	// Doing this to match the old logger. Don't know if it is required, but the string needs to be cleared anyway.
	estrCopy2(ppch, " ");

	pchType = StaticDefineIntRevLookup(EventTypeEnum, p->type);
	estrAppend2(ppch, pchType);

	APPEND("{")

	FIELD(EventName);

	AppendParticipants(p, ppch);

	FIELD_INT(Count, count);
	FIELD_INT(Unique, bUnique);

	FIELD_QUOTED(StoreName);
	FIELD_QUOTED(ContactName);
	FIELD_QUOTED(MissionRefString);
	FIELD_QUOTED(ItemName);
	FIELD_SCOPED(Clickable);
	FIELD_SCOPED(ClickableGroup);
	FIELD_QUOTED(CutsceneName);
	FIELD_QUOTED(VideoName);
	FIELD_QUOTED(FSMName);
	FIELD_QUOTED(FsmStateName);
	FIELD_SCOPED(Volume);
	FIELD_QUOTED(PowerName);
	FIELD_QUOTED(PowerEventName);
	FIELD_QUOTED(DamageType);
	FIELD_QUOTED(DialogName);
	FIELD_QUOTED(NemesisName);
	FIELD_QUOTED(EmoteName);
	FIELD_QUOTED(ItemAssignmentName);
	FIELD_QUOTED(ItemAssignmentOutcome);
	FIELD_QUOTED(Message);
	FIELD_QUOTED(MapName);
	FIELD_QUOTED(GroupProjectName);

	FIELD_INT(IsRootMission, bIsRootMission);

	FIELD_ENUM(EncState, encState, EncounterState);
	FIELD_ENUM(MissionState, missionState, MissionState);
	FIELD_ENUM(MissionType, missionType, MissionType);
	FIELD_ENUM(MissionLockoutState, missionLockoutState,MissionLockoutState);
	FIELD_ENUM(NemesisState, nemesisState, NemesisState);
	FIELD_ENUM(HealthState, healthState, HealthState);
	FIELD_ENUM(MinigameType, eMinigameType, MinigameType);
	FIELD_ENUM(PvPQueueMatchResult, ePvPQueueMatchResult, PvPQueueMatchResult);
	FIELD_ENUM(PvPEvent, ePvPEvent, PvPEvent);
	ARRAY_ENUM(ItemCategory, eaItemCategories, ItemCategory);
	
	if(p->pos[0]!=0.0f || p->pos[1]!=0.0f || p->pos[2]!=0.0f)
	{
		APPEND("Pos ");
		EMIT_FLOAT(p->pos[0]);
		estrAppend2(ppch, ", ");
		EMIT_FLOAT(p->pos[1]);
		estrAppend2(ppch, ", ");
		EMIT_FLOAT(p->pos[2]);
	}


	FIELD_FLOAT(ChainTime, fChainTime);

	APPEND("}")

	PERFINFO_AUTO_STOP();
}

/* End of File */
