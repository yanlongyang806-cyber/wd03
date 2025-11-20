GCC_SYSTEM
#pragma once

#include "UIGen.h"
#include "gclUIGen.h"

#define UI_GEN_COMPASS_PITCH_ANGLE RAD(55 / 2) // camera FOV / 2
#define UI_GEN_COMPASS_MIN_WAYPOINT_DISTANCE 2

AUTO_STRUCT;
typedef struct UIGenCompass
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeCompass))

	REF_TO(UIStyleFont) hTooltipFont; AST(NAME(TooltipFont))

	U32 uiTeammateColor; AST(DEFAULT(0xffffffff) NAME(TeammateColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiTeamLeaderColor; AST(DEFAULT(0xffffffff) NAME(TeamLeaderColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiContactColor; AST(DEFAULT(0xffffffff) NAME(ContactColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiMissionColor; AST(DEFAULT(0xffffffff) NAME(MissionColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiActiveMissionColor; AST(DEFAULT(0xffffffff) NAME(ActiveMissionColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiSavedWaypointColor; AST(DEFAULT(0xffffffff) NAME(SavedWaypointColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiFlaggedColor; AST(DEFAULT(0xffffffff) NAME(FlaggedEntityColor) FORMAT_COLOR SUBTABLE(ColorEnum))

	const char *pchCompassBackground; AST(POOL_STRING RESOURCEDICT(Texture))
	U32 uiCompassBackgroundColor; AST(DEFAULT(0xffffffff) NAME(CompassBackgroundColor) FORMAT_COLOR SUBTABLE(ColorEnum))

	const char *pchCompassNotch; AST(POOL_STRING RESOURCEDICT(Texture))
	U32 uiCompassNotchColor; AST(DEFAULT(0xffffffff) NAME(CompassNotchColor) FORMAT_COLOR SUBTABLE(ColorEnum))

	const char *pchTeammateIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchTeamLeaderIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchContactIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchMissionIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchActiveMissionIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchSavedWaypointIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchFlaggedIcon; AST(POOL_STRING RESOURCEDICT(Texture))

	const char *pchUpwardIcon; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchDownwardIcon; AST(POOL_STRING RESOURCEDICT(Texture))

	bool bShowClosestMissionIconOnly;

	// Always draw compass icons clamped to compass gen even when they are not in front of player
	bool bClampIcons;
} UIGenCompass;

AUTO_STRUCT;
typedef struct UIGenCompassIcon
{
	// Set during update
	const char *pchName; AST(UNOWNED)
	// If this icon has a special icon name, then pchName points to this string
	char* estrOwnedName; AST(ESTRING)
	
	AtlasTex *pIcon; NO_AST
	U32 uiColor;
	Vec3 v3Location;

	// Set during layout
	CBox ScreenBox; NO_AST

	bool bVisible;

	// Set during tick
	bool bHover;

	// Set during update
	bool bUpwards;
	bool bDownwards;
} UIGenCompassIcon;

AUTO_STRUCT;
typedef struct UIGenCompassFlagged
{
	EntityRef hEntity;
	Vec3 v3Location;
} UIGenCompassFlagged;

AUTO_STRUCT;
typedef struct UIGenCompassState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeCompass))
	UIGenCompassIcon **eaIcons;
	UIGenCompassFlagged **eaFlagged;

	S32 iOffset;

	AtlasTex *pCompassBackground; NO_AST
	AtlasTex *pCompassNotch; NO_AST

	AtlasTex *pForwardIcon; NO_AST
	AtlasTex *pUpwardIcon; NO_AST
	AtlasTex *pDownwardIcon; NO_AST
} UIGenCompassState;