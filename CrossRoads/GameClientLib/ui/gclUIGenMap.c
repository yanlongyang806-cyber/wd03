#include "gclUIGenMap.h"

#include "GlobalTypes.h"
#include "Character.h"
#include "Character_target.h"
#include "contact_common.h"
#include "EntityLib.h"
#include "EntityInteraction.h"
#include "EntityIterator.h"
#include "GameStringFormat.h"
#include "gclEntity.h"
#include "gclMapState.h"
#include "Guild.h"
#include "MapDescription.h"
#include "MapRevealCommon.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "Player.h"
#include "PvPGameCommon.h"
#include "RoomConn.h"
#include "StringUtil.h"
#include "UITextureAssembly.h"
#include "wlInteraction.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "inputMouse.h"
#include "inputLib.h"

#include "GfxCamera.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"
#include "GfxPrimitive.h"
#include "GfxMapSnap.h"

#include "MapSnap.h"
#include "CrypticDXT.h"
#include "MemoryPool.h"
#include "memref.h"
#include "StringCache.h"

#include "missionui_eval.h"

#include "GameClientLib.h"
#include "gclMapNotifications.h"
#include "mapstate_common.h"
#include "gclMapState.h"

#include "Player_h_ast.h"
#include "Character_target_h_ast.h"
#include "Powers_h_ast.h"
#include "mission_enums_h_ast.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "AutoGen/GameServerLib_AutoGen_ServerCmdWrappers.h"
#include "MapNotificationsCommon.h"
#include "AutoGen/MapNotificationsCommon_h_ast.h"

#include "gclUIGenMap_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define GenMapShouldUseSmoothShader() (gfxGetFeatures() & GFEATURE_POSTPROCESSING)

#define UI_GEN_MAP_MAX_HIGHRES 9

#define UI_GEN_MAP_SAVE_SCALE_TIMER (2.0f)

#define UI_GEN_MAP_ICON_INDEX_OFFSET 10

static const char *s_pchMapIcon;

static F32 s_fIconPushRadius = 50;
static F32 s_fIconPushForce = 35;
static F32 s_fPerceptionRadiusHack;
static bool s_bHideFogOfWar;
static bool s_bMapTexBoxDebug;
static bool s_bMapTexBoxLayered;
static bool s_bMapDrawLowRes = true;
static bool s_bMapDrawHighRes = true;
static int s_iMapMaxHighRes = UI_GEN_MAP_MAX_HIGHRES;
static bool s_bMapDrawMissionNumbers = true;
static bool s_bDrawMapFakeZoneHighlights = false;
static F32 s_fMapRevealLandmarkRadius = 80.0f;
static UIGenMapKeyIcon **s_eaMapKeyIcons = NULL; //Only touch this if you are dealing with a map with bShowMapKey set to true

static Mission **s_eaSortedMissions = NULL;

// extracted from the region
static F32 s_fRegionBaseGroundHeight;

// How close the mouse needs to be to map icons to push them around.
AUTO_CMD_FLOAT(s_fIconPushRadius, GenMapIconPushRadius);

// How far map icons should be pushable.
AUTO_CMD_FLOAT(s_fIconPushForce, GenMapIconPushForce);

// Debugging command to fake a minimap perception radius.
AUTO_CMD_FLOAT(s_fPerceptionRadiusHack, GenMapPerceptionRadiusOverride) ACMD_CATEGORY(Powers) ACMD_ACCESSLEVEL(9);

// Hide fog of war.
AUTO_CMD_INT(s_bHideFogOfWar, GenMapFogOfWarHide) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9);

// Hide fog of war. (Deprecated name.)
AUTO_CMD_INT(s_bHideFogOfWar, MapFogOfWarHide) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Draws boxes around all of the map textures drawn
AUTO_CMD_INT(s_bMapTexBoxDebug, GenMapTexBoxDebug) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Draws boxes around all of the map textures at map layer
AUTO_CMD_INT(s_bMapTexBoxLayered, GenMapTexBoxLayered) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Draw the low res map textures
AUTO_CMD_INT(s_bMapDrawLowRes, GenMapDrawLowRes) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Draw the high res map textures
AUTO_CMD_INT(s_bMapDrawHighRes, GenMapDrawHighRes) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Change the number of high res textures allowed at any time
AUTO_CMD_INT(s_iMapMaxHighRes, GenMapMaxHighRes) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Draw the mission numbers for map icons with a mission. The Map Gen's ShowMissionNumbers must also be True.
AUTO_CMD_INT(s_bMapDrawMissionNumbers, GenMapDrawMissionNumbers) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Draw all the highlights
AUTO_CMD_INT(s_bDrawMapFakeZoneHighlights, GenDrawMapFakeZoneHighlights) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

// Radius used when revealing landmark icons that are hidden unless revealed
AUTO_CMD_FLOAT(s_fMapRevealLandmarkRadius, GenMapRevealLandmarkRadius) ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(9) ACMD_HIDE;

#pragma warning(push)
#pragma warning(disable:4211) // nonstandard extension used : redefined extern to static
MP_DEFINE(UIGenMapEntityIconDef);
MP_DEFINE(UIGenMapNodeIconDef);
MP_DEFINE(UIGenMapWaypointIconDef);
MP_DEFINE(UIGenMapIcon);
#pragma warning(pop)

static UIGenMapFakeZoneHighlight *GetHighlight(UIGenMapFakeZone *pFakeZone, F32 *pX, F32 *pY, F32 *pRot);

static UIGenMapIcon *GenMapIconUnderMouse(CONST_EARRAY_OF(UIGenMapIcon) eaIcons);

void ui_GenMapSetScale(UIGen *pGen, F32 fScale, bool bSave)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	F32 fGenWidth = CBoxWidth(&pGen->ScreenBox);
	F32 fMapWidth = pState->pReveal->v3RegionMax[0] - pState->pReveal->v3RegionMin[0];

	if (pMap->eScaleMode == UIGenMapScaleWorldUnitsPerPixel)
		fScale = CLAMP(fScale, pMap->fScaleMin, pMap->fScaleMax);
	pState->fMapScale = fScale;
	pState->fPixelsPerWorldUnit = fMapWidth ? fGenWidth / (pState->fMapScale * fMapWidth) : 1.0f;

	if (bSave)
	{
		pState->fSavedPixelsPerWorldUnit = pState->fPixelsPerWorldUnit;
		pState->fSaveScaleTimer = pMap->bRememberScales ? UI_GEN_MAP_SAVE_SCALE_TIMER : -1;
	}
	else if (pState->fSavedPixelsPerWorldUnit <= 0)
	{
		pState->fSavedPixelsPerWorldUnit = pState->fPixelsPerWorldUnit;
	}
}

void ui_GenMapSetPixelsPerWorldUnit(UIGen *pGen, F32 fPixelPerWorldUnit, bool bSave)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	F32 fGenWidth = CBoxWidth(&pGen->ScreenBox);
	F32 fMapWidth = pState->pReveal->v3RegionMax[0] - pState->pReveal->v3RegionMin[0];
	F32 fScale;
	if (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit)
		fPixelPerWorldUnit = CLAMP(fPixelPerWorldUnit, pMap->fScaleMin, pMap->fScaleMax);
	fScale = fMapWidth ? fGenWidth / (fPixelPerWorldUnit * fMapWidth) : 1.0f;
	ui_GenMapSetScale(pGen, fScale, bSave);
}

static AtlasTex *GenMapIconLoadTexture(UIGenMapIcon *pIcon)
{
	UIGenMapIconDef *pDef = pIcon->pDef;
	UI_GEN_LOAD_TEXTURE(pDef->pchIcon, pDef->pTexture);
	if (!pIcon->pDef->pchFormatIcon)
	{
		AtlasTex *pIconTex = NULL;
		// Use the per-icon override if set and preesnt, otherwise use the one from the def.
		if (pIcon->pchTexture)
			pIconTex = atlasLoadTexture(pIcon->pchTexture);
		if ((!pIconTex || pIconTex == white_tex_atlas)
			&& pDef->pTexture
			&& pDef->pTexture != white_tex_atlas)
			return pDef->pTexture;
		else
			return pIconTex;
	}
	else
	{
		AtlasTex *pTex;
		static char *s_pch;
		const char *pchTexture = pIcon->pchTexture;
		char *pchFixedTexture = NULL;
		S32 iFrame;
		estrClear(&s_pch);

		// This probably isn't the best hack in the world.
		// But if the texture name ends with .Wtex, remove the .Wtex
		if (pIcon->pchTexture && strEndsWith(pIcon->pchTexture, ".Wtex"))
		{
			pchTexture = pIcon->pchTexture;
			strdup_alloca(pchFixedTexture, pIcon->pchTexture);
			pIcon->pchTexture = pchFixedTexture;
			pchFixedTexture = strstri(pchFixedTexture, ".Wtex");
			if (pchFixedTexture)
				*pchFixedTexture = '\0';
		}

		if (pDef->fFrameDuration > 0 && pDef->iFrameCount > 1)
		{
			iFrame = ((int)((g_ui_State.totalTimeInMs / 1000.0) / pDef->fFrameDuration) % pDef->iFrameCount) + 1;
		}
		else
		{
			iFrame = 1;
		}

		FormatGameString(&s_pch, 
			pDef->pchFormatIcon, 
			STRFMT_STRUCT(s_pchMapIcon, pIcon, parse_UIGenMapIcon),
			STRFMT_INT("Frame", iFrame),
			STRFMT_END);

		// Undo the hack.
		pIcon->pchTexture = pchTexture;

		// Use the formatted name of it exists.
		if (s_pch && *s_pch && (pTex = atlasLoadTexture(s_pch)) != white_tex_atlas)
			return pTex;
		// Otherwise try the unformatted texture override.
		else if (pIcon->pchTexture && (pTex = atlasLoadTexture(pIcon->pchTexture)) != white_tex_atlas)
			return pTex;
		// Otherwise try the static one from the def.
		else
			return pIcon->pDef->pTexture;
	}
}

static void GenMapMapPosToScreenPos(UIGen *pGen, const Vec2 v2MapPos, Vec2 v2ScreenPos)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	MapRevealInfo *pReveal = pState->pReveal;
	Vec3 vWorldCenterAtFocusHeight;
	F32 fScreenLeft,fScreenTop;

	copyVec3(pState->v3WorldCenter,vWorldCenterAtFocusHeight);
	
	vWorldCenterAtFocusHeight[0] += gfCurrentMapOrthoSkewX*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);
	vWorldCenterAtFocusHeight[2] += gfCurrentMapOrthoSkewZ*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);

	fScreenLeft = (pGen->ScreenBox.hx + pGen->ScreenBox.lx) / 2 - (vWorldCenterAtFocusHeight[0] - pReveal->v3RegionMin[0]) * pState->fPixelsPerWorldUnit;
	fScreenTop = (pGen->ScreenBox.hy + pGen->ScreenBox.ly) / 2 - (pReveal->v3RegionMax[2] - vWorldCenterAtFocusHeight[2]) * pState->fPixelsPerWorldUnit;

	v2ScreenPos[0] = fScreenLeft + v2MapPos[0]* pState->fPixelsPerWorldUnit;
	v2ScreenPos[1] = fScreenTop + v2MapPos[1] * pState->fPixelsPerWorldUnit;
}

// the extra parameter here is for supporting the ortho skew I put in for NW.  If fMapSnapOrthoSkewX and fMapSnapOrthoSkewZ are 0, it should do nothing
static void GenMapWorldPosToScreenPos(UIGen *pGen, const Vec3 v3WorldPos, Vec2 v2ScreenPos,bool bAdjustPos)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	MapRevealInfo *pReveal = pState->pReveal;
	Vec3 vWorldCenterAtFocusHeight;
	F32 fWorldDistanceX,fWorldDistanceZ;
	F32 fScreenLeft,fScreenTop;

	copyVec3(pState->v3WorldCenter,vWorldCenterAtFocusHeight);
	
	vWorldCenterAtFocusHeight[0] += gfCurrentMapOrthoSkewX*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);
	vWorldCenterAtFocusHeight[2] += gfCurrentMapOrthoSkewZ*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);

	fWorldDistanceX = v3WorldPos[0] - pReveal->v3RegionMin[0];
	fWorldDistanceZ = pReveal->v3RegionMax[2] - v3WorldPos[2];
	fScreenLeft = (pGen->ScreenBox.hx + pGen->ScreenBox.lx) / 2 - (vWorldCenterAtFocusHeight[0] - pReveal->v3RegionMin[0]) * pState->fPixelsPerWorldUnit;
	fScreenTop = (pGen->ScreenBox.hy + pGen->ScreenBox.ly) / 2 - (pReveal->v3RegionMax[2] - vWorldCenterAtFocusHeight[2]) * pState->fPixelsPerWorldUnit;

	v2ScreenPos[0] = fScreenLeft + fWorldDistanceX * pState->fPixelsPerWorldUnit;
	v2ScreenPos[1] = fScreenTop + fWorldDistanceZ * pState->fPixelsPerWorldUnit;

	if (bAdjustPos)
	{
		v2ScreenPos[0] += pState->fPixelsPerWorldUnit*gfCurrentMapOrthoSkewX*(v3WorldPos[1]-s_fRegionBaseGroundHeight);
		v2ScreenPos[1] -= pState->fPixelsPerWorldUnit*gfCurrentMapOrthoSkewZ*(v3WorldPos[1]-s_fRegionBaseGroundHeight);
	}
}

static void GenMapScreenPosToWorldPos(UIGen *pGen, const Vec2 v2ScreenPos, Vec3 v3WorldPos)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	MapRevealInfo *pReveal = pState->pReveal;
	Vec3 vWorldCenterAtFocusHeight;
	F32 fScreenLeft,fScreenTop;
	F32 fScreenDistanceX,fScreenDistanceY;
	F32 fPixelsPerWorldUnit = (pState->fPixelsPerWorldUnit != 0 ? pState->fPixelsPerWorldUnit : .1);

	copyVec3(pState->v3WorldCenter,vWorldCenterAtFocusHeight);
	
	vWorldCenterAtFocusHeight[0] += gfCurrentMapOrthoSkewX*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);
	vWorldCenterAtFocusHeight[2] += gfCurrentMapOrthoSkewZ*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);

	fScreenLeft = (pGen->ScreenBox.hx + pGen->ScreenBox.lx) / 2 - (vWorldCenterAtFocusHeight[0] - pReveal->v3RegionMin[0]) * fPixelsPerWorldUnit;
	fScreenTop = (pGen->ScreenBox.hy + pGen->ScreenBox.ly) / 2 - (pReveal->v3RegionMax[2] - vWorldCenterAtFocusHeight[2]) * fPixelsPerWorldUnit;
	fScreenDistanceX = v2ScreenPos[0] - fScreenLeft;
	fScreenDistanceY = v2ScreenPos[1] - fScreenTop;

	v3WorldPos[0] = pReveal->v3RegionMin[0] + fScreenDistanceX / fPixelsPerWorldUnit;
	v3WorldPos[1] = s_fRegionBaseGroundHeight;
	v3WorldPos[2] = pReveal->v3RegionMax[2] - fScreenDistanceY / fPixelsPerWorldUnit;
}

static void GenMapWorldBoxToScreenBox(UIGen *pGen, Vec3 v3WorldMin, Vec3 v3WorldMax, CBox *pBox)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	Vec2 v2ScreenMin;
	Vec2 v2ScreenMax;
	GenMapWorldPosToScreenPos(pGen, v3WorldMin, v2ScreenMin, true);
	GenMapWorldPosToScreenPos(pGen, v3WorldMax, v2ScreenMax, true);

	pBox->lx = min(v2ScreenMin[0], v2ScreenMax[0]);
	pBox->hx = max(v2ScreenMin[0], v2ScreenMax[0]);
	pBox->ly = min(v2ScreenMin[1], v2ScreenMax[1]);
	pBox->hy = max(v2ScreenMin[1], v2ScreenMax[1]);
}

static bool GenMapCheckOutOfBounds(Vec2 v2IconCenter, const CBox *pClampBox, F32 fRadius, const Vec2 v2ClampRadiusCenter)
{
	if ((v2IconCenter[0] < pClampBox->lx) || (v2IconCenter[0] > pClampBox->hx) || (v2IconCenter[1] < pClampBox->ly) || (v2IconCenter[1] > pClampBox->hy))
	{
		return true;
	}
	else if (fRadius != 0)
	{
		Vec2 vR = { v2IconCenter[0] - v2ClampRadiusCenter[0], v2IconCenter[1] - v2ClampRadiusCenter[1] };
		return lengthVec2Squared(vR) > SQR(fRadius);
	}
	return false;
}

static void GenMapIconRunAction(UIGen *pMap, UIGenMapIcon *pIcon, UIGenAction *pAction)
{
	ui_GenSetPointerVar(s_pchMapIcon, pIcon, parse_UIGenMapIcon);
	ui_GenRunAction(pMap, pAction);
	ui_GenSetPointerVar(s_pchMapIcon, NULL, parse_UIGenMapIcon);
}

static ContactInfo *GenMapContactForEntity(Entity *pPlayerEnt, EntityRef hTarget)
{
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	S32 i;

	if (!pInteractInfo)
		return NULL;

	for (i = 0; i < eaSize(&pInteractInfo->nearbyContacts); i++)
	{
		ContactInfo *pContact = pInteractInfo->nearbyContacts[i];
		if (pContact->entRef == hTarget)
			return pContact;

	}
	return NULL;
}

static UIGenMapEntityIconDef *GenMapMatchEntity(UIGen *pGen, CONST_EARRAY_OF(UIGenMapEntityIconDef) eaEntityIcons, Entity *pPlayer, Entity *pTarget, const CBox *pClampBox, float fClampRadius, Vec2 v2RadiusCenter)
{
	ContactInfo *pContact = NULL;
	TargetType eTargetType = 0;
	EntityRef hTarget;
	S32 i;
	Vec3 v3TargetPos;
	Vec2 v2ScreenPos;
	entGetPos(pTarget, v3TargetPos);
	GenMapWorldPosToScreenPos(pGen, v3TargetPos, v2ScreenPos, false);

	if (pTarget == pPlayer || !pPlayer || !pTarget)
		return NULL;

	hTarget = entGetRef(pTarget);

	pContact = GenMapContactForEntity(pPlayer, hTarget);

	for (i = eaSize(&eaEntityIcons) - 1; i >= 0; i--)
	{
		UIGenMapEntityIconDef *pDef = eaEntityIcons[i];

		if (pDef->Def.bOutOfBounds && !GenMapCheckOutOfBounds(v2ScreenPos, pClampBox, fClampRadius, v2RadiusCenter))
			continue;

		if (pDef->eTargetType)
		{
			if (!pTarget->pChar)
				continue;
			if (!eTargetType)
				eTargetType = character_MakeTargetType(PARTITION_CLIENT, pPlayer->pChar, pTarget->pChar);
			if ((eTargetType & pDef->eTargetType) != pDef->eTargetType)
				continue;
		}

		if (eaiSize(&pDef->eaiNotificationTypes) > 0)
		{
			bool bFoundNotification = false;
			S32 j;
			for (j = 0; j < eaiSize(&pDef->eaiNotificationTypes); j++)
			{
				if (gclMapNotifications_EntityHasNotification(hTarget, pDef->eaiNotificationTypes[j]))
				{
					bFoundNotification = true;
					break;
				}
			}

			if (!bFoundNotification)
			{
				continue;
			}
		}

		if (pDef->bSavedEntity && !pTarget->pSaved)
			continue;
		if (pDef->bGuildmate && !guild_InSameGuild(pPlayer, pTarget))
			continue;
		if (pDef->bPlayerPet && (!pTarget->erOwner || pTarget->erOwner != entGetRef(pPlayer)))
			continue;

		if (pDef->eContactFlags || pDef->eContactIndicator)
		{
			if (!pContact)
				continue;
			if ((pDef->eContactFlags & pContact->eFlags) != pDef->eContactFlags)
				continue;
			if (pDef->eContactIndicator && pDef->eContactIndicator != pContact->currIndicator)
				continue;
		}

		if (pDef->bMustPerceive)
		{
			F32 fPerception = pPlayer->pChar->pattrBasic->fMinimap;
			Vec3 v3PlayerPos;
			MAX1(fPerception, s_fPerceptionRadiusHack);
			entGetPos(pPlayer, v3PlayerPos);
			if (distance3Squared(v3PlayerPos, v3TargetPos) > SQR(fPerception))
				continue;
		}

		if (pDef->bEscort)
		{
			MissionInfo *pMissionInfo = SAFE_MEMBER2(pPlayer, pPlayer, missionInfo);
			if (!pMissionInfo || eaiFind(&pMissionInfo->eaiEscorting, hTarget) < 0)
				continue;
		}

		if (pDef->Def.iMaxCount && pDef->Def.iCount >= pDef->Def.iMaxCount)
			return NULL;

		pDef->Def.iCount++;
		return pDef;
	}
	return NULL;
}


static void GenMapIconForEntity(Entity *pPlayer, Entity *pEntity, UIGenMapIconDef *pIconDef, UIGenMapIcon *pIcon, Vec3 v3Center)
{
	Vec3 pyr;
	pIcon->pDef = pIconDef;
	pIcon->eLabelAlignment = pIconDef->eLabelAlignment;
	entGetPosClampedToRegion(pPlayer, pEntity, pIcon->v3WorldPos);

	if (pIconDef->bOutOfBounds)
	{
		pIcon->fRotation = atan2f(pIcon->v3WorldPos[0] - v3Center[0], pIcon->v3WorldPos[2] - v3Center[2]);
	}
	else
	{
		entGetFacePY(pEntity, pyr);
		pIcon->fRotation = pyr[1];
	}
	zeroVec3(pIcon->v3WorldSize);
	pIcon->hEntity = entGetRef(pEntity);
	pIcon->pchMission = NULL;
	pIcon->pchNode = NULL;
	pIcon->pchTexture = NULL;
	pIcon->bHideUnlessRevealed = false;
	estrCopy2(&pIcon->pchLabel, entGetLocalName(pEntity));
}

static UIGenMapNodeIconDef *GenMapMatchNode(UIGen *pGen, CONST_EARRAY_OF(UIGenMapNodeIconDef) eaNodeIcons, TargetableNode *pTarget, Entity *pPlayer, const CBox *pClampBox, float fClampRadius, Vec2 v2RadiusCenter)
{
	WorldInteractionNode *pNode = pTarget ? GET_REF(pTarget->hNode) : NULL;
	TargetType eTargetType = 0;
	Vec3 v3PlayerPos;
	S32 i;
	Vec2 v2ScreenPos;

	if (!pPlayer || !pNode)
		return NULL;

	eTargetType = character_MakeTargetTypeNode(pPlayer->pChar);
	entGetPos(pPlayer, v3PlayerPos);
	GenMapWorldPosToScreenPos(pGen, v3PlayerPos, v2ScreenPos, false);

	for (i = eaSize(&eaNodeIcons) - 1; i >= 0; i--)
	{
		UIGenMapNodeIconDef *pDef = eaNodeIcons[i];
		Vec3 v3Pos;
		F32 fRadius = wlInteractionNodeGetSphereBounds(pNode, v3Pos);
		F32 fDistance = distance3Squared(v3PlayerPos, v3Pos);
		S32 j;
		bool bMatched = true;

		if (pDef->Def.bOutOfBounds && !GenMapCheckOutOfBounds(v2ScreenPos, pClampBox, fClampRadius, v2RadiusCenter))
			continue;

		if (pDef->fInteractDistance && fDistance > SQR(pDef->fInteractDistance + fRadius))
			continue;

		if (pDef->eTargetType)
		{
			if ((eTargetType & pDef->eTargetType) != pDef->eTargetType)
				continue;
		}

		if (pDef->bMustPerceive)
		{
			F32 fPerception = pPlayer->pChar->pattrBasic->fMinimap;
			if (fDistance > SQR(fPerception + fRadius))
				continue;
		}

		for (j = 0; j < eaSize(&pDef->eaCategory) && bMatched; j++)
			bMatched = eaFind(&pTarget->eaCategories, pDef->eaCategory[j]) >= 0;
		if (!bMatched)
			continue;

		for (j = 0; j < eaSize(&pDef->eaTag) && bMatched; j++)
			bMatched = eaFind(&pTarget->eaTags, pDef->eaTag[j]) >= 0;
		if (!bMatched)
			continue;

		if (pDef->Def.iMaxCount && pDef->Def.iCount >= pDef->Def.iMaxCount)
			return NULL;

		pDef->Def.iCount++;
		return pDef;
	}

	return NULL;
}

static void GenMapIconForNode(TargetableNode *pTarget, UIGenMapIconDef *pIconDef, UIGenMapIcon *pIcon, Vec3 v3Center)
{
	const char* pchNodeDisplayName;
	WorldInteractionNode *pNode = pTarget ? GET_REF(pTarget->hNode) : NULL;
	F32 fRadius;
	assert(pNode);
	pIcon->pDef = pIconDef;
	pIcon->eLabelAlignment = pIconDef->eLabelAlignment;
	fRadius = wlInteractionNodeGetSphereBounds(pNode, pIcon->v3WorldPos);
	setVec3(pIcon->v3WorldSize, fRadius, fRadius, fRadius);
	if (pIconDef->bOutOfBounds)
	{
		pIcon->fRotation = atan2f(pIcon->v3WorldPos[0] - v3Center[0], pIcon->v3WorldPos[2] - v3Center[2]);
	}
	else
	{
		pIcon->fRotation = 0;
	}
	pIcon->pchNode = REF_STRING_FROM_HANDLE(pTarget->hNode);
	pIcon->pchTexture = NULL;
	pIcon->hEntity = 0;
	pIcon->bHideUnlessRevealed = false;
	if (pchNodeDisplayName = wlInteractionNodeGetDisplayName(pNode))
	{
		estrCopy2(&pIcon->pchLabel, pchNodeDisplayName);
	}
	else
	{
		estrClear(&pIcon->pchLabel);
	}
}


//Returns true if the passed waypoint is an area waypoint and the passed point falls inside of it
static bool isPointInArea(Vec3 v3Point, MinimapWaypoint *pWaypoint)
{
	if (pWaypoint && pWaypoint->fXAxisRadius > 0 && pWaypoint->fYAxisRadius > 0)
	{
		Vec3 v3Temp, v3Temp2;
		Vec3 upVec3 = {0, 1, 0};
		F32 fDist = 10;

		copyVec3(v3Point, v3Temp);

		v3Temp[0] -= pWaypoint->pos[0];
		v3Temp[1] = 0;
		v3Temp[2] -= pWaypoint->pos[2];

		rotateVecAboutAxis(pWaypoint->fRotation, upVec3, v3Temp, v3Temp2);

		v3Temp2[0] /= pWaypoint->fXAxisRadius;
		v3Temp2[2] /= pWaypoint->fYAxisRadius;

		fDist = distance3XZSquared(v3Temp2, zerovec3);

		if (fDist < 1 && FINITE(fDist))
		{
			return true;
		}
	}

	return false;
}

static F32 getDistSquaredLineSegPoint2d(Vec2 p, Vec2 a, Vec2 b)
{
	Vec2 ab, pa, d;
	F32 t;
	F32 ab2;
	subVec2(b, a, ab);
	subVec2(p, a, pa);
	ab2 = dotVec2(ab, ab);

	if (ab2 > 0.0f) {
		t = dotVec2(pa, ab) / ab2;
		MINMAX1(t, 0.0f, 1.0f);
		scaleAddVec2(ab, t, a, d);
	} else {
		t = 0.0f;
		copyVec2(a, d);
	}

	return distance2Squared(p, d);
}

static bool isAreaWaypointOnScreen(MinimapWaypoint *pWaypoint, UIGen *pGen, Vec2 v2ScreenCenter, F32 fRadius, const CBox *pClampBox)
{
	Vec3 v3WorldCenter;
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	F32 fDist = 0;

	if (pWaypoint->fXAxisRadius <= 0 || pWaypoint->fYAxisRadius <= 0)
		return false;

	if (fRadius == 0)
	{
		Vec2 v2WaypointPos;
		GenMapWorldPosToScreenPos(pGen, pWaypoint->pos, v2WaypointPos, false);

		//Check if the center of the waypoint is on screen
		if (v2WaypointPos[0] >= pClampBox->lx && v2WaypointPos[0] <= pClampBox->hx &&
			v2WaypointPos[1] >= pClampBox->ly && v2WaypointPos[1] <= pClampBox->hy)
		{
			return true;
		}
		else //Transform the box so that the area waypoint is a unit circle centered at the origin
		{
			F32 lx = pClampBox->lx - v2WaypointPos[0];
			F32 ly = pClampBox->ly - v2WaypointPos[1];
			F32 hx = pClampBox->hx - v2WaypointPos[0];
			F32 hy = pClampBox->hy - v2WaypointPos[1];
			Vec2 v2A, v2B, v2C, v2D, v2Origin;

			zeroVec2(v2Origin);

			v2A[0] = lx;
			v2A[1] = ly;
			v2B[0] = hx;
			v2B[1] = ly;
			v2C[0] = hx;
			v2C[1] = hy;
			v2D[0] = lx;
			v2D[1] = hy;

			rotateXZ(pWaypoint->fRotation, &v2A[0], &v2A[1]);
			rotateXZ(pWaypoint->fRotation, &v2B[0], &v2B[1]);
			rotateXZ(pWaypoint->fRotation, &v2C[0], &v2C[1]);
			rotateXZ(pWaypoint->fRotation, &v2D[0], &v2D[1]);

			v2A[0] /= pWaypoint->fXAxisRadius * pState->fPixelsPerWorldUnit;
			v2B[0] /= pWaypoint->fXAxisRadius * pState->fPixelsPerWorldUnit;
			v2C[0] /= pWaypoint->fXAxisRadius * pState->fPixelsPerWorldUnit;
			v2D[0] /= pWaypoint->fXAxisRadius * pState->fPixelsPerWorldUnit;

			v2A[1] /= pWaypoint->fYAxisRadius * pState->fPixelsPerWorldUnit;
			v2B[1] /= pWaypoint->fYAxisRadius * pState->fPixelsPerWorldUnit;
			v2C[1] /= pWaypoint->fYAxisRadius * pState->fPixelsPerWorldUnit;
			v2D[1] /= pWaypoint->fYAxisRadius * pState->fPixelsPerWorldUnit;


			//Now check each edge of the transformed box to see if it goes within 1 unit of the origin
			if (getDistSquaredLineSegPoint2d(v2Origin, v2A, v2B) <= 1)
			{
				return true;
			}
			else if (getDistSquaredLineSegPoint2d(v2Origin, v2B, v2C) <= 1)
			{
				return true;
			}
			else if (getDistSquaredLineSegPoint2d(v2Origin, v2C, v2D) <= 1)
			{
				return true;
			}
			else if (getDistSquaredLineSegPoint2d(v2Origin, v2D, v2A) <= 1)
			{
				return true;
			}
			else
			{
				return false;
			}
		}
	}
	else
	{
		GenMapScreenPosToWorldPos(pGen, v2ScreenCenter, v3WorldCenter);

		fDist = distance3XZ(v3WorldCenter,pWaypoint->pos);

		if (fDist > fRadius / (pState->fPixelsPerWorldUnit ? pState->fPixelsPerWorldUnit : .1))
		{
			Vec3 v3OuterPoint;

			subVec3XZ(pWaypoint->pos, v3WorldCenter, v3OuterPoint);

			v3OuterPoint[1] = 0;

			normalizeCopyVec3(v3OuterPoint, v3OuterPoint);

			scaleVec3XZ(v3OuterPoint, fRadius / (pState->fPixelsPerWorldUnit ? pState->fPixelsPerWorldUnit : .1), v3OuterPoint);

			addVec3XZ(v3OuterPoint, v3WorldCenter, v3OuterPoint);

			return isPointInArea(v3OuterPoint, pWaypoint);
		}
	}

	return true;
}

static UIGenMapWaypointIconDef *GenMapMatchWaypoint(UIGen *pGen, UIGenMapState *pState, CONST_EARRAY_OF(UIGenMapWaypointIconDef) eaWaypointIcons, MinimapWaypoint *pWaypoint, const CBox *pClampBox, float fClampRadius, Vec2 v2RadiusCenter, bool bCheckOutOfBounds)
{
	S32 i;
	Vec2 v2ScreenPos;
	GenMapWorldPosToScreenPos(pGen, pWaypoint->pos, v2ScreenPos, false);
	for (i = eaSize(&eaWaypointIcons) - 1; i >= 0; i--)
	{
		UIGenMapWaypointIconDef *pDef = eaWaypointIcons[i];
		bool bAreaOnScreen = isAreaWaypointOnScreen(pWaypoint, pGen, v2RadiusCenter, fClampRadius, pClampBox);

		if (pDef->Def.bOutOfBounds && (!(bCheckOutOfBounds && !bAreaOnScreen) || !GenMapCheckOutOfBounds(v2ScreenPos, pClampBox, fClampRadius, v2RadiusCenter)))
			continue;
		if (pDef->eType != MinimapWaypointType_None && pWaypoint->type != pDef->eType)
			continue;
		if (pDef->bAreaWaypoint && pWaypoint->fXAxisRadius == 0 && pWaypoint->fYAxisRadius == 0)
			continue;
		if (eaSize(&pDef->eaContact) && eaFind(&pDef->eaContact, pWaypoint->pchContactName) < 0)
			continue;
		if (eaSize(&pDef->eaMission) && eaFind(&pDef->eaMission, pWaypoint->pchMissionRefString) < 0)
			continue;
		if (pDef->bPrimary && pWaypoint->pchMissionRefString)
		{
			// Slightly more complicated comparison than just a straight string compare
			const char* pchPrimaryMission = entGetPrimaryMission(entActivePlayerPtr());
			const char* pchColon = strchr(pWaypoint->pchMissionRefString, ':'); // Submissions have mission::submission syntax. Only need mission
			S32 iLen = pchColon ? (pchColon - pWaypoint->pchMissionRefString) : (S32)strlen(pWaypoint->pchMissionRefString);
			if (!pchPrimaryMission || strnicmp(pWaypoint->pchMissionRefString, pchPrimaryMission, iLen) != 0)
				continue;
		}
		if (pDef->bSelected && pWaypoint->pchMissionRefString != pState->pchSelectedMissionRefString)
			continue;

		if (pDef->Def.iMaxCount && pDef->Def.iCount >= pDef->Def.iMaxCount)
			return NULL;

		pDef->Def.iCount++;
		return pDef;
	}
	return NULL;
}

void GenMapIconGetMissionLabel(MinimapWaypoint *pWaypoint, char **ppchLabel)
{
	MissionDef *pDef = missiondef_DefFromRefString(pWaypoint->pchMissionRefString);
	while (pDef && GET_REF(pDef->parentDef))
	{
		pDef = GET_REF(pDef->parentDef);
	}
	estrClear(ppchLabel);
	if (pDef)
	{
		const char* pchMissionDisplayName = TranslateMessageRefDefault(pDef->displayNameMsg.hMessage, pWaypoint->pchDescription);
		StringStripTagsPrettyPrint(pchMissionDisplayName, ppchLabel);
	}
}

static void GenMapIconForWaypoint(MinimapWaypoint *pWaypoint, UIGenMapIconDef *pIconDef, UIGenMapIcon *pIcon, Vec3 v3Center)
{
	pIcon->pchNode = NULL;
	pIcon->pchMission = pWaypoint->pchMissionRefString;
	pIcon->pDef = pIconDef;
	pIcon->eLabelAlignment = pIconDef->eLabelAlignment;
	pIcon->pchTexture = allocAddString(pWaypoint->pchIconTexName);
	copyVec3(pWaypoint->pos, pIcon->v3WorldPos);
	if (pIconDef->bOutOfBounds)
	{
		zeroVec3(pIcon->v3WorldSize);
	}
	else
	{
		pIcon->v3WorldSize[0] = 2 * pWaypoint->fXAxisRadius;
		pIcon->v3WorldSize[1] = 0;
		pIcon->v3WorldSize[2] = 2 * pWaypoint->fYAxisRadius;
	}

	if (pIconDef->bOutOfBounds)
	{
		pIcon->fRotation = atan2f(pWaypoint->pos[0] - v3Center[0], pWaypoint->pos[2] - v3Center[2]);
		zeroVec3(pIcon->v3WorldSize);
	}
	else
	{
		pIcon->fRotation = -pWaypoint->fRotation;
		pIcon->v3WorldSize[0] = 2 * pWaypoint->fXAxisRadius;
		pIcon->v3WorldSize[1] = 0;
		pIcon->v3WorldSize[2] = 2 * pWaypoint->fYAxisRadius;
	}
	pIcon->bHideUnlessRevealed = pWaypoint->bHideUnlessRevealed;
	if (GET_REF(pWaypoint->hDisplayNameMsg))
	{
		const char *pchLabel = TranslateMessageRef(pWaypoint->hDisplayNameMsg);
		estrCopy2(&pIcon->pchLabel, pchLabel);

		// Designers can specify alignment in the waypoint name using an SMF-like tag.
		if (!strnicmp(pchLabel, "<align ", 7))
		{
			const char *pchEnd = strchr(pchLabel + 7, '>');

			if (pchEnd)
			{
				estrCopy2(&pIcon->pchLabel, pchEnd + 1);
				if (!strnicmp(pchLabel + 7, "right", 5))
					pIcon->eLabelAlignment = UIRight;
				else if (!strnicmp(pchLabel + 7, "left", 4))
					pIcon->eLabelAlignment = UILeft;
				else if (!strnicmp(pchLabel + 7, "bottom", 6))
					pIcon->eLabelAlignment = UIBottom;
				else if (!strnicmp(pchLabel + 7, "top", 3))
					pIcon->eLabelAlignment = UITop;
			}
		}
	}
	else if (pWaypoint->pchMissionRefString)
	{
		//GenMapIconGetMissionLabel(pWaypoint, &pIcon->pchLabel); //This is the way we used to get the label
		MissionDef *pDef = missiondef_DefFromRefString(pWaypoint->pchMissionRefString);

		estrClear(&pIcon->pchLabel);

		if (pDef)
		{
			bool bSucceeded = false;
			bool bFailed = false;
			bool bOnMap = false;
			const char *pchMissionMapName = mission_GetMapDisplayNameFromMissionDef(pDef);

			//Check to see if we are on the correct map
			if (pDef->eaObjectiveMaps)
			{
				int i;
				Entity *pEnt = entActivePlayerPtr();
				

				for(i = 0; i < eaSize(&pDef->eaObjectiveMaps); ++i)
				{
					MissionMap *pMap = pDef->eaObjectiveMaps[i];
					const char *pchMapName;
					pchMapName = zmapInfoGetCurrentName(zmapGetInfo(NULL));
					if(pchMapName == pMap->pchMapName)
					{
						bOnMap = true;
					}
				}

			}

			{
				Entity *pEnt = entActivePlayerPtr();
				MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

				if (pInfo)
				{
					Mission *pMission = mission_GetMissionOrSubMissionByName(pInfo, pWaypoint->pchMissionRefString);

					if (pMission == NULL)
					{
						OpenMission * pOpenMission = mapState_OpenMissionFromName(mapStateClient_Get(), pWaypoint->pchMissionRefString);
						if (pOpenMission)
							pMission = pOpenMission->pMission;
					}

					// If our state is succeeded or failed, we don't care about the objective map that we found above,
					// we care about the return map
					if (pMission && (pMission->state == MissionState_Succeeded || pMission->state == MissionState_Failed))
					{
						const char *pchMapName = zmapInfoGetCurrentName(zmapGetInfo(NULL));
						
						if (pMission->state == MissionState_Succeeded)
						{
							bSucceeded = true;
						}
						else if (pMission->state == MissionState_Failed)
						{
							bFailed = true;
						}

						if (pchMapName == pDef->pchReturnMap)
						{
							bOnMap = true;
						}
						else
						{
							if (pDef->pchReturnMap)
							{
								DisplayMessage *pMessage = zmapInfoGetDisplayNameMessage(zmapInfoGetByPublicName(pDef->pchReturnMap));
								if (pMessage)
								{
									pchMissionMapName = TranslateMessageRef(pMessage->hMessage);
								}
							}
							bOnMap = false;
						}
					}
				}
			}

			//If the player is on the objective map, show the sub-objective for the waypoint
			if(bOnMap)
			{
				if(bSucceeded)
				{
					missionsystem_ClientFormatMessagePtr("MissionUI", entActivePlayerPtr(), pDef, 0, &pIcon->pchLabel, GET_REF(pDef->msgReturnStringMsg.hMessage));
				}
				else if(bFailed)
				{
					missionsystem_ClientFormatMessagePtr("MissionUI", entActivePlayerPtr(), pDef, 0, &pIcon->pchLabel, GET_REF(pDef->failReturnMsg.hMessage));
				}
				else
				{
					missionsystem_ClientFormatMessagePtr("MissionUI", entActivePlayerPtr(), pDef, 0, &pIcon->pchLabel, GET_REF(pDef->uiStringMsg.hMessage));
				}
			}
			else if(pchMissionMapName && pchMissionMapName[0]) // If we aren't on the correct map, tell the player to go to that map
			{
				estrPrintf(&pIcon->pchLabel, "%s %s", TranslateMessageKeySafe("MissionHelper_GoToMap"), pchMissionMapName);
			}
			else // If all else fails, use the standard mission name
			{
				GenMapIconGetMissionLabel(pWaypoint, &pIcon->pchLabel);
			}
			
		}

	}
	else
		estrCopy2(&pIcon->pchLabel, pWaypoint->pchDescription);

	if(pWaypoint->pchContactName)
	{
		S32 j;
		Entity *pPlayer = entActivePlayerPtr();
		// I can't believe there are four different strings on pWaypoint and yet
		// none of them are the contact's display name.
		for (j = 0; j < eaSize(&pPlayer->pPlayer->pInteractInfo->nearbyContacts); j++)
		{
			ContactInfo *pContact = pPlayer->pPlayer->pInteractInfo->nearbyContacts[j];
			if (pContact->pchContactDef == pWaypoint->pchContactName && pContact->pchStaticEncName == pWaypoint->pchStaticEncName)
			{
				Entity *pContactEnt = entFromEntityRefAnyPartition(pContact->entRef);
				EntityFlags eFlags = pContactEnt ? entGetFlagBits(pContactEnt) : 0;
				if (pContactEnt)
					estrCopy2(&pIcon->pchLabel, entGetLocalName(pContactEnt));
			}
		}
	}
}

static void GenMapFreeFogTexture(UIGenMapState *pState)
{
	eaiDestroy(&pState->eaiRevealBits);
	if (pState->pFogTexData)
	{
		memrefDecrement(pState->pFogTexData);
		pState->pFogTexData = NULL;
	}
	if (pState->pFogOverlay)
	{
		texGenFreeNextFrame(pState->pFogOverlay);
		pState->pFogOverlay = NULL;
	}
}

static void GenMapRefreshFogTexture(UIGenMapState *pState, MapRevealInfo *pRevealInfo)
{
	bool bRefresh = false;

	if (pRevealInfo
		&& pRevealInfo->eType == kMapRevealType_Grid
		&& eaiSize(&pRevealInfo->eaiRevealed))
	{
		if (eaiSize(&pState->eaiRevealBits) != eaiSize(&pRevealInfo->eaiRevealed))
			bRefresh = true;
		else if (memcmp(pRevealInfo->eaiRevealed, pState->eaiRevealBits, eaiSize(&pRevealInfo->eaiRevealed) * sizeof(pRevealInfo->eaiRevealed[0])))
			bRefresh = true;
	}
	else
	{
		GenMapFreeFogTexture(pState);
	}


	if (bRefresh)
	{
		int sz = MAPREVEAL_MAX_BITS_PER_DIMENSION * MAPREVEAL_MAX_BITS_PER_DIMENSION / 2;
		eaiCopy(&pState->eaiRevealBits, &pRevealInfo->eaiRevealed);
		if (!pState->pFogTexData)
			pState->pFogTexData = memrefAlloc(sz);
		if (!pState->pFogOverlay)
			pState->pFogOverlay = texGenNew(MAPREVEAL_MAX_BITS_PER_DIMENSION, MAPREVEAL_MAX_BITS_PER_DIMENSION, "MapReveal", TEXGEN_NORMAL, WL_FOR_UI);
		dxtCompressBitfield(pState->eaiRevealBits, MAPREVEAL_MAX_BITS_PER_DIMENSION, MAPREVEAL_MAX_BITS_PER_DIMENSION, ColorWhite, pState->pFogTexData, sz);
		texGenUpdate(pState->pFogOverlay, pState->pFogTexData, RTEX_2D, RTEX_DXT1, 1, false, false, GenMapShouldUseSmoothShader(), 1);
	}
}

static UIGenMapIcon *MapIconResetIndex(UIGenMapIcon *pCurIcon)
{
	pCurIcon->iKeyIndex = 0;
	return pCurIcon;
}

static int EntityCompareDistance(const Vec3 v3Center, const Entity **ppA, const Entity **ppB)
{
	Vec3 v3A;
	Vec3 v3B;
	F32 fDistA;
	F32 fDistB;
	entGetPos((Entity *)*ppA, v3A);
	entGetPos((Entity *)*ppB, v3B);
	fDistA = distance3Squared(v3A, v3Center);
	fDistB = distance3Squared(v3B, v3Center);
	return (fDistA < fDistB) ? -1 : (fDistA > fDistB) ? 1 : (*ppA - *ppB);
}

static int TargetableNodeCompareDistance(const Vec3 v3Center, const TargetableNode **ppA, const TargetableNode **ppB)
{
	Vec3 v3A = {1e10, 1e10, 1e10};
	Vec3 v3B = {1e10, 1e10, 1e10};
	F32 fRadiusA = GET_REF((*ppA)->hNode) ? wlInteractionNodeGetSphereBounds(GET_REF((*ppA)->hNode), v3A) : 0;
	F32 fRadiusB = GET_REF((*ppB)->hNode) ? wlInteractionNodeGetSphereBounds(GET_REF((*ppB)->hNode), v3B) : 0;
	F32 fDistA = distance3Squared(v3A, v3Center) - SQR(fRadiusA);
	F32 fDistB = distance3Squared(v3B, v3Center) - SQR(fRadiusB);
	return (fDistA < fDistB) ? -1 : (fDistA > fDistB) ? 1 : (*ppA - *ppB);
}

static int WaypointCompareDistance(const Vec3 v3Center, const MinimapWaypoint **ppA, const MinimapWaypoint **ppB)
{
	F32 fDistA = distance3Squared((*ppA)->pos, v3Center);
	F32 fDistB = distance3Squared((*ppB)->pos, v3Center);
	return (fDistA < fDistB) ? -1 : (fDistA > fDistB) ? 1 : (*ppA - *ppB);
}

static int SortMapKeyIcons(const UIGenMapKeyIcon **ppIconA, const UIGenMapKeyIcon **ppIconB)
{
	if(ppIconA && ppIconB)
	{
		const UIGenMapKeyIcon *pIconA = *ppIconA;
		const UIGenMapKeyIcon *pIconB = *ppIconB;

		if (pIconA && pIconB)
		{
			S32 iComparison;

			if (pIconA->eWaypointType != MinimapWaypointType_None)
			{
				if (pIconA->eWaypointType != MinimapWaypointType_Landmark)
				{
					if (pIconB->eWaypointType != MinimapWaypointType_None)
					{
						if (pIconB->eWaypointType != MinimapWaypointType_Landmark)
						{
							iComparison = pIconB->eWaypointType - pIconA->eWaypointType;
						}
						else
						{
							iComparison = -1;
						}
					}
					else
					{
						iComparison = -1;
					}
				}
				else // A is a landmark
				{
					if (pIconB->eWaypointType == MinimapWaypointType_Landmark)
					{
						iComparison = 0;
					} 
					else
					{
						iComparison = 1;
					}
				}
			}
			else //Icon A is a contact
			{
				if (pIconB->eWaypointType != MinimapWaypointType_None)
				{
					if (pIconB->eWaypointType != MinimapWaypointType_Landmark)
					{
						iComparison = 1;
					}
					else
					{
						iComparison = -1;
					}
				}
				else
				{
					iComparison = pIconB->eContactIndicator - pIconA->eContactIndicator;
				}
			}
			/*if(pIconA->eContactIndicator != ContactIndicator_NoInfo)
			{
				if (pIconB->eContactIndicator != ContactIndicator_NoInfo)
				{
					iComparison = pIconA->eContactIndicator - pIconB->eContactIndicator;
				} else {
					iComparison = 1;
				}
			} else {
				if (pIconB->eContactIndicator != ContactIndicator_NoInfo) 
				{
					iComparison = 1;
				} else {
					iComparison = pIconA->eWaypointType - pIconB->eWaypointType;
				}
			}*/

			if(iComparison == 0)
			{
				if(pIconA->pchLabel != pIconB->pchLabel)
				{
					iComparison = stricmp(pIconA->pchLabel, pIconB->pchLabel);
				}
			}
			return iComparison;
		}
	}
	return 1;
}

static const char *ui_GetHeaderNameForKeyIcon(UIGenMapKeyIcon *pIcon)
{
	const char *pchHeader = NULL;

	if (pIcon->eWaypointType != MinimapWaypointType_None)
	{
		pchHeader = StaticDefineGetTranslatedMessage(MinimapWaypointTypeEnum, pIcon->eWaypointType);
	}
	else
	{
		pchHeader = StaticDefineGetTranslatedMessage(ContactIndicatorEnum, pIcon->eContactIndicator);
	}

	if (!pchHeader)
	{
		pchHeader = allocAddString("");
	}

	return pchHeader;
}

// helper for _CreateIconsForPvPDomination
static void _SetPvPDominationIcon(UIGenMapIcon *pIcon, UIGenMapIconDef *pIconDef)
{
	Message *pLabel = GET_REF(pIconDef->hLabel);

	pIcon->pchNode = NULL;
	pIcon->pchTexture = NULL;
	pIcon->hEntity = 0;
	pIcon->fRotation = 0;
	pIcon->bHideUnlessRevealed = false;
	estrClear(&pIcon->pchLabel);
	
	if (pLabel)
	{
		FormatMessagePtr(&pIcon->pchLabel, pLabel, STRFMT_END);
	}

	pIcon->eLabelAlignment = pIconDef->eLabelAlignment;
	pIcon->pDef = pIconDef;
}

// creates the icons for PvP domination map
static void _CreateIconsForPvPDomination(Entity *pEnt, UIGenMapState *pState, S32 *piCountInOut, 
											UIGenMapPvPDominationIconDef *pPvPDominationIconDef)
{
	DOMControlPoint ***peaControlPoints = NULL;
	MapState *pMapState = mapStateClient_Get();

	peaControlPoints = pMapState ? mapState_GetGameSpecificStructs(pMapState) : NULL;
	if (peaControlPoints)
	{
		S32 iPlayerGroupIdx = -1;
		CritterFaction* pPlayerFaction = entGetFaction(pEnt);

		if (pPlayerFaction->pchName)
		{
			if (pPvPDominationIconDef->pchGroup1Faction && 
				!stricmp(pPvPDominationIconDef->pchGroup1Faction, pPlayerFaction->pchName))
			{
				iPlayerGroupIdx = 0;
			}
			else if (pPvPDominationIconDef->pchGroup2Faction && 
				!stricmp(pPvPDominationIconDef->pchGroup2Faction, pPlayerFaction->pchName))
			{
				iPlayerGroupIdx = 1;
			}
		}

		FOR_EACH_IN_EARRAY(*peaControlPoints, DOMControlPoint, pControlPoint)
		{
			UIGenMapIcon *pIcon = MapIconResetIndex(eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, *piCountInOut));
			(*piCountInOut)++;

			copyVec3(pControlPoint->vLocation, pIcon->v3WorldPos);

			// determine the state of the control point
			if (pControlPoint->iOwningGroup == -1)
			{	
				// unowned
				if (kDOMPointStatus_Contested == pControlPoint->eStatus)
				{
					if (pControlPoint->iAttackingGroup == iPlayerGroupIdx)
					{ // friendly attacking
						_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->unownedContestedFriendly);
					}
					else
					{ // enemy attacking
						_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->unownedContestedEnemy);
					}
				}
				else
				{
					_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->unowned);
				}
			}
			else 
			{
				if (kDOMPointStatus_Controled == pControlPoint->eStatus)
				{
					if (pControlPoint->iOwningGroup == iPlayerGroupIdx)
					{ // friendly
						_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->ownedFriendly);
					}
					else
					{ // enemy
						_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->ownedEnemy);
					}
				}
				else
				{
					if (pControlPoint->iOwningGroup == iPlayerGroupIdx)
					{ // friendly owns it, being contested
						_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->ownedFriendlyContested);
					}
					else
					{ // enemy owns it, being contested
						_SetPvPDominationIcon(pIcon, &pPvPDominationIconDef->ownedEnemyContested);
					}
				}

			}

		}
		FOR_EACH_END
	}
}

void ui_GenUpdateMap(UIGen *pGen)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	Entity *pEnt = entActivePlayerPtr();
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	MapRevealInfo *pReveal = pState->pReveal = mapRevealInfoGetByRegion(entActivePlayerPtr(), NULL);
	const char *pchMapName = allocAddString(zmapInfoGetPublicName(NULL));
	UIGenMapFakeZoneHighlight *pHighlight = NULL;
	S32 iCount = 0;
	S32 i;
	S32 iKeyIconCount = 0;
	Vec3 vEntPos;

	// Update the map notifications
	gclMapNotifications_Tick();

	FillFilterAndSortMissionsWithNumbers(&s_eaSortedMissions);

	if (pMap->bRememberScales && pState->fSaveScaleTimer > 0)
	{
		pState->fSaveScaleTimer -= g_ui_State.timestep;
		if (pState->fSaveScaleTimer <= 0)
			ServerCmd_gslSetMapRegionScale(entGetWorldRegionTypeOfEnt(pEnt), pState->fPixelsPerWorldUnit);
	}

	UI_GEN_LOAD_TEXTURE(pMap->pchMask, pState->pMask);
	UI_GEN_LOAD_TEXTURE(pMap->pchBackgroundTex, pState->pBackgroundTex);
	UI_GEN_LOAD_TEXTURE(pMap->pchUpwardIcon, pState->pUpwardIcon);
	UI_GEN_LOAD_TEXTURE(pMap->pchDownwardIcon, pState->pDownwardIcon);

	if (pState->pchFakeZone && (pState->pFakeZone = eaIndexedGetUsingString(&pMap->eaFakeZones, pState->pchFakeZone)))
	{
		pReveal = pState->pReveal = &pState->FakeZoneReveal;

		pState->FakeZoneReveal.v3RegionMin[0] = pState->pFakeZone->v2WorldSize[0] / -2 + pState->pFakeZone->v2WorldOffset[0];
		pState->FakeZoneReveal.v3RegionMin[1] = 0;
		pState->FakeZoneReveal.v3RegionMin[2] = pState->pFakeZone->v2WorldSize[1] / -2 + pState->pFakeZone->v2WorldOffset[1];

		pState->FakeZoneReveal.v3RegionMax[0] = pState->pFakeZone->v2WorldSize[0] / 2 + pState->pFakeZone->v2WorldOffset[0];
		pState->FakeZoneReveal.v3RegionMax[1] = 0;
		pState->FakeZoneReveal.v3RegionMax[2] = pState->pFakeZone->v2WorldSize[1] / 2 + pState->pFakeZone->v2WorldOffset[1];
	}
	else
		pState->pFakeZone = NULL;

	if (!pEnt)
		return;

	entGetPos(pEnt, vEntPos);

	if (!pReveal)
	{
		WorldRegion *pRegion;
		pRegion = worldGetWorldRegionByPos(vEntPos);
		pReveal = pState->pReveal = &pState->FakeZoneReveal;
		if (pRegion)
		{
			mapSnapRegionGetMapBounds(pRegion, pReveal->v3RegionMin, pReveal->v3RegionMax);
		}
		else
			zmapGetBounds(NULL, pReveal->v3RegionMin, pReveal->v3RegionMax);
	}

	if (!(pReveal && pMissionInfo))
	{
		return;
	}

	pHighlight = GetHighlight(pState->pFakeZone, NULL, NULL, NULL);

	if (!pState->pFakeZone 
		// TODO(jm): Delete me, the flags should replace the gConf flag
		|| gConf.bShowMapIconsOnFakeZones 
		|| pState->pFakeZone->bShowIcons 
		|| (pHighlight && pHighlight->bShowIcons))
	{

		Entity *pTarget;
		EntityIterator *pIter;
		static char const * * s_eaMissionReturnEntEncounters = NULL;

		static Entity **s_eaEnts;
		static TargetableNode **s_eaNodes;
		static MinimapWaypoint **s_eaWaypoints;
		typedef struct ClosestDoorWaypointInfo
		{
			MinimapWaypoint * pWaypoint;
			F32 fDistSq;
		} ClosestDoorWaypointInfo;
		ClosestDoorWaypointInfo * papClosestDoorWaypoints;
		int iClosestDoorWaypoints=0;
		Vec2 v2RadiusCenter = 
		{
			(pGen->ScreenBox.lx + pGen->ScreenBox.hx) / 2 + pMap->ClampRadius.v2Offset[0] * pGen->fScale, 
			(pGen->ScreenBox.ly + pGen->ScreenBox.hy) / 2 + pMap->ClampRadius.v2Offset[1] * pGen->fScale
		};
		float fClampRadius = pMap->ClampRadius.fClampRadius * pGen->fScale;
		CBox ClampBox = pGen->ScreenBox;
		ClampBox.lx += pMap->uiLeftIconPadding;
		ClampBox.hx -= pMap->uiRightIconPadding;
		ClampBox.ly += pMap->uiTopIconPadding;
		ClampBox.hy -= pMap->uiBottomIconPadding;

		if (pState->hTarget == -1 && !pMap->bFollowPlayer)
		{
			pTarget = NULL;
		}
		else
		{
			Entity *pFollow = entFromEntityRefAnyPartition(pState->hTarget);
			if (!pFollow)
			{
				pState->hTarget = pMap->bFollowPlayer ? 0 : -1;
				pFollow = pMap->bFollowPlayer ? pEnt : NULL;
			}

			if (pFollow)
			{
				Vec3 v;
				entGetPos(pFollow, v);
				if (pMap->bFollowPlayer && !sameVec3(v, pState->v3TargetPrev))
				{
					copyVec3(pState->v3Target, pState->v3TargetPrev);
					copyVec3(v, pState->v3Target);
					pState->bFollowing |= pMap->bFollowPlayer;
				}
			}
		}

		GenMapRefreshFogTexture(pState, pReveal);

		for (i = 0; i < eaSize(&pMap->eaEntityIcons); i++)
			pMap->eaEntityIcons[i]->Def.iCount = 0;
		for (i = 0; i < eaSize(&pMap->eaNodeIcons); i++)
			pMap->eaNodeIcons[i]->Def.iCount = 0;
		for (i = 0; i < eaSize(&pMap->eaWaypointIcons); i++)
			pMap->eaWaypointIcons[i]->Def.iCount = 0;

		PERFINFO_AUTO_START("Building Map Icon List", 1);

		//////////////////////////////////////////////////////////////////////////
		// Build entity / node / waypoint lists
		eaClearFast(&s_eaEnts);

		// Don't exclude DONOTDRAW - contacts and teammates that are far away
		// are marked DONOTDRAW. Don't exclude UNTARGETABLE - contacts and escorts
		// are untargetable.
		pIter = entGetIteratorAllTypesAllPartitions(0, ENTITYFLAG_IGNORE | ENTITYFLAG_UNSELECTABLE);
		while (pTarget = EntityIteratorGetNext(pIter))
		{
			if (!pTarget->bImperceptible) // character_CanPerceive, but cached
				eaPush(&s_eaEnts, pTarget);
		}
		EntityIteratorRelease(pIter);

		eaClearFast(&s_eaNodes);
		for (i = 0; i < eaSize(&pEnt->pPlayer->InteractStatus.ppTargetableNodes); i++)
		{
			TargetableNode *pNode = pEnt->pPlayer->InteractStatus.ppTargetableNodes[i];
			if (GET_REF(pNode->hNode))
				eaPush(&s_eaNodes, pNode);
		}

		eaCopy(&s_eaWaypoints, &pMissionInfo->waypointList);
		for (i = 0; i < eaSize(&pEnt->pPlayer->ppMyWaypoints); i++)
		{
			if (pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn
				&& pchMapName
				&& !stricmp(pchMapName, pEnt->pPlayer->ppMyWaypoints[i]->MapCreatedOn))
			{
				eaPush(&s_eaWaypoints, pEnt->pPlayer->ppMyWaypoints[i]);
			}
		}

		// Nodes/Ents are distance from the active target - usually the player.
		eaQSort_s(s_eaEnts, EntityCompareDistance, pState->v3Target);
		eaQSort_s(s_eaNodes, TargetableNodeCompareDistance, pState->v3Target);
		// Waypoints are distance from the map center.
		eaQSort_s(s_eaWaypoints, WaypointCompareDistance, pState->v3WorldCenter);

		//////////////////////////////////////////////////////////////////////////
		// Match and fill icons.
		for (i = 0; i < eaSize(&s_eaEnts); i++)
		{
			UIGenMapEntityIconDef *pEntityIconDef;
			if (pEntityIconDef = GenMapMatchEntity(pGen, pMap->eaEntityIcons, pEnt, s_eaEnts[i], &ClampBox, fClampRadius, v2RadiusCenter))
			{
				UIGenMapIcon *pCurIcon = eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount++);
				GenMapIconForEntity(pEnt, s_eaEnts[i], &pEntityIconDef->Def, pCurIcon, pState->v3WorldCenter);

				if (pEntityIconDef->eContactIndicator == ContactIndicator_MissionCompleted || pEntityIconDef->eContactIndicator == ContactIndicator_MissionCompletedRepeatable)
				{
					ContactInfo * pContact;
					pContact = GenMapContactForEntity(pEnt,s_eaEnts[i]->myRef);
					if (pContact && (pContact->currIndicator == ContactIndicator_MissionCompleted || pContact->currIndicator == ContactIndicator_MissionCompletedRepeatable))
					{
						eaPush(&s_eaMissionReturnEntEncounters,pContact->pchStaticEncName);
					}
				}
				pCurIcon->iKeyIndex = 0;
				if(pEntityIconDef && pEntityIconDef->eContactIndicator != ContactIndicator_NoInfo && pMap->bShowMapKey)
				{
					UIGenMapKeyIcon *pKeyIcon = eaGetStruct(&s_eaMapKeyIcons, parse_UIGenMapKeyIcon, iKeyIconCount++);
					pKeyIcon->eContactIndicator = pEntityIconDef->eContactIndicator;
					pKeyIcon->eWaypointType = MinimapWaypointType_None;
					pKeyIcon->pchLabel = allocAddString(pCurIcon->pchLabel);
					pKeyIcon->iKeyIndex = iCount - 1;
					pKeyIcon->bIsHeader = false;
					pKeyIcon->pchTexture = NULL;
				}
			}
		}

		// A nice tidy block of memory that will get rid of itself
		papClosestDoorWaypoints = (ClosestDoorWaypointInfo *)alloca(sizeof(ClosestDoorWaypointInfo)*eaSize(&s_eaWaypoints));

		// pre-process the door waypoints.  We only want to keep the closest one for each mission
		for (i = 0; i < eaSize(&s_eaWaypoints); i++)
		{
			if (s_eaWaypoints[i]->bIsDoorWaypoint)
			{
				MinimapWaypoint * pClosestWaypointForThisMission = NULL;
				int iWp;
				F32 fDistSq = distance3Squared(s_eaWaypoints[i]->pos,vEntPos);
				bool bClosestFound = false;
				bool bThisOneIsClosest = false;
				
				for (iWp=0;iWp<iClosestDoorWaypoints;iWp++)
				{
					if (papClosestDoorWaypoints[iWp].pWaypoint->pchMissionRefString == s_eaWaypoints[i]->pchMissionRefString)
					{
						bClosestFound = true;
						if (fDistSq < papClosestDoorWaypoints[iWp].fDistSq)
						{
							// new closest wp for this mission
							bThisOneIsClosest = true;
							break;
						}
					}
				}

				if (bThisOneIsClosest || !bClosestFound)
				{
					papClosestDoorWaypoints[iWp].pWaypoint = s_eaWaypoints[i];
					papClosestDoorWaypoints[iWp].fDistSq = fDistSq;
					if (!bClosestFound)
						iClosestDoorWaypoints++;
				}
			}
		}

		for (i = 0; i < eaSize(&s_eaWaypoints); i++)
		{
			bool bShouldDraw = true;

			if (s_eaWaypoints[i]->type == MinimapWaypointType_MissionReturnContact)
			{
				// search the return ent encounters to make sure we should draw
				if (eaFind(&s_eaMissionReturnEntEncounters, s_eaWaypoints[i]->pchStaticEncName) >= 0)
				{
					// I was hoping to improve the label here, but the system we use to print map labels doesn't support newlines.
					// Probably would be easiest to make GenMapDrawLabel do 2 prints in this case [RMARR - 7/19/11]
#if 0
					char * pchLabel = NULL;
					GenMapIconGetMissionLabel(s_eaWaypoints[i], &pchLabel);

					// ASSUMPTION - the first icons made are entity icons, and this index is good for pState->eaIcons
					estrConcatf(&pState->eaIcons[j]->pchLabel, "<br>%s", pchLabel);

					estrDestroy(&pchLabel);
#endif
					bShouldDraw = false;
				}
			}

			if (s_eaWaypoints[i]->bIsDoorWaypoint)
			{
				int iWp;
				bShouldDraw = false;

				// if this turned out to be slow, I could probably figure out a way to avoid it
				for (iWp=0;iWp<iClosestDoorWaypoints;iWp++)
				{
					if (papClosestDoorWaypoints[iWp].pWaypoint == s_eaWaypoints[i])
					{
						bShouldDraw = true;
						break;
					}
				}
			}

			if (bShouldDraw)
			{
				UIGenMapWaypointIconDef *pWaypointIconDef;

				if (pWaypointIconDef = GenMapMatchWaypoint(pGen, pState, pMap->eaWaypointIcons, s_eaWaypoints[i], &ClampBox, fClampRadius, v2RadiusCenter, true))
				{
					GenMapIconForWaypoint(s_eaWaypoints[i], &pWaypointIconDef->Def, eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount++), pState->v3WorldCenter);
					if (pWaypointIconDef->Def.bOutOfBounds
						&& pWaypointIconDef->bAreaWaypoint
						&& (pWaypointIconDef = GenMapMatchWaypoint(pGen, pState, pMap->eaWaypointIcons, s_eaWaypoints[i], &ClampBox, fClampRadius, v2RadiusCenter, false)))
					{
						GenMapIconForWaypoint(s_eaWaypoints[i], &pWaypointIconDef->Def, eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount++), pState->v3WorldCenter);
					}

					if(pWaypointIconDef->eType != MinimapWaypointType_None && pMap->bShowMapKey) 
					{
						UIGenMapKeyIcon *pNewIcon = eaGetStruct(&s_eaMapKeyIcons, parse_UIGenMapKeyIcon, iKeyIconCount++);
						pNewIcon->eWaypointType = pWaypointIconDef->eType;
						pNewIcon->eContactIndicator = ContactIndicator_NoInfo;
						pNewIcon->iKeyIndex = iCount - 1;
						pNewIcon->bIsHeader = false;
						pNewIcon->pchTexture = NULL;
						if(s_eaWaypoints[i]->pchMissionRefString)
						{
							char *pchLabel = NULL;
							GenMapIconGetMissionLabel(s_eaWaypoints[i], &pchLabel);
							pNewIcon->pchLabel = allocAddString(pchLabel);
						} 
						else
						{
							pNewIcon->pchLabel = allocAddString(TranslateMessageRef(s_eaWaypoints[i]->hDisplayNameMsg));
						}
					}
				}
			}
		}

		eaClear(&s_eaMissionReturnEntEncounters);

		for (i = 0; i < eaSize(&s_eaNodes); i++)
		{
			UIGenMapNodeIconDef *pNodeIconDef;
			if (pNodeIconDef = GenMapMatchNode(pGen, pMap->eaNodeIcons, s_eaNodes[i], pEnt, &ClampBox, fClampRadius, v2RadiusCenter))
			{
				UIGenMapIcon *pIcon = MapIconResetIndex(eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount++));
				GenMapIconForNode(s_eaNodes[i], &pNodeIconDef->Def, pIcon, pState->v3WorldCenter);

				if(pMap->bShowMapKey)
				{
					UIGenMapKeyIcon *pKeyIcon = eaGetStruct(&s_eaMapKeyIcons, parse_UIGenMapKeyIcon, iKeyIconCount++);
					pKeyIcon->eContactIndicator = ContactIndicator_NoInfo;
					pKeyIcon->eWaypointType = MinimapWaypointType_None;
					pKeyIcon->pchLabel = allocAddString(pIcon->pchLabel);
					pKeyIcon->iKeyIndex = iCount - 1;
					pKeyIcon->bIsHeader = false;
					pKeyIcon->pchTexture = NULL;
				}
			}
		}
		
		if (pMap->pPlayerIcon)
			GenMapIconForEntity(pEnt, pEnt, pMap->pPlayerIcon, MapIconResetIndex(eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount++)), pState->v3WorldCenter);

		// Camera is identical to player except with a different rotation.
		if (pMap->pCameraIcon)
		{
			Vec3 pyr;
			UIGenMapIcon *pCameraIcon = MapIconResetIndex(eaGetStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount++));
			gfxGetActiveCameraYPR(pyr);
			GenMapIconForEntity(pEnt, pEnt, pMap->pCameraIcon, pCameraIcon, pState->v3WorldCenter);
			pCameraIcon->fRotation = pyr[1];
			estrClear(&pCameraIcon->pchLabel);
		}

		//
		if (pMap->pPvPDominationIcons)
		{
			_CreateIconsForPvPDomination(pEnt, pState, &iCount, pMap->pPvPDominationIcons);
		}

		PERFINFO_AUTO_STOP();
	}

	eaSetSizeStruct(&pState->eaIcons, parse_UIGenMapIcon, iCount);
	if(pMap->bShowMapKey)
	{
		eaSetSizeStruct(&s_eaMapKeyIcons, parse_UIGenMapKeyIcon, iKeyIconCount);
		eaQSort(s_eaMapKeyIcons, SortMapKeyIcons);
	}

	//The following does some things that could be fairly slow inside of a loop including
	//removes and inserts in an earray.
	//This is because the list of key icons used by the rows in the minimap key has to
	//be built at the same time as the waypoint icons so that they know about each other.
	//Fortunately these arrays should never have too many things in them, but it might be
	//a good idea to go back and reimplement this stuff in a faster way. -DHOGBERG 12/12/2011
	if(pMap->bShowMapKey)
	{
		UIGenMapKeyIcon *pOldIcon = NULL;
		UIGenMapKeyIcon *pCurIcon = NULL;
		int iIndex = 1;

		i = 0;
		while (i < eaSize(&s_eaMapKeyIcons))
		{
			pCurIcon = s_eaMapKeyIcons[i];
			if(pCurIcon)
			{
				UIGenMapIcon *pMapIcon = pState->eaIcons[pCurIcon->iKeyIndex];
				if(pMapIcon->pchTexture && pMapIcon->pchTexture[0])
				{
					pCurIcon->pchTexture = pMapIcon->pchTexture;
				}
				else
				{
					pCurIcon->pchTexture = pMapIcon->pDef->pchIcon;
				}
				//If the two icons are the same, remove the second one and update its waypoint to match the number index
				if (pOldIcon && pCurIcon->pchLabel == pOldIcon->pchLabel
							 && pCurIcon->eContactIndicator == pOldIcon->eContactIndicator
							 && pCurIcon->eContactIndicator == pOldIcon->eContactIndicator)
				{
					pState->eaIcons[pCurIcon->iKeyIndex]->iKeyIndex = pOldIcon->iKeyIndex;

					if (s_eaMapKeyIcons[i])
					{
						StructDestroy(parse_UIGenMapKeyIcon, s_eaMapKeyIcons[i]);
					}

					eaRemove(&s_eaMapKeyIcons, i);
				}
				else if (!pCurIcon->pchLabel || !pCurIcon->pchLabel[0])
				{
					if (s_eaMapKeyIcons[i])
					{
						StructDestroy(parse_UIGenMapKeyIcon, s_eaMapKeyIcons[i]);
					}

					eaRemove(&s_eaMapKeyIcons, i); //Remove any that don't have labels
				}
				else
				{
					if(!pOldIcon
						|| pCurIcon->eContactIndicator != pOldIcon->eContactIndicator
						|| pCurIcon->eWaypointType != pOldIcon->eWaypointType)
					{ 
						//If the new icon is in a different category, we need to insert a header
						UIGenMapKeyIcon *pNewHeader = StructCreate(parse_UIGenMapKeyIcon);
						pNewHeader->pchLabel = ui_GetHeaderNameForKeyIcon(pCurIcon);
						pNewHeader->bIsHeader = true;
						eaInsert(&s_eaMapKeyIcons, pNewHeader, i++);
					}

					pState->eaIcons[pCurIcon->iKeyIndex]->iKeyIndex = iIndex;
					pCurIcon->iKeyIndex = iIndex++;
					pOldIcon = pCurIcon;
					i++;
				}
			}
		}
	}
}

static void GenMapClampIconToEdge(CBox *pIconBox, const CBox *pClampBox, F32 fRadius, const Vec2 v2ClampRadiusCenter)
{
	Vec2 v2IconCenter = { (pIconBox->lx + pIconBox->hx)/2, (pIconBox->ly + pIconBox->hy)/2 };
	F32 fWidth = CBoxWidth(pClampBox);
	F32 fHeight = CBoxHeight(pClampBox);
	F32 fIconX = v2IconCenter[0] - pClampBox->lx;
	F32 fIconY = v2IconCenter[1] - pClampBox->ly;
	F32 fClampedX = CLAMP(fIconX, 0, fWidth);
	F32 fClampedY = CLAMP(fIconY, 0, fHeight);
	F32 fHalfWidth = fWidth / 2;
	F32 fHalfHeight = fHeight / 2;
	F32 fAspectRatio = fHeight ? fWidth / fHeight : 1;
	F32 fInvAspectRatio = fWidth ? fHeight / fWidth : 1;

	if (fRadius != 0)
	{
		Vec2 vR = { v2IconCenter[0] - v2ClampRadiusCenter[0], v2IconCenter[1] - v2ClampRadiusCenter[1] };
		F32 fRSquared;

		// Automatically determine radius from box size.
		if (fRadius < 0)
			fRadius = max(fHalfWidth, fHalfHeight);

		fRSquared = lengthVec2Squared(vR);

		if (fRSquared > SQR(fRadius))
		{
			F32 fRatioR = fRadius / fsqrt(fRSquared);
			v2IconCenter[0] = v2ClampRadiusCenter[0] + (vR[0] * fRatioR);
			v2IconCenter[1] = v2ClampRadiusCenter[1] + (vR[1] * fRatioR);
		}
	}

	v2IconCenter[0] = CLAMP(v2IconCenter[0], pClampBox->lx, pClampBox->hx);
	v2IconCenter[1] = CLAMP(v2IconCenter[1], pClampBox->ly, pClampBox->hy);

	CBoxSetCenter(pIconBox, v2IconCenter[0], v2IconCenter[1]);
}

static void GenMapPushIcons(EARRAY_OF(UIGenMapIcon) eaIconList, const CBox *pScreenBox)
{
	Vec2 v2Mouse;
	static UIGenMapIcon **s_eaPushable;
	bool bChanged = false;
	S32 iIterations = 0;
	S32 i;
	S32 iMouseX, iMouseY;
	mousePos(&iMouseX, &iMouseY);
	v2Mouse[0] = iMouseX;
	v2Mouse[1] = iMouseY;

	eaClearFast(&s_eaPushable);
	for (i = 0; i < eaSize(&eaIconList); i++)
	{
		UIGenMapIcon *pIcon = eaIconList[i];
		Vec2 v2Center;
		pIcon->fPushX = 0;
		pIcon->fPushY = 0;
		pIcon->fMouseDistanceSqd = 0;
		pIcon->DesiredScreenBox = pIcon->ScreenBox;
		CBoxGetCenter(&pIcon->ScreenBox, &v2Center[0], &v2Center[1]);
		pIcon->fMouseDistanceSqd = distance2Squared(v2Mouse, v2Center);
		if (pIcon->pDef->bPushable
			&& pIcon->fMouseDistanceSqd < SQR(s_fIconPushRadius)
			&& CBoxIntersects(&pIcon->ScreenBox, pScreenBox))
			eaPush(&s_eaPushable, pIcon);
	}

	if (!eaSize(&s_eaPushable))
		return;

	do
	{
		bChanged = false;

		for (i = 0; i < eaSize(&s_eaPushable); i++)
		{
			F32 fCenterXI, fCenterYI, fRadiusI;
			F32 fTrueCenterXI, fTrueCenterYI;
			bool bIntersection = false;
			UIGenMapIcon *pIconI = s_eaPushable[i];
			S32 j;

			// Get center of icon
			CBoxGetCenter(&pIconI->DesiredScreenBox, &fTrueCenterXI, &fTrueCenterYI);

			fRadiusI = (CBoxWidth(&pIconI->ScreenBox) + CBoxHeight(&pIconI->ScreenBox)) / 4;
			fCenterXI = fTrueCenterXI + pIconI->fPushX;
			fCenterYI = fTrueCenterYI + pIconI->fPushY;

			for (j = 0; j < eaSize(&s_eaPushable); j++)
			{
				if (j != i && CBoxIntersects(&pIconI->ScreenBox, &s_eaPushable[j]->ScreenBox))
				{
					bIntersection = true;
					break;
				}
			}

			// Don't apply push if nothing intersects with it
			if (!bIntersection)
			{
				continue;
			}

			for (j = 0; j < eaSize(&s_eaPushable); j++)
			{
				F32 fCenterXJ, fCenterYJ, fRadiusJ;
				F32 fDistanceSqd, fAngle, fScale;
				UIGenMapIcon *pIconJ = s_eaPushable[j];

				if (i == j)
				{
					continue;
				}

				CBoxGetCenter(&pIconJ->ScreenBox, &fCenterXJ, &fCenterYJ);

				fDistanceSqd = SQR(fCenterXI - fCenterXJ) + SQR(fCenterYI - fCenterYJ);
				if (fDistanceSqd >= SQR(s_fIconPushForce))
				{
					// Ignore large distance cases
					continue;
				}

				fRadiusJ = (CBoxWidth(&pIconJ->ScreenBox) + CBoxHeight(&pIconJ->ScreenBox)) / 4;

				fScale = (fRadiusI + fRadiusJ);
				if (fDistanceSqd < 1.f)
				{
					// Handle (near) zero case
					fAngle = fDistanceSqd == 0 ? -PI / 2 : atan2f(fCenterYJ - fCenterYI, fCenterXJ - fCenterXI);
				}
				else
				{
					fAngle = atan2f(fCenterYI - fCenterYJ, fCenterXI - fCenterXJ);
					MIN1(fScale, 4 * (fRadiusI * fRadiusJ) / fDistanceSqd);
				}

				pIconI->fPushX += cosf(fAngle) * fScale;
				pIconI->fPushY += sinf(fAngle) * fScale;
				fCenterXI = fTrueCenterXI + pIconI->fPushX;
				fCenterYI = fTrueCenterYI + pIconI->fPushY;

				if (pIconI->fPushX && pIconI->fPushY)
				{
					CBoxSetCenter(&pIconI->ScreenBox, fCenterXI, fCenterYI);
					bChanged = true;
				}
			}
		}
	} while (bChanged && iIterations++ < 5);

	for (i = 0; i < eaSize(&s_eaPushable); i++)
	{
		UIGenMapIcon *pIcon = s_eaPushable[i];
		Vec2 v2Center;
		F32 fTween;
		CBoxGetCenter(&pIcon->ScreenBox, &v2Center[0], &v2Center[1]);
		fTween = sqrtf(CLAMPF32(1.f - pIcon->fMouseDistanceSqd / SQR(s_fIconPushRadius), 0.f, 1.f));
		pIcon->ScreenBox = pIcon->DesiredScreenBox;
		pIcon->ScreenBox.lx += pIcon->fPushX * fTween;
		pIcon->ScreenBox.hx += pIcon->fPushX * fTween;
		pIcon->ScreenBox.ly += pIcon->fPushY * fTween;
		pIcon->ScreenBox.hy += pIcon->fPushY * fTween;
	}
}


void ui_GenLayoutLateMap(UIGen *pGen)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	Entity *pEnt = entActivePlayerPtr();
	MapRevealInfo *pReveal = pState->pReveal;
	F32 fGenWidth = CBoxWidth(&pGen->ScreenBox);
	F32 fGenHeight = CBoxHeight(&pGen->ScreenBox);
	F32 fMapWidth;
	F32 fMapHeight;
	int i;

	if (!(pEnt && pReveal))
		return;

	fMapWidth = pReveal->v3RegionMax[0] - pReveal->v3RegionMin[0];
	fMapHeight = pReveal->v3RegionMax[2] - pReveal->v3RegionMin[2];

	if(pState->ePreviousRegionType != entGetWorldRegionTypeOfEnt(pEnt) || pState->fMapScale <= 0)
	{
		PlayerUI *pUI = SAFE_MEMBER2(pEnt, pPlayer, pUI);
		PlayerUIMapRegionScale *pSavedScale = (pMap->bRememberScales && pUI) ? eaIndexedGetUsingInt(&pUI->eaRegionScales, entGetWorldRegionTypeOfEnt(pEnt)) : NULL;

		if (pSavedScale)
			ui_GenMapSetPixelsPerWorldUnit(pGen, pSavedScale->fScale, false);
		else if (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit)
			ui_GenMapSetPixelsPerWorldUnit(pGen, pMap->fScaleDefault, false);
		else
			ui_GenMapSetScale(pGen, pMap->fScaleDefault, false);

		pState->ePreviousRegionType  = entGetWorldRegionTypeOfEnt(pEnt);
	}
	pState->fMapScale = fMapWidth ? fGenWidth / (pState->fSavedPixelsPerWorldUnit * fMapWidth) : 1.0f;

	if (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit)
	{
		F32 fMinScale = fMapWidth ? fGenWidth / (pMap->fScaleMax * fMapWidth) : 1.0f;
		F32 fMaxScale = fMapWidth ? fGenWidth / (pMap->fScaleMin * fMapWidth) : 1.0f;
		pState->fMapScale = CLAMP(pState->fMapScale, fMinScale, fMaxScale);
	}
	else
	{
		pState->fMapScale = CLAMP(pState->fMapScale, pMap->fScaleMin, pMap->fScaleMax);
	}

	pState->fPixelsPerWorldUnit = fMapWidth ? fGenWidth / (pState->fMapScale * fMapWidth) : 1.0f;

	// Needs to be done before tick so icons are placed correctly.
	if (pState->iGrabbedX || pState->iGrabbedY)
	{
		S32 iDiffX = g_ui_State.mouseX - pState->iGrabbedX;
		S32 iDiffY = g_ui_State.mouseY - pState->iGrabbedY;
		pState->v3WorldCenter[0] = pState->v3DragStartTarget[0] - iDiffX / pState->fPixelsPerWorldUnit;
		pState->v3WorldCenter[2] = pState->v3DragStartTarget[2] + iDiffY / pState->fPixelsPerWorldUnit;
		// This is not right for skew - RMARR - TODO FIX
		CLAMPVEC3VEC3(pState->v3WorldCenter, pState->pReveal->v3RegionMin, pState->pReveal->v3RegionMax);
		inpHandled();
		pState->bFollowing = false;
		pState->hTarget = -1;
	}
	else if (!pState->pFakeZone || gConf.bShowMapIconsOnFakeZones)
	{
		bool bImmediate = !ui_GenInState(pGen, kUIGenStateVisible);

		if (!pMap->bFollowPlayer)
		{
			// Stop following once recentering is complete when not following player
			pState->bFollowing &= ABS(pState->v3Target[0] - pState->v3WorldCenter[0]) > 1
				|| ABS(pState->v3Target[1] - pState->v3WorldCenter[1]) > 1
				|| ABS(pState->v3Target[2] - pState->v3WorldCenter[2]) > 1;
		}
		if (pState->bFollowing)
		{
			if (bImmediate)
			{
				copyVec3(pState->v3Target, pState->v3WorldCenter);
			}
			else
			{
				F32 fWorldDist = g_ui_State.timestep * (CBoxWidth(&pGen->ScreenBox) * 2 / pState->fPixelsPerWorldUnit);
				pState->v3WorldCenter[0] += (pState->v3Target[0] - pState->v3WorldCenter[0]) * pMap->fFollowSpeed;
				pState->v3WorldCenter[1] += (pState->v3Target[1] - pState->v3WorldCenter[1]) * pMap->fFollowSpeed;
				pState->v3WorldCenter[2] += (pState->v3Target[2] - pState->v3WorldCenter[2]) * pMap->fFollowSpeed;
			}
		}
	}

	if (pMap->eMapZoomMode != UIGenMapZoomModeNone)
	{
		bool bHorizontal = false;
		F32 fRegionWidth = (pState->pReveal->v3RegionMax[0] - pState->pReveal->v3RegionMin[0]);
		F32 fRegionHeight = (pState->pReveal->v3RegionMax[2] - pState->pReveal->v3RegionMin[2]);
		F32 fHorizontalMinScale = fRegionWidth ? (CBoxWidth(&pGen->ScreenBox) / fRegionWidth) : 1;
		F32 fVerticalMinScale = fRegionHeight ? (CBoxHeight(&pGen->ScreenBox) / fRegionHeight) : 1;
		F32 fBest;
		Vec3 v3Min,v3Max;
		Vec2 v2Min,v2Max;
		F32 fHorizontalClampingPadding;
		F32 fVerticalClampingPadding;
		Vec2 vSkew = {gfCurrentMapOrthoSkewX,gfCurrentMapOrthoSkewZ};

		// Make sure scale is acceptable
		if (pMap->eMapZoomMode == UIGenMapZoomModeFilled)
		{
			fBest = max(fHorizontalMinScale, fVerticalMinScale);
		} 
		else
		{
			fBest = min(fHorizontalMinScale, fVerticalMinScale);
			bHorizontal = fHorizontalMinScale > fVerticalMinScale;
		}
		MAX1(pState->fPixelsPerWorldUnit, fBest);

		// Make sure position is acceptable. 
		copyVec3(pState->pReveal->v3RegionMin, v3Min);
		copyVec3(pState->pReveal->v3RegionMax, v3Max);

		mapSnapGetExtendedBounds(pState->pReveal->v3RegionMin,pState->pReveal->v3RegionMax,vSkew,s_fRegionBaseGroundHeight,v2Min,v2Max);

		fHorizontalClampingPadding = (fGenWidth/2.0) / pState->fPixelsPerWorldUnit;
		fVerticalClampingPadding = (fGenHeight/2.0) / pState->fPixelsPerWorldUnit;

		if (pMap->eMapZoomMode == UIGenMapZoomModeScaled && bHorizontal)
		{
			float diff = (fGenWidth / pState->fPixelsPerWorldUnit - fMapWidth) / 2.0;
			fHorizontalClampingPadding -= max(0, diff);
		}

		if (pMap->eMapZoomMode == UIGenMapZoomModeScaled && !bHorizontal)
		{
			float diff = (fGenHeight / pState->fPixelsPerWorldUnit - fMapHeight) / 2.0;
			fVerticalClampingPadding -= max(0, diff);
		}
		
		v3Min[0] = v2Min[0]+fHorizontalClampingPadding;
		v3Max[0] = v2Max[0]-fHorizontalClampingPadding;
		v3Min[2] = v2Min[1]+fVerticalClampingPadding;
		v3Max[2] = v2Max[1]-fVerticalClampingPadding;

		// adjust all the scrolling limits for skew
		v3Min[0] -= gfCurrentMapOrthoSkewX*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);
		v3Max[0] -= gfCurrentMapOrthoSkewX*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);
		v3Min[2] -= gfCurrentMapOrthoSkewZ*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);
		v3Max[2] -= gfCurrentMapOrthoSkewZ*(pState->v3WorldCenter[1]-s_fRegionBaseGroundHeight);

		CLAMPVEC3VEC3(pState->v3WorldCenter, v3Min, v3Max);
		
	}

	PERFINFO_AUTO_START("Map Icon Layout", eaSize(&pState->eaIcons));
	for (i = 0; i < eaSize(&pState->eaIcons); i++)
	{
		UIGenMapIcon *pIcon = pState->eaIcons[i];
		Vec2 v2ScreenPos;
		F32 fWidth = 0;
		F32 fHeight = 0;

		GenMapWorldPosToScreenPos(pGen, pIcon->v3WorldPos, v2ScreenPos, true);

		if (pIcon->pTex = GenMapIconLoadTexture(pIcon))
		{
			if (pIcon->pDef->bIgnoreWorldSize || (pIcon->v3WorldSize[0] == 0 && pIcon->v3WorldSize[2] == 0))
			{
				F32 fScale = pIcon->pDef->bScaleToWorld ? pState->fPixelsPerWorldUnit / pIcon->pDef->fPixelsPerWorldUnit : pGen->fScale;
				if (pIcon->pDef->bScale)
				{
					F32 fZoomScale = pIcon->pDef->fScaleMultiplier * (1 - (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit ? pState->fPixelsPerWorldUnit : pState->fMapScale) / pMap->fScaleMax); // TODO Fix this
					fScale *= MAX(pIcon->pDef->fMinScale, fZoomScale);
				}
				fScale *= pIcon->pDef->fBaseScale;
				fWidth = pIcon->pTex->width * fScale;
				fHeight = pIcon->pTex->height * fScale;
			}
			else
			{
				fWidth = pIcon->v3WorldSize[0] * pState->fPixelsPerWorldUnit * pIcon->pDef->fBaseScale;
				fHeight = pIcon->v3WorldSize[2] * pState->fPixelsPerWorldUnit * pIcon->pDef->fBaseScale;
			}
		}
		BuildCBox(&pIcon->ScreenBox, v2ScreenPos[0] - fWidth / 2, v2ScreenPos[1] - fHeight / 2, fWidth, fHeight);
	}

	PERFINFO_AUTO_START("Icon Pushing", 1);
	GenMapPushIcons(pState->eaIcons, &pGen->ScreenBox);
	PERFINFO_AUTO_STOP();

	for (i = 0; i < eaSize(&pState->eaIcons); i++)
	{
		UIGenMapIcon *pIcon = pState->eaIcons[i];
		Vec2 v2RadiusCenter = 
		{
			(pGen->ScreenBox.lx + pGen->ScreenBox.hx) / 2 + pMap->ClampRadius.v2Offset[0] * pGen->fScale, 
			(pGen->ScreenBox.ly + pGen->ScreenBox.hy) / 2 + pMap->ClampRadius.v2Offset[1] * pGen->fScale
		};
		CBox ClampBox = pGen->ScreenBox;
		ClampBox.lx += pMap->uiLeftIconPadding;
		ClampBox.hx -= pMap->uiRightIconPadding;
		ClampBox.ly += pMap->uiTopIconPadding;
		ClampBox.hy -= pMap->uiBottomIconPadding;
		if (pIcon->pDef->bClampToEdge)
		{
			GenMapClampIconToEdge(&pIcon->ScreenBox, &ClampBox, pMap->ClampRadius.fClampRadius * pGen->fScale, v2RadiusCenter);
		}
	}
	PERFINFO_AUTO_STOP();
}

static UIGenMapIcon *GenMapIconUnderMouse(CONST_EARRAY_OF(UIGenMapIcon) eaIcons)
{
	S32 iMouseX = g_ui_State.mouseX;
	S32 iMouseY = g_ui_State.mouseY;
	const Vec2 v2Mouse = {iMouseX, iMouseY};
	UIGenMapIcon *pBest = NULL;
	F32 fBestDist = 1e9;
	S32 iMaxZ = 0;
	S32 i;

	for (i = 0; i < eaSize(&eaIcons); i++)
	{
		UIGenMapIcon *pIcon = eaIcons[i];
		Vec2 v2Center;
		F32 fDist;
		CBoxGetCenter(&pIcon->ScreenBox, &v2Center[0], &v2Center[1]);
		fDist = distance2Squared(v2Mouse, v2Center);
		if ((!pBest || pIcon->pDef->iZ >= pBest->pDef->iZ)
			&& mouseCollision(&pIcon->ScreenBox)
			&& fDist < fBestDist)
		{
			fBestDist = fDist;
			pBest = pIcon;
		}
		pIcon->bMouseOver = false;
	}

	if (pBest && !inpCheckHandled())
		pBest->bMouseOver = true;
	return pBest;

}

bool GenMapFindGround(UIGenMapState *pState, Vec3 posInOut)
{
	WorldColl * wc = worldGetActiveColl(PARTITION_CLIENT);
	// If the map skews right as we go up, then a line from the ground to the player finger skews left as it goes up
	Vec3 vStep = {-gfCurrentMapOrthoSkewX,1.0f,-gfCurrentMapOrthoSkewZ};
	Vec3 vEndPos;
	Vec3 vIntersectUp,vIntersectDown;
	F32 fIntersectDistUp = 1e8f, fIntersectDistDown=1e8f;
	WorldCollCollideResults results;
	bool bHit = false;
	
	F32 fMaxHeight = pState->pReveal->v3RegionMax[1];
	F32 fMinHeight = pState->pReveal->v3RegionMin[1];

	F32 fCastUpDist = (fMaxHeight-posInOut[1]);
	F32 fCastDownDist = (posInOut[1]-fMinHeight);

	if (fCastUpDist > 0.0f)
	{
		scaleAddVec3(vStep,fCastUpDist,posInOut,vEndPos);
		if(wcRayCollide(wc, posInOut, vEndPos, WC_FILTER_BIT_MOVEMENT, &results)){
			fIntersectDistUp = results.posWorldImpact[1] - posInOut[1];
			copyVec3(results.posWorldImpact,vIntersectUp);
			bHit = true;
		}
	}

	if (fCastDownDist > 0.0f)
	{
		scaleAddVec3(vStep,-fCastDownDist,posInOut,vEndPos);
		if(wcRayCollide(wc, posInOut, vEndPos, WC_FILTER_BIT_MOVEMENT, &results)){
			fIntersectDistDown = posInOut[1]-results.posWorldImpact[1];
			copyVec3(results.posWorldImpact,vIntersectDown);
			bHit = true;
		}
	}

	if (!bHit)
		return false;

	if (fIntersectDistUp < fIntersectDistDown)
		copyVec3(vIntersectUp,posInOut);
	else
		copyVec3(vIntersectDown,posInOut);

	return true;
}

void ui_GenTickEarlyMap(UIGen *pGen)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	Entity *pEnt = entActivePlayerPtr();
	MapRevealInfo *pReveal = pState->pReveal;
	UIGenMapIcon *pIcon;
	bool bInputHandled = false;
	S32 iMouseX,iMouseY;

	if (!(pEnt && pReveal))
		return;

	mousePos(&iMouseX,&iMouseY);
	if (mouseClickHit(MS_WHEELUP, &pGen->ScreenBox) || mouseClickHit(MS_WHEELDOWN, &pGen->ScreenBox))
	{
		F32 fOldScale = pState->fMapScale;
		F32 fPixelsPerWorldUnit = (pState->fPixelsPerWorldUnit != 0 ? pState->fPixelsPerWorldUnit : .1);

		if (pMap->eScaleMode == UIGenMapScalePixelsPerWorldUnit)
			ui_GenMapSetPixelsPerWorldUnit(pGen, pState->fPixelsPerWorldUnit + mouseZ() * pMap->fScaleStep, true);
		else
			ui_GenMapSetScale(pGen, pState->fMapScale - mouseZ() * pMap->fScaleStep, true);

		pState->bFollowing = false;
		pState->hTarget = -1;

		pState->v3WorldCenter[0] += (1.0f-(pState->fMapScale/fOldScale))*((iMouseX-(pGen->ScreenBox.lx+pGen->ScreenBox.hx)*0.5f))/fPixelsPerWorldUnit;
		pState->v3WorldCenter[2] -= (1.0f-(pState->fMapScale/fOldScale))*((iMouseY-(pGen->ScreenBox.ly+pGen->ScreenBox.hy)*0.5f))/fPixelsPerWorldUnit;

		mouseCaptureZ();
	}

	if (pIcon = GenMapIconUnderMouse(pState->eaIcons))
	{
		if (!bInputHandled && (mouseDoubleClickHit(MS_LEFT, &pIcon->ScreenBox) && pIcon->pDef->pOnLeftDoubleClick))
		{
			GenMapIconRunAction(pGen, pIcon, pIcon->pDef->pOnLeftDoubleClick);
			bInputHandled = true;
		}
		if (!bInputHandled && (mouseClickHit(MS_LEFT, &pIcon->ScreenBox) && pIcon->pDef->pOnLeftClick))
		{
			GenMapIconRunAction(pGen, pIcon, pIcon->pDef->pOnLeftClick);
			bInputHandled = true;
		}
		if (!bInputHandled && (mouseClickHit(MS_RIGHT, &pIcon->ScreenBox) && pIcon->pDef->pOnRightClick))
		{
			GenMapIconRunAction(pGen, pIcon, pIcon->pDef->pOnRightClick);
			bInputHandled = true;
		}
	}

	{
		UIGenAction *pAction = NULL;
		S32 iX = 0;
		S32 iY = 0;

		if (!bInputHandled && mouseClickHit(MS_LEFT, &pGen->ScreenBox) && inpLevelPeek(INP_CONTROL))
		{
			// Maybe isProductionEditMode()?
			if (isDevelopmentMode() || entGetAccessLevel(entActivePlayerPtr()) >= ACCESS_GM)
			{
				Vec2 v2ClickPos;
				Vec3 v3StartPos;
				Vec3 v3BelowPos;
				Vec3 v3AbovePos;
				S32 iBelow = 0;
				S32 iAbove = 0;

				mouseDownPos(MS_LEFT, &iX, &iY);
				v2ClickPos[0] = iX;
				v2ClickPos[1] = iY;
				GenMapScreenPosToWorldPos(pGen, v2ClickPos, v3StartPos);

				// Find highest point below current ground level
				copyVec3(v3StartPos, v3BelowPos);
				worldSnapPosToGround(PARTITION_CLIENT, v3BelowPos, 7, -20000, &iBelow);

				// Find lowest point above current ground level
				copyVec3(v3StartPos, v3AbovePos);
				worldSnapPosToGround(PARTITION_CLIENT, v3AbovePos, 20000, -7, &iAbove);
				if (iAbove)
				{
					Vec3 v3LastAbove;
					copyVec3(v3AbovePos, v3LastAbove);
					while (v3AbovePos[1] > v3StartPos[1] + 1.01f)
					{
						copyVec3(v3StartPos, v3AbovePos);
						worldSnapPosToGround(PARTITION_CLIENT, v3AbovePos, v3LastAbove[1] - v3StartPos[1] - 1, -7, &iAbove);
						if (iAbove && v3AbovePos[1] > v3StartPos[1] + 1.01f)
							copyVec3(v3AbovePos, v3LastAbove);
					}
					copyVec3(v3LastAbove, v3AbovePos);
					iAbove = 1;
				}

				// Ctrl+Shift+Click = travel up from current location
				// Ctrl+Click = travel down from current location
				// Only relevant if there is an above and below destination
				if (iBelow && iAbove)
					ServerCmd_setpos(inpLevelPeek(INP_SHIFT) ? v3AbovePos : v3BelowPos);
				else if (iBelow || iAbove)
					ServerCmd_setpos(iBelow ? v3BelowPos : v3AbovePos);
				else
					ServerCmd_setpos(v3StartPos);
			}
		}

		if (!bInputHandled && (mouseDoubleClickHit(MS_LEFT, &pGen->ScreenBox) && pMap->pOnLeftDoubleClick))
		{
			mouseDownPos(MS_LEFT, &iX, &iY);
			pAction = pMap->pOnLeftDoubleClick;
		}
		else if (!bInputHandled && (mouseClickHit(MS_LEFT, &pGen->ScreenBox) && pMap->pOnLeftClick))
		{
			mouseDownPos(MS_LEFT, &iX, &iY);
			pAction = pMap->pOnLeftClick;
		}
		else if (!bInputHandled && (mouseClickHit(MS_RIGHT, &pGen->ScreenBox) && pMap->pOnRightClick))
		{
			mouseDownPos(MS_RIGHT, &iX, &iY);
			pAction = pMap->pOnRightClick;
		}

		if (pAction)
		{
			UIGenMapIcon Icon = {0};
			//S32 iFloor;
			Icon.ScreenBox.lx = Icon.ScreenBox.hx = iX;
			Icon.ScreenBox.ly = Icon.ScreenBox.hy = iY;
			GenMapScreenPosToWorldPos(pGen, Icon.ScreenBox.lowerRight, Icon.v3WorldPos);
			GenMapFindGround(pState,Icon.v3WorldPos);
			//worldSnapPosToGround(PARTITION_CLIENT, Icon.v3WorldPos, 7, -7, &iFloor);
			GenMapIconRunAction(pGen, &Icon, pAction);
			bInputHandled = true;
		}
	}

	if (!bInputHandled && (pMap->ePannable != MS_MOUSENONE) && (mouseDragHit(pMap->ePannable, &pGen->ScreenBox)))
	{
		mouseDownPos(pMap->ePannable, &pState->iGrabbedX, &pState->iGrabbedY);
		copyVec3(pState->v3WorldCenter, pState->v3DragStartTarget);
	}

	if ((pMap->ePannable != MS_MOUSENONE) && !mouseIsDown(pMap->ePannable) && (pState->iGrabbedX || pState->iGrabbedY))
	{
		pState->iGrabbedX = 0;
		pState->iGrabbedY = 0;
	}
}

static int SortMapTexPartitionsByMinY(const UIGenMapTexture **ppA, const UIGenMapTexture **ppB)
{
	const UIGenMapTexture *pA = *ppA;
	const UIGenMapTexture *pB = *ppB;
	if (pA->pPartition->bounds_min[1] < pB->pPartition->bounds_min[1])
		return -1;
	else if (pA->pPartition->bounds_min[1] > pB->pPartition->bounds_min[1])
		return 1;
	else
		return pA->pPartition - pB->pPartition;
}

static int SortMapTexPartitionsByDistFromCenter(const UIGenMapTexture **ppA, const UIGenMapTexture **ppB)
{
	const UIGenMapTexture *pA = *ppA;
	const UIGenMapTexture *pB = *ppB;
	return pA->fDistFromCenterSq - pB->fDistFromCenterSq > 0 ? 1 : -1;
}

static F32 GetMaxShowY(void)
{
	if(gProjectGameClientConfig.bUseMapMaxShowY)
	{
		return gProjectGameClientConfig.fMapMaxShowY;
	}

	return 10.0f;
}

static bool GenMapDrawTexture(UIGen *pGen, UIGenMap *pMap, UIGenMapState *pState, MapRevealInfo *pRevealInfo, UIGenMapTexture *pMapTex, F32 fMinScreenZ, F32 fMaxScreenZ)
{
	RoomPartition *pPartition = pMapTex->pPartition;
	F32 fScaleZ = (fMaxScreenZ - fMinScreenZ) / AVOID_DIV_0(pRevealInfo->v3RegionMax[1] - pRevealInfo->v3RegionMin[1]);
	fScaleZ = fabs(fScaleZ);

	if (pPartition->bounds_min[1] > pState->v3WorldCenter[1] + GetMaxShowY())
		return false;

	if (CBoxIntersects(&pMapTex->TexBox, &pGen->ScreenBox))
	{
		F32 fZ = (pPartition->bounds_min[1] - pRevealInfo->v3RegionMin[1]) * fScaleZ + fMinScreenZ;
		bool bIn = (pRevealInfo->eType == kMapRevealType_EnteredRooms)  ? pointBoxCollision(pState->v3Target, pPartition->bounds_min, pPartition->bounds_max) : true;
		U32 uiColor = ui_StyleColorPaletteIndex(bIn ? pMap->uiRoomActiveColor : pMap->uiRoomInactiveColor);

		if (!uiColor || s_bHideFogOfWar)
			uiColor = ui_StyleColorPaletteIndex(pMap->uiRoomActiveColor);
		if (!uiColor)
			uiColor = 0xFFFFFFFF;
		if ((pMap->uiRoomFogColor || gConf.bGenMapHideUndiscoveredRooms) && pRevealInfo && pRevealInfo->eType == kMapRevealType_EnteredRooms && pRevealInfo->eaiRevealed)
		{
			if (pMapTex->iRoomIndex >= 0 && !TSTB(pRevealInfo->eaiRevealed, pMapTex->iRoomIndex))
			{
				if (gConf.bGenMapHideUndiscoveredRooms)
					return true;
				else if (pMap->uiRoomFogColor)
					uiColor = ui_StyleColorPaletteIndex(pMap->uiRoomFogColor);
			}
		}
		uiColor = ColorRGBAMultiplyAlpha(uiColor, pGen->chAlpha);

		display_sprite_mask_boxes(pMapTex->pTex, NULL, pState->pMask, NULL, &pMapTex->TexBox, &pGen->ScreenBox, fZ, uiColor);
		if (s_bMapTexBoxDebug)
 			gfxDrawBox(pMapTex->TexBox.lx, pMapTex->TexBox.ly, pMapTex->TexBox.hx, pMapTex->TexBox.hy, UI_INFINITE_Z, ColorWhite);
		if (s_bMapTexBoxLayered)
			gfxDrawBox(pMapTex->TexBox.lx, pMapTex->TexBox.ly, pMapTex->TexBox.hx, pMapTex->TexBox.hy, fZ, ColorRed);
	}
	return true;
}

static void GenMapDrawRooms(UIGen *pGen, RoomConnGraph *pGraph, MapRevealInfo *pRevealInfo, F32 fMinScreenZ, F32 fMaxScreenZ)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	S32 i, j, k;
	CBox TexBox;
	static UIGenMapTexture **s_eaMapTexs;
	static UIGenMapTexture **s_eaHighResTexs;
	int iMapTexCount = 0;
	int iHighResTexCount = 0;
	Vec2 vScreenBoxCenter;
	Vec2 vScreenBoxHalfSize;

	if (!pGraph || !pRevealInfo)
		return;

	vScreenBoxCenter[0] = (pGen->ScreenBox.hx+pGen->ScreenBox.lx)*0.5f;
	vScreenBoxCenter[1] = (pGen->ScreenBox.hy+pGen->ScreenBox.ly)*0.5f;
	vScreenBoxHalfSize[0] = (pGen->ScreenBox.hx-pGen->ScreenBox.lx)*0.5f;
	vScreenBoxHalfSize[1] = (pGen->ScreenBox.hy-pGen->ScreenBox.ly)*0.5f;

	// Prepare low res textures
	for (i = 0; i < eaSize(&pGraph->rooms); i++)
	{
		Room *pRoom = pGraph->rooms[i];
		for (j = 0; j < eaSize(&pRoom->partitions); j++)
		{
			RoomPartition *pPartition = pRoom->partitions[j];
			AtlasTex *pTex = NULL;

			// Don't draw things from the floor above
			if (pPartition->bounds_min[1] > pState->v3WorldCenter[1] + GetMaxShowY())
				continue;

			if (pPartition->overview_tex)
				pTex = pPartition->overview_tex;
			else if (eaSize(&pPartition->tex_list) == 1)
				pTex = pPartition->tex_list[0];

			if (pTex)
			{
				Vec2 vMin, vMax;

				UIGenMapTexture *pMapTex = eaGetStruct(&s_eaMapTexs, parse_UIGenMapTexture, iMapTexCount++);
				pMapTex->pTex = pTex;
				pMapTex->pPartition = pPartition;
				pMapTex->pGraph = pGraph;
				pMapTex->iRoomIndex = i;

				GenMapMapPosToScreenPos(pGen,pPartition->mapSnapData.vMin, vMin);
				GenMapMapPosToScreenPos(pGen,pPartition->mapSnapData.vMax, vMax);

				pMapTex->TexBox.lx = vMin[0];
				pMapTex->TexBox.hx = vMax[0];
				pMapTex->TexBox.ly = vMin[1];
				pMapTex->TexBox.hy = vMax[1];
			}
		}
	}
	eaSetSizeStruct(&s_eaMapTexs, parse_UIGenMapTexture, iMapTexCount);
	if (iMapTexCount > 1)
		eaQSort(s_eaMapTexs, SortMapTexPartitionsByMinY);

	// Prepare high res textures
	for (i = 0; i < eaSize(&pGraph->rooms); i++)
	{
		Room *pRoom = pGraph->rooms[i];
		for (j = 0; j < eaSize(&pRoom->partitions); j++)
		{
			RoomPartition *pPartition = pRoom->partitions[j];
			int iOffsetX = 0;
			int iOffsetY = 0;
			Vec2 vTexScale;
			Vec2 vMin,vMax;

			// Don't draw things from the floor above
			if (pPartition->bounds_min[1] > pState->v3WorldCenter[1] + GetMaxShowY() || eaSize(&pPartition->tex_list) == 0)
				continue;

			// Already being used as a low res texture
			if (!pPartition->overview_tex && eaSize(&pPartition->tex_list) == 1)
				continue;

			GenMapMapPosToScreenPos(pGen,pPartition->mapSnapData.vMin, vMin);
			GenMapMapPosToScreenPos(pGen,pPartition->mapSnapData.vMax, vMax);

			TexBox.lx = vMin[0];
			TexBox.hx = vMax[0];
			TexBox.ly = vMin[1];
			TexBox.hy = vMax[1];

			// we need 2 scales in case the artists are stretching an override image
			vTexScale[0] = pPartition->mapSnapData.image_width ? CBoxWidth(&TexBox) / pPartition->mapSnapData.image_width : 1;
			vTexScale[1] = pPartition->mapSnapData.image_height ? CBoxHeight(&TexBox) / pPartition->mapSnapData.image_height : 1;

			for (k = 0; k < eaSize(&pPartition->tex_list); k++)
			{
				UIGenMapTexture *pMapTex = eaGetStruct(&s_eaHighResTexs, parse_UIGenMapTexture, iHighResTexCount++);
				float fBufferU,fBufferV;
				CBox outputbox;

				pMapTex->pTex = pPartition->tex_list[k];
				pMapTex->pPartition = pPartition;
				pMapTex->pGraph = pGraph;
				pMapTex->iRoomIndex = i;

				// These 2 lines are part of a half-implemented feature to clip a couple of pixels from the edge of each map snap for filtering or removing other artifacts
				// We turned out not to need it too badly, and the DX pixel math may be slightly off, making it very very hard to get right.  We can revisit if we have time in the future. [RMARR - 1/24/12]
				//fBufferU = fTexScale*2.0f;
				//fBufferV = fTexScale*2.0f;
				fBufferU = fBufferV = 0.0f;
 				BuildCBox(&pMapTex->TexBox, TexBox.lx + iOffsetX * vTexScale[0] - fBufferU, TexBox.hy - (iOffsetY + pMapTex->pTex->height) * vTexScale[1] + fBufferV, pMapTex->pTex->width * vTexScale[0] + 2.f*fBufferU, pMapTex->pTex->height * vTexScale[1] + 2.f*fBufferV);

				// Wrap boxes when they pass the edge of the map
				iOffsetX += pMapTex->pTex->width;
				if (iOffsetX >= pPartition->mapSnapData.image_width)
				{
					iOffsetX = 0;
					iOffsetY += pMapTex->pTex->height;
				}

				if (CBoxIntersectClip(&pMapTex->TexBox,&pGen->ScreenBox,&outputbox))
				{
					// choose textures that have the most pixels on-screen
					pMapTex->fDistFromCenterSq = -(outputbox.hx-outputbox.lx)*(outputbox.hy-outputbox.ly);

					// then choose textures that are toward the center
					pMapTex->fDistFromCenterSq -= (vScreenBoxHalfSize[0]-fabsf((outputbox.hx+outputbox.lx)*0.5f-vScreenBoxCenter[0]))*(vScreenBoxHalfSize[1]-fabsf((outputbox.hy+outputbox.ly)*0.5f-vScreenBoxCenter[1]))*0.01f;
				}
				else
				{
					pMapTex->fDistFromCenterSq = 0.0f;
				}
			}
		}
	}
	eaSetSizeStruct(&s_eaHighResTexs, parse_UIGenMapTexture, iHighResTexCount);
	if (iHighResTexCount > 1)
		eaQSort(s_eaHighResTexs, SortMapTexPartitionsByDistFromCenter);

	clipperPushRestrict(&pGen->ScreenBox);

	// Draw the Fog of War
	if (!s_bHideFogOfWar && pMap->uiFogColor && pRevealInfo && pRevealInfo->eType == kMapRevealType_Grid && pState->pFogOverlay)
	{
		F32 fMapTexWidth;
		F32 fMapTexHeight;
		F32 fScaleX = CBoxHeight(&pGen->ScreenBox)/texWidth(pState->pFogOverlay);
		F32 fScaleY = CBoxWidth(&pGen->ScreenBox)/texHeight(pState->pFogOverlay);
		F32 fDiff;
		U32 uiFogColor = ui_StyleColorPaletteIndex(pMap->uiFogColor);
		CBox DrawBox;
		GenMapWorldBoxToScreenBox(pGen, pRevealInfo->v3RegionMin, pRevealInfo->v3RegionMax, &TexBox);
		fMapTexWidth = CBoxWidth(&TexBox);
		fMapTexHeight = CBoxHeight(&TexBox);
		fDiff = CBoxWidth(&pGen->ScreenBox) - CBoxHeight(&pGen->ScreenBox);
		if (CBoxIntersectClip(&pGen->ScreenBox, &TexBox, &DrawBox))
		{
			clipperPushRestrict(&DrawBox);
			display_sprite_effect_ex(
				NULL, pState->pFogOverlay,
				pState->pMask, NULL,
				pGen->ScreenBox.lx + fDiff/2, pGen->ScreenBox.ly - fDiff/2,
				fMaxScreenZ, fScaleX, fScaleY,
				uiFogColor, uiFogColor, uiFogColor, uiFogColor,
				-(pGen->ScreenBox.hy - TexBox.ly)/fMapTexHeight, (pGen->ScreenBox.lx - TexBox.lx)/fMapTexWidth, 
				-(pGen->ScreenBox.ly - TexBox.ly)/fMapTexHeight, (pGen->ScreenBox.hx - TexBox.lx)/fMapTexWidth, 
				0.f, 0.f, 1.f, 1.f,
				-PI / 2, false, clipperGetCurrent(),
				GenMapShouldUseSmoothShader() ? RdrSpriteEffect_Smooth : 0, 1.f / texWidth(pState->pFogOverlay),
				false); 
			clipperPop();
		}
	}

	// Draw low res images
	if (s_bMapDrawLowRes)
	{
		for (i = 0; i < iMapTexCount; i++)
		{
			if (!GenMapDrawTexture(pGen, pMap, pState, pRevealInfo, s_eaMapTexs[i], fMinScreenZ, fMaxScreenZ))
				break;
		}
	}

	// draw high res images
	if (s_bMapDrawHighRes)
	{
		for (i = 0; i < MIN(iHighResTexCount, s_iMapMaxHighRes); i++)
		{
			if (!GenMapDrawTexture(pGen, pMap, pState, pRevealInfo, s_eaHighResTexs[i], fMinScreenZ, fMaxScreenZ))
				break;
		}
	}

	clipperPop();
}

// Draws the numerical index over the specified icon
static void GenMapDrawIconIndex(UIGen *pGen, UIGenMap *pMap, UIGenMapIcon *pIcon, F32 x, F32 y, bool bCenter)
{
	char pchLabel[11];
	UIStyleFont *pFont = GET_REF(pIcon->pDef->hLabelFont) ? GET_REF(pIcon->pDef->hLabelFont) : GET_REF(pMap->hLabelFont);
	F32 fWidth = 0;
	F32 fHeight = ui_StyleFontLineHeight(pFont, pGen->fScale);

	if(!pIcon || !pGen || !pMap || !pIcon->pchLabel || !pIcon->pchLabel[0] || pIcon->iKeyIndex <= 0 || !pIcon->pchIndexLabel || !pIcon->pchIndexLabel[0])
	{
		return;
	}

	quick_sprintf(pchLabel, 11, "%d", pIcon->iKeyIndex);

	//fWidth = ui_StyleFontWidth(pFont, pGen->fScale, (const char *)pchLabel);
	fWidth = ui_StyleFontWidth(pFont, pGen->fScale, (const char *)pIcon->pchIndexLabel);

	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	/*gfxfont_Print(
	x, y + (fHeight / 2),
	UI_GET_Z(),
	pGen->fScale, pGen->fScale,
	0,
	(const char *)pchLabel);*/
	gfxfont_Print(
		x - (bCenter ? fWidth / 2 : 0), y + (fHeight / 2),
		UI_GET_Z(),
		pGen->fScale, pGen->fScale,
		0,
		(const char *)pIcon->pchIndexLabel);
}

static void GenMapDrawMissionNumber(UIGen *pGen, UIGenMap *pMap, UIGenMapIcon *pIcon, F32 fScale)
{
	UIStyleFont *pFont = GET_REF(pIcon->pDef->hLabelFont) ? GET_REF(pIcon->pDef->hLabelFont) : GET_REF(pMap->hLabelFont);
	Entity *pEntity = NULL;
	MissionInfo *pMissionInfo = NULL;
	Mission *pMission = NULL;
	S32 missioncount, i;
	char number[32] = {0};
	F32 fWidth, fHeight;
	CBox Box = {0, 0, 0, 0};

	if(!pIcon->pchMission) return;
	missioncount = eaSize(&s_eaSortedMissions);
	if(missioncount == 0) return;

	pEntity = entActivePlayerPtr();
	pMissionInfo = mission_GetInfoFromPlayer(pEntity);
	pMission = mission_FindMissionFromRefString(pMissionInfo, pIcon->pchMission);

	for (i = 0; i < missioncount; i++)
	{
		if(s_eaSortedMissions[i] == pMission)
		{
			snprintf(number, sizeof(number), "%d", i + 1);
			break;
		}
	}

	if (pIcon->bMouseOver && GET_REF(pMap->hHighlightFont))
		pFont = GET_REF(pMap->hHighlightFont);
	if (pIcon->bMouseOver && GET_REF(pIcon->pDef->hHighlightFont))
		pFont = GET_REF(pIcon->pDef->hHighlightFont);

	fWidth = ui_StyleFontWidth(pFont, fScale, number);
	fHeight = ui_StyleFontLineHeight(pFont, fScale);

	CBoxSetX(&Box, 0, fWidth);
	CBoxSetY(&Box, 0, fHeight);

	CBoxMoveX(&Box, (pIcon->ScreenBox.lx + pIcon->ScreenBox.hx - fWidth) / 2);
	CBoxMoveY(&Box, (pIcon->ScreenBox.ly + pIcon->ScreenBox.hy - fHeight) / 2);

	if(Box.lx < pGen->ScreenBox.lx)
		CBoxMoveX(&Box, pGen->ScreenBox.lx);
	if(Box.hx > pGen->ScreenBox.hx)
		CBoxMoveX(&Box, pGen->ScreenBox.hx - fWidth);
	if(Box.ly < pGen->ScreenBox.ly)
		CBoxMoveY(&Box, pGen->ScreenBox.ly);
	if(Box.hy > pGen->ScreenBox.hy)
		CBoxMoveY(&Box, pGen->ScreenBox.hy - fHeight);

	ui_StyleFontUse(pFont, false, kWidgetModifier_None);
	gfxfont_Print(
		Box.lx, Box.hy,
		pIcon->bMouseOver ? UI_INFINITE_Z : UI_GET_Z(),
		fScale, fScale,
		/*flags=*/0,
		number);
}

static void GenMapDrawLabel(UIGen *pGen, UIGenMap *pMap, UIGenMapIcon *pIcon, F32 fScale)
{
	UIStyleFont *pFont = GET_REF(pIcon->pDef->hLabelFont) ? GET_REF(pIcon->pDef->hLabelFont) : GET_REF(pMap->hLabelFont);
	F32 fWidth = ui_StyleFontWidth(pFont, fScale, pIcon->pchLabel); // Lame.
	F32 fHeight = ui_StyleFontLineHeight(pFont, fScale);
	CBox Box = {0, 0, fWidth, fHeight};

	if (pIcon->bMouseOver && GET_REF(pMap->hHighlightFont))
		pFont = GET_REF(pMap->hHighlightFont);
	if (pIcon->bMouseOver && GET_REF(pIcon->pDef->hHighlightFont))
		pFont = GET_REF(pIcon->pDef->hHighlightFont);

	if (pIcon->eLabelAlignment & UIRight)
		CBoxMoveX(&Box, max(pIcon->ScreenBox.lx, pGen->UnpaddedScreenBox.lx) - fWidth);
	else if (pIcon->eLabelAlignment & UILeft)
		CBoxMoveX(&Box, min(pIcon->ScreenBox.hx, pGen->UnpaddedScreenBox.hx));
	else
		CBoxMoveX(&Box, (max(pIcon->ScreenBox.lx, pGen->UnpaddedScreenBox.lx) + min(pIcon->ScreenBox.hx, pGen->UnpaddedScreenBox.hx) - fWidth) / 2);

	if (pIcon->eLabelAlignment & UIBottom)
		CBoxMoveY(&Box, max(pIcon->ScreenBox.ly, pGen->UnpaddedScreenBox.ly) - fHeight);
	else if (pIcon->eLabelAlignment & UITop)
		CBoxMoveY(&Box, min(pIcon->ScreenBox.hy, pGen->UnpaddedScreenBox.hy));
	else
		CBoxMoveY(&Box, (max(pIcon->ScreenBox.ly, pGen->UnpaddedScreenBox.ly) + min(pIcon->ScreenBox.hy, pGen->UnpaddedScreenBox.hy) - fHeight) / 2);

	if (pIcon->bMouseOver)
	{
		if (Box.lx < 0)
			CBoxMoveX(&Box, 0);
		else if (Box.hx > g_ui_State.screenWidth)
			CBoxMoveX(&Box, g_ui_State.screenWidth - fWidth);

		if (Box.ly < 0)
			CBoxMoveY(&Box, 0);
		else if (Box.hy > g_ui_State.screenHeight)
			CBoxMoveX(&Box, g_ui_State.screenHeight - fHeight);
	}

	ui_StyleFontUse(pFont, false, kWidgetModifier_None);

	if (pMap->bShowMapKey && pIcon->v3WorldSize[0] != 0 && pIcon->v3WorldSize[2] != 0)
	{
		F32 fCenterX = 0;
		F32 fCenterY = 0;
		
		CBoxGetCenter(&pIcon->ScreenBox, &fCenterX, &fCenterY);
		{
			F32 fX = fCenterX - fWidth / 2;
			F32 fY = fCenterY - fHeight * 1;

			gfxfont_Print(
				fX, fY,
				pIcon->bMouseOver ? UI_INFINITE_Z : UI_GET_Z(),
				fScale, fScale,
				0,
				pIcon->pchLabel);
		}
	} 
	else
	{
		gfxfont_Print(
			Box.lx, Box.hy,
			pIcon->bMouseOver ? UI_INFINITE_Z : UI_GET_Z(),
			fScale, fScale,
			0,
			pIcon->pchLabel);
	}
}

static UIGenMapFakeZoneHighlight *GetHighlightForMap(
	const char *pchMap, SA_PARAM_NN_VALID UIGenMapFakeZone *pFakeZone, F32 fX, F32 fY)
{
	int j;
	int n = eaSize(&pFakeZone->pHighlights->eaHighlights);

	for(j=0; j<n; j++)
	{
		UIGenMapFakeZoneHighlight *pHighlight = pFakeZone->pHighlights->eaHighlights[j];
		if(stricmp(pHighlight->pchMap, pchMap)==0)
		{
			if(fX >= pHighlight->Source.fLeft && fX < pHighlight->Source.fLeft+pHighlight->Source.fWidth
				&& fY <= pHighlight->Source.fTop && fY > pHighlight->Source.fTop+pHighlight->Source.fHeight)
			{
				return pHighlight;
			}
		}
	}

	return NULL;
}

static UIGenMapFakeZoneHighlight *GetHighlight(UIGenMapFakeZone *pFakeZone, F32 *pX, F32 *pY, F32 *pRot)
{
	UIGenMapFakeZoneHighlight *pRet;
	Entity *pEnt = entActivePlayerPtr();

	if(pEnt && pEnt->pPlayer && pFakeZone && pFakeZone->pHighlights)
	{
		SavedMapDescription *pCurrentMap = entity_GetLastMap(pEnt);
		SavedMapDescription *pLastStaticMap = entity_GetLastStaticMap(pEnt);
		Vec3 v;

		entGetPos(pEnt, v);

		// First see if the current map has a highlight set
		pRet = GetHighlightForMap(zmapInfoGetPublicName(NULL), pFakeZone, v[0], v[2]);
		if( ( pRet == NULL ) && ( pLastStaticMap != NULL ) && ( pLastStaticMap != pCurrentMap ) )
		{
			// See if the last static map is a matching one
			v[0] = pLastStaticMap->spawnPos[0];
			v[2] = pLastStaticMap->spawnPos[2];

			pRet = GetHighlightForMap(pLastStaticMap->mapDescription, pFakeZone, v[0], v[2]);
		}

		if(pRet)
		{
			if(pX && pY)
			{
				*pX = v[0];
				*pX = ((*pX) - pRet->Source.fLeft)
					* (pRet->Mapped.fWidth)/(pRet->Source.fWidth)
					+ pRet->Mapped.fLeft;

				*pY = v[2];
				*pY = ((*pY) - pRet->Source.fTop)
					* (pRet->Mapped.fHeight)/(pRet->Source.fHeight)
					+ pRet->Mapped.fTop;
			}

			if (pRot)
			{
				Vec3 pyr;
				entGetFacePY(pEnt, pyr);
				*pRot = pyr[1];
			}

			return pRet;
		}

	}

	return NULL;
}

static void GenMiniMapDrawFakeZone(UIGen *pGen, UIGenMap *pMap, UIGenMapState *pState, UIGenMapFakeZone *pFakeZone)
{
	F32 fWidth = pFakeZone->v2WorldSize[0];
	F32 fHeight = pFakeZone->v2WorldSize[1];
	CBox Box = { 0, 0, fWidth * pState->fPixelsPerWorldUnit, fHeight * pState->fPixelsPerWorldUnit };
	BasicTexture *pSmallTex = texFindAndFlag(pFakeZone->pchSmallMap, 1, WL_FOR_UI);
	F32 fZ = UI_GET_Z();
	F32 fXCur, fYCur, fRot;
	UIGenMapFakeZoneHighlight *pHighlight;
	S32 i;

	CBoxMoveX(&Box, pGen->ScreenBox.lx + CBoxWidth(&pGen->ScreenBox) / 2
		- (pState->v3WorldCenter[0] - pState->pReveal->v3RegionMin[0]) * pState->fPixelsPerWorldUnit);
	CBoxMoveY(&Box, (pGen->ScreenBox.ly + CBoxHeight(&pGen->ScreenBox) / 2)
		- (pState->pReveal->v3RegionMax[2] - pState->v3WorldCenter[2]) * pState->fPixelsPerWorldUnit);

	clipperPushRestrict(&pGen->ScreenBox);

	if (CBoxWidth(&Box) < pSmallTex->width || CBoxHeight(&Box) < pSmallTex->height)
	{
		display_sprite_box2(pSmallTex, &Box, fZ, 0xFFFFFFFF);
	}
	else
	{
		S32 iRow;
		for (iRow = 0; iRow < pFakeZone->aiTileCount[1]; iRow++)
		{
			S32 iCol;
			for (iCol = 0; iCol < pFakeZone->aiTileCount[0]; iCol++)
			{
				static char *s_pchTexture;
				BasicTexture *pTex;
				F32 fTexWidth = CBoxWidth(&Box) / pFakeZone->aiTileCount[0];
				F32 fTexHeight = CBoxHeight(&Box) / pFakeZone->aiTileCount[1];
				CBox TexBox;
				estrClear(&s_pchTexture);
				FormatGameString(&s_pchTexture, pFakeZone->pchMapFormat,
					STRFMT_UGLYINT("Row", iRow + 1),
					STRFMT_UGLYINT("Column", iCol + 1),
					STRFMT_END);
				pTex = texFindAndFlag(s_pchTexture, 1, WL_FOR_UI);
				BuildCBox(&TexBox,
					Box.lx + iCol * fTexWidth,
					Box.ly + iRow * fTexHeight,
					fTexWidth,
					fTexHeight);
				if (pTex && CBoxIntersects(&pGen->ScreenBox, &TexBox))
					display_sprite_box2(pTex, &TexBox, fZ, 0xFFFFFFFF);
			}
		}
	}

	pHighlight = GetHighlight(pFakeZone, &fXCur, &fYCur, &fRot);
	if(pHighlight)
	{
		CBox TexBox;
		BasicTexture *pTex;
		F32 fXScale = (pFakeZone->v2WorldSize[0]/pFakeZone->pHighlights->v2Size[0]) * pState->fPixelsPerWorldUnit;
		F32 fYScale = (pFakeZone->v2WorldSize[1]/pFakeZone->pHighlights->v2Size[1]) * pState->fPixelsPerWorldUnit;

		pTex = texFindAndFlag("white", 1, WL_FOR_UI);

		BuildCBox(&TexBox,
			Box.lx + pHighlight->Highlight.fLeft * fXScale,
			Box.ly + pHighlight->Highlight.fTop * fYScale,
			pHighlight->Highlight.fWidth * fXScale,
			pHighlight->Highlight.fHeight * fYScale);

		if (pTex && CBoxIntersects(&pGen->ScreenBox, &TexBox))
			display_sprite_box2(pTex, &TexBox, UI_GET_Z(), ui_StyleColorPaletteIndex(pHighlight->uiColor));

		if(pFakeZone->pHighlights->Marker.pchIcon)
		{
			BasicTexture *pTexMarker;

			pTexMarker = texFindAndFlag(pFakeZone->pHighlights->Marker.pchIcon, 1, WL_FOR_UI);

			if(pTexMarker)
			{
				F32 w = pTexMarker->width;
				F32 h = pTexMarker->height;

				BuildCBox(&TexBox,
					Box.lx + fXCur * fXScale - w/2,
					Box.ly + fYCur * fYScale - h/2,
					w, h);

				if (pTexMarker && CBoxIntersects(&pGen->ScreenBox, &TexBox))
				{
					if (pFakeZone->pHighlights->Marker.bRotate)
						display_sprite_mask_boxes_rotated(NULL, pTexMarker, NULL, NULL, &TexBox, &TexBox, UI_GET_Z(), ui_StyleColorPaletteIndex(pFakeZone->pHighlights->Marker.uiColor), fRot);
					else
						display_sprite_box2(pTexMarker, &TexBox, UI_GET_Z(), ui_StyleColorPaletteIndex(pFakeZone->pHighlights->Marker.uiColor));
				}
			}
		}
	}

	// Draw all the fake zone highlights
	if (s_bDrawMapFakeZoneHighlights)
	{
		BasicTexture *pTex = texFindAndFlag("white", 1, WL_FOR_UI);
		UIGenMapFakeZoneHighlight *pMouseOverHighlight = NULL;
		S32 iMouseX, iMouseY;
		CBox DebugBox = {0};

		mousePos(&iMouseX, &iMouseY);

		for (i = 0; i < eaSize(&pFakeZone->pHighlights->eaHighlights); i++)
		{
			UIGenMapFakeZoneHighlight *pDebugHighlight = pFakeZone->pHighlights->eaHighlights[i];
			CBox TexBox;
			F32 fXScale = (pFakeZone->v2WorldSize[0]/pFakeZone->pHighlights->v2Size[0]) * pState->fPixelsPerWorldUnit;
			F32 fYScale = (pFakeZone->v2WorldSize[1]/pFakeZone->pHighlights->v2Size[1]) * pState->fPixelsPerWorldUnit;

			BuildCBox(&TexBox,
				Box.lx + pDebugHighlight->Highlight.fLeft * fXScale,
				Box.ly + pDebugHighlight->Highlight.fTop * fYScale,
				pDebugHighlight->Highlight.fWidth * fXScale,
				pDebugHighlight->Highlight.fHeight * fYScale);

			if (point_cbox_clsn(iMouseX, iMouseY, &TexBox))
			{
				DebugBox = TexBox;
				pMouseOverHighlight = pDebugHighlight;
			}

			if (pTex && CBoxIntersects(&pGen->ScreenBox, &TexBox))
				display_sprite_box2(pTex, &TexBox, UI_GET_Z(), ui_StyleColorPaletteIndex(pDebugHighlight->uiColor));
			if ((!pTex || pMouseOverHighlight == pDebugHighlight) && CBoxIntersects(&pGen->ScreenBox, &TexBox))
				gfxDrawCBox(&TexBox, UI_GET_Z(), colorFromRGBA(ui_StyleColorPaletteIndex(pDebugHighlight->uiColor)));
		}

		if (pMouseOverHighlight)
		{
			UIStyleFont *pFont = GET_REF(pMap->hLabelFont);
			F32 fX, fY;
			CBoxGetCenter(&DebugBox, &fX, &fY);
			ui_StyleFontUse(pFont, false, kWidgetModifier_None);
			gfxfont_Print(fX, fY, UI_GET_Z(), pGen->fScale, pGen->fScale, CENTER_XY, pMouseOverHighlight->pchMap);
		}
	}

	clipperPop();
}

typedef struct DebugDrawer {
	UIDebugDrawFunc func;
	void* userdata;
} DebugDrawer;

static DebugDrawer **s_eaDebugDrawers = NULL;

void ui_MapAddDebugDrawer(UIDebugDrawFunc func, void *userdata)
{
	int i;
	DebugDrawer *pDrawer = NULL;

	for(i=eaSize(&s_eaDebugDrawers)-1; i>=0; i--)
	{
		pDrawer = s_eaDebugDrawers[i];

		if(pDrawer->func==func && pDrawer->userdata==userdata)
			return;
	}

	pDrawer = callocStruct(DebugDrawer);
	pDrawer->func = func;
	pDrawer->userdata = userdata;

	eaPush(&s_eaDebugDrawers, pDrawer);
}

void ui_MapRemoveDebugDrawer(UIDebugDrawFunc func)
{
	int i;
	DebugDrawer *pDrawer = NULL;

	for(i=eaSize(&s_eaDebugDrawers)-1; i>=0; i--)
	{
		pDrawer = s_eaDebugDrawers[i];

		if(pDrawer->func==func)
		{
			free(pDrawer);
			eaRemoveFast(&s_eaDebugDrawers, i);
		}
	}
}

#define WORLD_X_TO_SCREEN_X(fScreenCenterX, fWorldCenterX, fX, fUnitScale) (fScreenCenterX - (fWorldCenterX - (fX)) / (fUnitScale))
#define WORLD_Z_TO_SCREEN_Y(fScreenCenterY, fWorldCenterZ, fZ, fUnitScale) (fScreenCenterY + (fWorldCenterZ - (fZ)) / (fUnitScale))
#define SCREEN_X_TO_WORLD_X(fScreenPosX, fScreenCenter, fWorldCenterX, fScale) (((fScreenPosX) - (fScreenCenter)) * (fScale) + (fWorldCenterX))
#define SCREEN_Y_TO_WORLD_Z(fScreenPosY, fScreenCenter, fWorldCenterZ, fScale) (((fScreenCenter) - (fScreenPosY)) * (fScale) + (fWorldCenterZ))

typedef struct DebugDrawUserData {
	F32 fScreenCenterX;
	F32 fScreenCenterY;
	F32 fZ;

	F32 fWorldCenterX;
	F32 fWorldCenterZ;

	F32 fMapScale;
} DebugDrawUserData;

static void ui_DrawMinimapDebugLine(Vec3 world_src, int color_src, Vec3 world_dst, int color_dst, DebugDrawUserData *userdata)
{
	F32 sX, sY, dX, dY;

	sX = WORLD_X_TO_SCREEN_X(userdata->fScreenCenterX,
		userdata->fWorldCenterX,
		world_src[0],
		userdata->fMapScale);
	sY = WORLD_Z_TO_SCREEN_Y(userdata->fScreenCenterY,
		userdata->fWorldCenterZ,
		world_src[2],
		userdata->fMapScale);

	dX = WORLD_X_TO_SCREEN_X(userdata->fScreenCenterX,
		userdata->fWorldCenterX,
		world_dst[0],
		userdata->fMapScale);
	dY = WORLD_Z_TO_SCREEN_Y(userdata->fScreenCenterY,
		userdata->fWorldCenterZ,
		world_dst[2],
		userdata->fMapScale);

	gfxDrawLineARGB2(sX, sY, userdata->fZ, dX, dY, color_src, color_dst);
}

static void ui_DrawMinimapDebugSphere(Vec3 pos, F32 radius, int color, DebugDrawUserData *data)
{

}

void ui_DrawMinimapDebug(UIGen *pGen, UIGenMap *pMap, UIGenMapState* pState)
{
	Entity* pPlayer = entActivePlayerPtr();
	DebugDrawUserData data = {0};
	int i;

	if(!pPlayer)
		return;

	// this is just a hack right now to not draw debug lines on the small minimap
	if(pMap->ClampRadius.fClampRadius != -1)
		return;

	data.fWorldCenterX = pState->v3WorldCenter[0];
	data.fWorldCenterZ = pState->v3WorldCenter[2];
	CBoxGetCenter(&pGen->ScreenBox, &data.fScreenCenterX, &data.fScreenCenterY);
	data.fZ = UI_GET_Z();
	data.fMapScale = pState->fMapScale;

	for(i=0; i<eaSize(&s_eaDebugDrawers); i++)
	{
		DebugDrawer *pDrawer = s_eaDebugDrawers[i];

		pDrawer->func(pPlayer, pDrawer->userdata, ui_DrawMinimapDebugLine, &data, ui_DrawMinimapDebugSphere, &data);
	}
}

//Sets the key index labels for the map icons based on their index and the indices of any overlapping icons
static void GenMapSetIconIndexLabels(UIGenMapIcon **eaIcons)
{
	UIGenMapIcon *pCurrentIcon = NULL;
	UIGenMapIcon *pTempIcon = NULL;
	char buf[256] = {0};
	int i;
	int j;

	

	for(i = 0; i < eaSize(&eaIcons); ++i)
	{
		pCurrentIcon = eaIcons[i];
		if(pCurrentIcon)
		{
			ea32Clear(&pCurrentIcon->eaIndices); //Clear out the indices
			ea32ClearFast(&pCurrentIcon->eaIndices);
			pCurrentIcon->bVisited = false;
		}
	}

	for(i = 0; i < eaSize(&eaIcons); ++i)
	{
		pCurrentIcon = eaIcons[i];
		if(pCurrentIcon)
		{
			if(pCurrentIcon->iKeyIndex <= 0 || !pCurrentIcon->pchLabel || !pCurrentIcon->pchLabel[0])
				continue;

			if(pCurrentIcon->bVisited)
				continue;

			ea32Insert(&pCurrentIcon->eaIndices, pCurrentIcon->iKeyIndex, 0);

			for(j = i + 1; j < eaSize(&eaIcons); ++j)
			{
				pTempIcon = eaIcons[j];
				if(pTempIcon)
				{
					if(pTempIcon->iKeyIndex <= 0 || !pTempIcon->pchLabel || !pTempIcon->pchLabel[0])
						continue;

					if(pTempIcon->bVisited)
						continue;

					if(pCurrentIcon->ScreenBox.hx == pTempIcon->ScreenBox.hx && //Do the icons occupy the same screen location
					   pCurrentIcon->ScreenBox.ly == pTempIcon->ScreenBox.ly)
					{

						if(pCurrentIcon->iKeyIndex == pTempIcon->iKeyIndex)
						{
							pTempIcon->iKeyIndex = -1;
						}
						else
						{
							U32 iInsertLocation = 0;
							ea32SortedFindIntOrPlace(&pCurrentIcon->eaIndices, pTempIcon->iKeyIndex, &iInsertLocation);
							ea32Insert(&pCurrentIcon->eaIndices, pTempIcon->iKeyIndex, iInsertLocation);

							pTempIcon->bVisited = true;
						}
					}
				}
			}

			if (ea32Size(&pCurrentIcon->eaIndices) == 0)
			{
				estrPrintf(&pCurrentIcon->pchIndexLabel, "%d", pCurrentIcon->iKeyIndex);
			} 
			else
			{
				estrPrintf(&pCurrentIcon->pchIndexLabel, "%d", pCurrentIcon->eaIndices[0]);
				for(j = 1; j < ea32Size(&pCurrentIcon->eaIndices); ++j)
				{
					sprintf(buf, ", %d", pCurrentIcon->eaIndices[j]);
					estrAppend2(&pCurrentIcon->pchIndexLabel, buf);
				}
			}
		}
	}
}

static int SortByZValue(const UIGenMapIcon **ppLeft, const UIGenMapIcon **ppRight, const void *pContext)
{
	return (*ppLeft)->pDef->iZ - (*ppRight)->pDef->iZ;
}

void ui_GenDrawEarlyMap(UIGen *pGen)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	UITextureAssembly *pTexAs = pMap->pMapAssembly ? ui_GenTextureAssemblyGetAssembly(pGen, &pMap->pMapAssembly->MapAssembly) : NULL;
	Entity *pEnt = entActivePlayerPtr();
	MapRevealInfo *pReveal = pState->pReveal;
	WorldRegion *pRegion = NULL;
	RoomConnGraph *pGraph = NULL;
	Vec3 v3PlayerPos;
	F32 fIconMinZ = 0;
	F32 fIconMaxZ;
	F32 fIconStepZ = 0;
	S32 iMaxZ = 0;
	S32 i;
	F32 fKeyX = 0;
	F32 fKeyY = 0;

	s_fRegionBaseGroundHeight = pState->v3WorldCenter[1];

	if (!(pEnt && pReveal))
		return;

	if (pState->pBackgroundTex)
		display_sprite_box_mask(pState->pBackgroundTex, pState->pMask, &pGen->ScreenBox, UI_GET_Z(), ui_StyleColorPaletteIndex(pMap->uiBackgroundColor));

	if (pState->pFakeZone)
	{
		PERFINFO_AUTO_START("Map Fake Zone", 1);
		GenMiniMapDrawFakeZone(pGen, pMap, pState, pState->pFakeZone);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		entGetPos(pEnt, v3PlayerPos);
		pRegion = worldGetWorldRegionByPos(v3PlayerPos);
		pGraph = worldRegionGetRoomConnGraph(pRegion);

		if (pRegion)
		{
			s_fRegionBaseGroundHeight = worldRegionGetMapSnapData(pRegion)->fGroundFocusHeight;
		}

		if (pGraph)
		{
			F32 fMinRoomZ = UI_GET_Z();
			F32 fMaxRoomZ = UI_GET_Z();
			PERFINFO_AUTO_START("Map Room", 1);
			GenMapDrawRooms(pGen, pGraph, pReveal, fMinRoomZ, fMaxRoomZ);
			PERFINFO_AUTO_STOP();
		}
	}

	if (!pState->pFakeZone || gConf.bShowMapIconsOnFakeZones)
	{
		UIGenMapIcon **sortedIconArray;

		for (i = 0; i < eaSize(&pState->eaIcons); i++)
		{
			UIGenMapIcon *pIcon = pState->eaIcons[i];
			MAX1(iMaxZ, pIcon->pDef->iZ);
		}
		if (pTexAs)
			MAX1(iMaxZ, pMap->pMapAssembly->iZ);

		fIconMinZ = UI_GET_Z();
		fIconMaxZ = UI_GET_Z();
		if (iMaxZ)
			fIconStepZ = (fIconMaxZ - fIconMinZ) / iMaxZ;
		else
			fIconStepZ = 0;
		PERFINFO_AUTO_START("Map Icons", eaSize(&pState->eaIcons));

		sortedIconArray = alloca(sizeof(UIGenMapIcon *) * eaSize(&pState->eaIcons));
		memcpy(sortedIconArray, pState->eaIcons, sizeof(UIGenMapIcon *) * eaSize(&pState->eaIcons));
		mergeSort(sortedIconArray, eaSize(&pState->eaIcons), sizeof(UIGenMapIcon *), NULL, SortByZValue);

		GenMapSetIconIndexLabels(pState->eaIcons);
		for (i = 0; i < eaSize(&pState->eaIcons); i++)
		{
			bool bRevealed;
			UIGenMapIcon *pIcon = sortedIconArray[i];
			U32 uiColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pIcon->pDef->uiColor), pGen->chAlpha);
			if (!(uiColor & 0xFF))
				continue;

			if (!pIcon->pTex)
				continue;

			if (CBoxIntersects(&pGen->UnpaddedScreenBox, &pIcon->ScreenBox))
			{
				bRevealed = true;
				if(pIcon->bHideUnlessRevealed)
				{
					PERFINFO_AUTO_START("mapRevealHasBeenRevealed", 1);
					bRevealed = mapRevealHasBeenRevealed(pState->pReveal, pIcon->v3WorldPos, s_fMapRevealLandmarkRadius, &pGraph->rooms);
					PERFINFO_AUTO_STOP();
				}

				if(bRevealed)
				{
					F32 fDiffY = pIcon->v3WorldPos[1] - pState->v3WorldCenter[1];
					F32 fAnnotateX = pIcon->ScreenBox.hx;
					F32 fAnnotateY = pIcon->ScreenBox.ly;
					F32 fZ = pIcon->pDef->iZ * fIconStepZ + fIconMinZ;
					bool bCenter = false;
					clipperPushRestrict(pIcon->pDef->bClip ? &pGen->ScreenBox : NULL);
					display_sprite_mask_boxes_rotated(
						pIcon->pTex, NULL, pIcon->pDef->bClip ? pState->pMask : NULL, NULL,
						&pIcon->ScreenBox, &pGen->ScreenBox, fZ,
						uiColor, pIcon->pDef->bRotate ? pIcon->fRotation : 0);

					// If this is an area rather than a point, put the annotation icon
					// into the center of it.
					if (!pIcon->pDef->bIgnoreWorldSize && pIcon->v3WorldSize[0] && pIcon->v3WorldSize[2])
					{
						CBoxGetCenter(&pIcon->ScreenBox, &fAnnotateX, &fAnnotateY);
						bCenter= true;
					}

					fKeyX = fAnnotateX;
					fKeyY = fAnnotateY;

					if (fDiffY > pMap->fRangeY && pState->pUpwardIcon)
						display_sprite(
						pState->pUpwardIcon,
						fKeyX = fAnnotateX - pState->pUpwardIcon->width * pGen->fScale / 2,
						fKeyY = fAnnotateY - pState->pUpwardIcon->height * pGen->fScale / 2,
						fZ + fIconStepZ / 2,
						pGen->fScale, pGen->fScale, ui_StyleColorPaletteIndex(pMap->uiUpwardIconColor));
					else if (fDiffY < -pMap->fRangeY && pState->pDownwardIcon)
						display_sprite(
						pState->pDownwardIcon,
						fKeyX = fAnnotateX - pState->pDownwardIcon->width * pGen->fScale / 2,
						fKeyY = fAnnotateY - pState->pDownwardIcon->height * pGen->fScale / 2,
						fZ + fIconStepZ / 2,
						pGen->fScale, pGen->fScale, ui_StyleColorPaletteIndex(pMap->uiDownwardIconColor));

					if ((pIcon->pDef->bAlwaysShowLabel || pIcon->bMouseOver)
						&& pIcon->pchLabel && *pIcon->pchLabel)
					{
						if (pIcon->bMouseOver)
							clipperPush(NULL);
						GenMapDrawLabel(pGen, pMap, pIcon, pGen->fScale);
						if (pIcon->bMouseOver)
							clipperPop();
					}
					CBoxGetCenter(&pIcon->ScreenBox, &fKeyX, &fKeyY);
					if(pMap->bShowMapKey && !pIcon->bVisited)
					{
						GenMapDrawIconIndex(pGen, pMap, pIcon, fKeyX + (bCenter ? 0 :UI_GEN_MAP_ICON_INDEX_OFFSET), fKeyY, bCenter);
					}

					if(s_bMapDrawMissionNumbers && pMap->bShowMissionNumbers && pIcon->pchMission)
					{
						if (pIcon->bMouseOver)
							clipperPush(NULL);
						GenMapDrawMissionNumber(pGen, pMap, pIcon, pGen->fScale);
						if (pIcon->bMouseOver)
							clipperPop();
					}

					clipperPop();
				}
			}
		}
		PERFINFO_AUTO_STOP();
	}

	if (pTexAs)
	{
		ui_TextureAssemblyDraw(
			pTexAs, 
			&pGen->UnpaddedScreenBox, NULL, 
			pGen->fScale, 
			pMap->pMapAssembly->iZ * fIconStepZ + fIconMinZ, 
			(pMap->pMapAssembly->iZ + 1) * fIconStepZ + fIconMinZ, 
			pGen->chAlpha, 
			&pMap->pMapAssembly->MapAssembly.Colors);
	}

	if (!pState->pFakeZone)
	{
		if(eaSize(&s_eaDebugDrawers))
		{
			ui_DrawMinimapDebug(pGen, pMap, pState);
		}
	}
}

void ui_GenHideMap(UIGen *pGen)
{
	UIGenMap *pMap = UI_GEN_RESULT(pGen, Map);
	UIGenMapState *pState = UI_GEN_STATE(pGen, Map);
	if (pState)
	{
		//If the user has changed the zoom and we haven't already saved it, we should do so here
		if (pMap && pMap->bRememberScales && pState->fSaveScaleTimer > 0)
		{
			Entity *pEnt = entActivePlayerPtr();
			if(pEnt)
				ServerCmd_gslSetMapRegionScale(entGetWorldRegionTypeOfEnt(pEnt), pState->fPixelsPerWorldUnit);
		}
		eaClearStruct(&pState->eaIcons, parse_UIGenMapIcon);
		pState->pBackgroundTex = NULL;
		pState->pMask = NULL;
		pState->fMapScale = 0;
		GenMapFreeFogTexture(pState);
	}
}

//Sets the gen list for the map key
void gclGenGetMapKeyIcons(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenMapKeyIcon ***peaUIMapKeyIcons = ui_GenGetManagedListSafe(pGen, UIGenMapKeyIcon);
	S32 iCount = 0;

	FOR_EACH_IN_EARRAY_FORWARDS(s_eaMapKeyIcons, UIGenMapKeyIcon, pCurIcon)
	{
		UIGenMapKeyIcon *pNewIcon = eaGetStruct(peaUIMapKeyIcons, parse_UIGenMapKeyIcon, iCount++);
		StructCopyAll(parse_UIGenMapKeyIcon, pCurIcon, pNewIcon);
	}
	FOR_EACH_END
	eaSetSizeStruct(peaUIMapKeyIcons, parse_UIGenMapKeyIcon, iCount);
	ui_GenSetManagedListSafe(pGen, peaUIMapKeyIcons, UIGenMapKeyIcon, true);
}

//Sorts map icons based on position first and name second
static int SortMapIconsByLocation(const UIGenMapIcon **ppIconA, const UIGenMapIcon **ppIconB)
{
	if(ppIconA && ppIconB) 
	{
		const UIGenMapIcon *pIconA = *ppIconA;
		const UIGenMapIcon *pIconB = *ppIconB;

		if(pIconA && pIconB) 
		{
			F32 fDiff = pIconB->v3WorldPos[2] - pIconA->v3WorldPos[2];
			if (fDiff != 0)
			{
				return fDiff;
			} 
			else
			{
				fDiff = pIconB->v3WorldPos[0] - pIconA->v3WorldPos[0];
				if (fDiff != 0)
				{
					return fDiff;
				} 
				else
				{
					fDiff = pIconB->v3WorldPos[1] - pIconA->v3WorldPos[1];
					if (fDiff != 0)
					{
						return fDiff;
					} 
					else 
					{
						fDiff = pIconB->iKeyIndex - pIconA->iKeyIndex;
						return fDiff;
					}
				}
			}
		}
	}
	return 0;
}

//Copies the location of the next map icon with the same key index as the passed key icon
//Returns success
bool GenMapGetNextPositionForKeyIndex(UIGen *pGen, UIGenMapKeyIcon *pIcon, Vec3 *pPos)
{
	static S32 s_i = -1;
	S32 iStart = s_i++;

	if(pGen && pIcon && pPos)
	{
		if(UI_GEN_IS_TYPE(pGen, kUIGenTypeMap))
		{
			UIGenMapState *pState = UI_GEN_STATE(pGen, Map);

			if(pState && pState->eaIcons)
			{
				eaQSort(pState->eaIcons, SortMapIconsByLocation); // We need to sort to get consistent results

				while(s_i != iStart)
				{
					if(s_i >= eaSize(&pState->eaIcons))
					{
						s_i = 0;
					}

					if(pState->eaIcons[s_i] && pState->eaIcons[s_i]->iKeyIndex == pIcon->iKeyIndex)
					{
						copyVec3(pState->eaIcons[s_i]->v3WorldPos, *pPos);
						return true;
					}

					s_i++;
				}

				if(s_i >= eaSize(&pState->eaIcons))
				{
					s_i = 0;
				}

				if(pState->eaIcons[s_i] &&  pState->eaIcons[s_i]->iKeyIndex == pIcon->iKeyIndex)
				{
					copyVec3(pState->eaIcons[s_i]->v3WorldPos, *pPos);
					return true;
				}
			}
		}
	}
	return false;
}


AUTO_RUN;
void ui_GenMap_Register(void)
{
	s_pchMapIcon = allocAddString("MapIcon");

	ui_GenInitStaticDefineVars(MapIconInfoTypeEnum, "MapIcon_");
	ui_GenInitStaticDefineVars(ZoneMapTypeEnum, "ZoneMapType_");
	ui_GenInitStaticDefineVars(MinimapWaypointTypeEnum, "WaypointType_");

	ui_GenInitPointerVar(s_pchMapIcon, parse_UIGenMapIcon);

	ui_GenRegisterType(kUIGenTypeMap,
		UI_GEN_NO_VALIDATE,
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateMap,
		UI_GEN_NO_LAYOUTEARLY, 
		ui_GenLayoutLateMap,
		ui_GenTickEarlyMap, 
		UI_GEN_NO_TICKLATE,
		ui_GenDrawEarlyMap, 
		UI_GEN_NO_FITCONTENTSSIZE, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideMap, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);

	MP_CREATE(UIGenMapEntityIconDef, 16);
	MP_CREATE(UIGenMapNodeIconDef, 16);
	MP_CREATE(UIGenMapWaypointIconDef, 16);
	MP_CREATE(UIGenMapIcon, 64);
}
