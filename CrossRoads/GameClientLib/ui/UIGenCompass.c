#include "earray.h"
#include "StringUtil.h"

#include "GfxCamera.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"

#include "inputMouse.h"

#include "EntityLib.h"
#include "Entity.h"
#include "mission_common.h"
#include "gclEntity.h"
#include "EntitySavedData.h"
#include "Team.h"
#include "WorldGrid.h"
#include "MapDescription.h"
#include "Player.h"
#include "contact_common.h"

#include "UIGenCompass.h"

#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenCompass_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_GenUpdateCompass(UIGen *pGen)
{
	UIGenCompass *pCompass = UI_GEN_RESULT(pGen, Compass);
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	Entity *pPlayer = entActivePlayerPtr();
	S32 iCount = 0;
	SavedMapDescription *pMap;
	bool bMissionMap = (zmapInfoGetMapType(NULL) == ZMTYPE_MISSION);

	UI_GEN_LOAD_TEXTURE(pCompass->pchCompassBackground, pState->pCompassBackground);
	UI_GEN_LOAD_TEXTURE(pCompass->pchUpwardIcon, pState->pUpwardIcon);
	UI_GEN_LOAD_TEXTURE(pCompass->pchDownwardIcon, pState->pDownwardIcon);
	UI_GEN_LOAD_TEXTURE(pCompass->pchCompassNotch, pState->pCompassNotch);

	pMap = entity_GetLastMap(pPlayer);
	if (pPlayer && pPlayer->pSaved && pMap)
	{
		S32 i;
		Team *pTeam = team_GetTeam(pPlayer);
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayer);
		const char *pchMyMap = zmapInfoGetPublicName(NULL);
		U32 iMapInstance = pMap->mapInstanceIndex;
		Vec3 v3PlayerPos;

		entGetPos(pPlayer, v3PlayerPos);

		if (pTeam)
		{
			for (i = 0; i < eaSize(&pTeam->eaMembers); i++)
			{
				TeamMember *pMember = pTeam->eaMembers[i];
				Entity *pEnt = GET_REF(pMember->hEnt);
				bool bIsLeader = pMember->iEntID == (pTeam->pLeader ? pTeam->pLeader->iEntID : 0);
				UIGenCompassIcon *pIcon = NULL;

				// If this member is me, or not on my map, ignore it.
				if (!pEnt
					|| pMember->iEntID == entGetContainerID(pPlayer)
					|| stricmp_safe(pMember->pcMapName, pchMyMap))
					continue;

				if (!(pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID)))
					continue;

				if (bIsLeader && pCompass->pchTeamLeaderIcon)
				{
					pIcon = eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
					UI_GEN_LOAD_TEXTURE(pCompass->pchTeamLeaderIcon, pIcon->pIcon);
					pIcon->uiColor = pCompass->uiTeamLeaderColor;
					estrClear(&pIcon->estrOwnedName);
				}
				else if (pCompass->pchTeammateIcon)
				{
					pIcon = eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
					UI_GEN_LOAD_TEXTURE(pCompass->pchTeammateIcon, pIcon->pIcon);
					pIcon->uiColor = pCompass->uiTeammateColor;
					estrClear(&pIcon->estrOwnedName);
				}
				else
					continue;

				entGetPos(pEnt, pIcon->v3Location);
				pIcon->pchName = pMember->pcName;
				if (pMember->iMapInstanceNumber != iMapInstance)
					pIcon->uiColor = (pIcon->uiColor & ~0xFF) + (pIcon->uiColor & 0xFF) / 2;
			}
		}

		// Personal saved waypoints
		for (i = 0; i < eaSize(&pPlayer->pPlayer->ppMyWaypoints); i++)
		{
			MinimapWaypoint *pWaypoint = pPlayer->pPlayer->ppMyWaypoints[i];
			UIGenCompassIcon *pIcon = NULL;

			// If we're inside the waypoint or otherwise very close, don't display it.
			if (distance3Squared(pWaypoint->pos, v3PlayerPos) < UI_GEN_COMPASS_MIN_WAYPOINT_DISTANCE * UI_GEN_COMPASS_MIN_WAYPOINT_DISTANCE)
				continue;

			if (pCompass->pchSavedWaypointIcon && pWaypoint->type == MinimapWaypointType_SavedWaypoint)
			{
				pIcon = eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
				UI_GEN_LOAD_TEXTURE(pCompass->pchSavedWaypointIcon, pIcon->pIcon);
				pIcon->uiColor = pCompass->uiSavedWaypointColor;
				estrClear(&pIcon->estrOwnedName);
			}
			else
				continue;

			copyVec3(pWaypoint->pos, pIcon->v3Location);
			if (TranslateMessageRef(pWaypoint->hDisplayNameMsg))
				pIcon->pchName = TranslateMessageRef(pWaypoint->hDisplayNameMsg);
			else
				pIcon->pchName = pWaypoint->pchDescription;
		}

		// Mission-related waypoints
		if (pInfo)
		{
			const char *pchPrimary = entGetPrimaryMission(pPlayer);
			MissionDef *pPrimaryDef = pchPrimary ? missiondef_DefFromRefString(pchPrimary) : NULL;
			MissionDef *pPrimaryParentDef = pPrimaryDef;
			UIGenCompassIcon *pClosestIcon = NULL;
			F32 fClosestDist = 0.0f;
			while (pPrimaryParentDef && GET_REF(pPrimaryParentDef->parentDef))
				pPrimaryParentDef = GET_REF(pPrimaryParentDef->parentDef);
			for (i = 0; i < eaSize(&pInfo->waypointList); i++)
			{
#define MAX_MISSION_NAME_LEN 1024
				MinimapWaypoint *pWaypoint = pInfo->waypointList[i];
				UIGenCompassIcon *pIcon = NULL;
				F32 fRadius = min(pWaypoint->fXAxisRadius, pWaypoint->fYAxisRadius);
				F32 fDist = distance3Squared(pWaypoint->pos, v3PlayerPos);
				char pchWaypointMissionName[MAX_MISSION_NAME_LEN] = {0};
				if (pWaypoint->pchMissionRefString)
				{
					char* pchColon = NULL;
					strcpy_s(pchWaypointMissionName, MAX_MISSION_NAME_LEN, pWaypoint->pchMissionRefString);
					pchColon = strchr(pchWaypointMissionName, ':');
					if (pchColon)
						*pchColon = '\0';
				}
#undef MAX_MISSION_NAME_LEN

				MAX1(fRadius, UI_GEN_COMPASS_MIN_WAYPOINT_DISTANCE);

				// If we're inside the waypoint or otherwise very close, don't display it.
				if (fDist < fRadius * fRadius)
					continue;

				// If we've already setup a closer icon for this mission, do not show this one.
				if (pCompass->bShowClosestMissionIconOnly && pClosestIcon && fDist > fClosestDist)
					continue;

				if (pPrimaryParentDef && pCompass->pchActiveMissionIcon
					&& !stricmp_safe(pchWaypointMissionName, pPrimaryParentDef->name)
					&& (pWaypoint->type != MinimapWaypointType_MissionReturnContact
					&& pWaypoint->type != MinimapWaypointType_MissionRestartContact
					&& pWaypoint->type != MinimapWaypointType_TrackedContact))
				{
					pIcon = pClosestIcon ? pClosestIcon : eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
					if(pCompass->bShowClosestMissionIconOnly)
					{
						pClosestIcon = pIcon;
						fClosestDist = fDist;
					}
					UI_GEN_LOAD_TEXTURE(pCompass->pchActiveMissionIcon, pIcon->pIcon);
					pIcon->uiColor = pCompass->uiActiveMissionColor;
					pIcon->pchName = NULL;
					estrClear(&pIcon->estrOwnedName);
				}
				else if(gConf.bShowOpenMissionInCompass && pCompass->pchActiveMissionIcon && bMissionMap && pWaypoint->type == MinimapWaypointType_OpenMission)
				{
					pIcon = pClosestIcon ? pClosestIcon : eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
					if(pCompass->bShowClosestMissionIconOnly)
					{
						pClosestIcon = pIcon;
						fClosestDist = fDist;
					}
					UI_GEN_LOAD_TEXTURE(pCompass->pchActiveMissionIcon, pIcon->pIcon);
					pIcon->uiColor = pCompass->uiActiveMissionColor;
					pIcon->pchName = NULL;
					estrClear(&pIcon->estrOwnedName);
				}
				else if (pCompass->pchContactIcon && pWaypoint->pchContactName
					&& (pWaypoint->type == MinimapWaypointType_MissionReturnContact
						|| pWaypoint->type == MinimapWaypointType_MissionRestartContact
						|| pWaypoint->type == MinimapWaypointType_TrackedContact))
				{
					pIcon = pClosestIcon ? pClosestIcon : eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
					if(pCompass->bShowClosestMissionIconOnly)
					{
						pClosestIcon = pIcon;
						fClosestDist = fDist;
					}
					UI_GEN_LOAD_TEXTURE(pCompass->pchContactIcon, pIcon->pIcon);
					pIcon->uiColor = pCompass->uiContactColor;
					pIcon->pchName = NULL;
					estrClear(&pIcon->estrOwnedName);
				}
				else if (pCompass->pchMissionIcon && pWaypoint->pchMissionRefString)
				{
					pIcon = pClosestIcon ? pClosestIcon : eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
					if(pCompass->bShowClosestMissionIconOnly)
					{
						pClosestIcon = pIcon;
						fClosestDist = fDist;
					}
					UI_GEN_LOAD_TEXTURE(pCompass->pchMissionIcon, pIcon->pIcon);
					pIcon->uiColor = pCompass->uiMissionColor;
					pIcon->pchName = NULL;
					estrClear(&pIcon->estrOwnedName);
				}
				else
					continue;

				copyVec3(pWaypoint->pos, pIcon->v3Location);

				if(!pIcon->pchName)
				{
					if(pWaypoint->pchContactName)
					{
						S32 j;
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
									pIcon->pchName = entGetLocalName(pContactEnt);
							}
						}
					}
				}

				if (!pIcon->pchName)
				{
					if (TranslateMessageRef(pWaypoint->hDisplayNameMsg))
						pIcon->pchName = TranslateMessageRef(pWaypoint->hDisplayNameMsg);
					else
						pIcon->pchName = pWaypoint->pchDescription;
				}

				if (!pIcon->pchName)
				{
					MissionDef *pDef = missiondef_DefFromRefString(pWaypoint->pchMissionRefString);
					while (pDef && GET_REF(pDef->parentDef))
					{
						pDef = GET_REF(pDef->parentDef);
					}
					if (pDef)
					{
						const char* pchMissionDisplayName = TranslateMessageRefDefault(pDef->displayNameMsg.hMessage, pWaypoint->pchDescription);
						if (StringStripTagsPrettyPrintEx(pchMissionDisplayName, &pIcon->estrOwnedName, false))
						{
							pIcon->pchName = pIcon->estrOwnedName;
						}
						else
						{
							pIcon->pchName = pchMissionDisplayName;
						}
					}
				}
			}
		}

		// Personal flagged entities / locations
		for (i = eaSize(&pState->eaFlagged) - 1; i >= 0; i--)
		{
			UIGenCompassFlagged *pFlagged = pState->eaFlagged[i];
			UIGenCompassIcon *pIcon = NULL;
			const char *pchName = NULL;
			if (pFlagged->hEntity)
			{
				Entity *pEnt = entFromEntityRefAnyPartition(pFlagged->hEntity);
				if (!pEnt)
				{
					eaRemove(&pState->eaFlagged, i);
					StructDestroy(parse_UIGenCompassFlagged, pFlagged);
					continue;
				}
				else
				{
					entGetPos(pEnt, pFlagged->v3Location);
					pchName = entGetLocalName(pEnt);
				}
			}

			pIcon = eaGetStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount++);
			UI_GEN_LOAD_TEXTURE(pCompass->pchFlaggedIcon, pIcon->pIcon);
			pIcon->uiColor = pCompass->uiFlaggedColor;
			pIcon->pchName = pchName;
			estrClear(&pIcon->estrOwnedName);
		}

		eaSetSizeStruct(&pState->eaIcons, parse_UIGenCompassIcon, iCount);
	}
	else
	{
		eaDestroyStruct(&pState->eaFlagged, parse_UIGenCompassFlagged);
		eaDestroyStruct(&pState->eaIcons, parse_UIGenCompassIcon);
	}
}

void ui_GenLayoutLateCompass(UIGen *pGen)
{
	UIGenCompass *pCompass = UI_GEN_RESULT(pGen, Compass);
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	Entity *pPlayer = entActivePlayerPtr();
	Vec3 v3CamPos = {0, 0, 0};
	Vec3 v3CamPYR = {0, 0, 0};
	F32 fPixelPerRadian;
	F32 fCenterX = (pGen->ScreenBox.lx + pGen->ScreenBox.hx) / 2;
	F32 fCenterY = (pGen->ScreenBox.ly + pGen->ScreenBox.hy) / 2;
	S32 i;
	
	if (pPlayer)
		entGetPos(pPlayer, v3CamPos);
	else
		gfxGetActiveCameraPos(v3CamPos);
	gfxGetActiveCameraYPR(v3CamPYR);

	if (pState->pCompassBackground)
	{
		// Background starts at Y = 0 = south, goes 720 degrees total,
		// We should align based on the center.
		F32 fCamRadians = v3CamPYR[1];
		F32 fTexWidth = pState->pCompassBackground->width * pGen->fScale;
		fPixelPerRadian = fTexWidth / (4 * PI);
		pState->iOffset = floorf(fCamRadians * fPixelPerRadian);
	}
	else
	{
		fPixelPerRadian = CBoxWidth(&pGen->ScreenBox) / PI; // lacking a texture, assume a 180 FOV
		pState->iOffset = 0;
	}

	for (i = 0; i < eaSize(&pState->eaIcons); i++)
	{
		UIGenCompassIcon *pIcon = pState->eaIcons[i];
		F32 fDiffX = v3CamPos[0] - pIcon->v3Location[0];
		F32 fDiffY = v3CamPos[1] - pIcon->v3Location[1];
		F32 fDiffZ = v3CamPos[2] - pIcon->v3Location[2];
		F32 fDiffXZ = sqrt(fDiffX * fDiffX + fDiffZ * fDiffZ);
		F32 fYaw = atan2(fDiffX, fDiffZ); // world angle from camera to target
		F32 fYawDiff = fYaw - v3CamPYR[1]; // difference between angle to target and camera view
		F32 fPitch = atan2(fDiffY, fDiffXZ);
		F32 fPitchDiff = fPitch - v3CamPYR[0];
		if (!pIcon->pIcon)
			continue;

		if (fYawDiff < -PI)
			fYawDiff += 2 * PI;
		else if (fYawDiff > PI)
			fYawDiff -= 2 * PI;
		if (fPitchDiff < -PI)
			fPitchDiff += 2 * PI;
		else if (fPitchDiff > PI)
			fPitchDiff -= 2 * PI;

		BuildCBox(&pIcon->ScreenBox, 0, 0, pIcon->pIcon->width * pGen->fScale, pIcon->pIcon->height * pGen->fScale);
		CBoxSetCenter(&pIcon->ScreenBox, floorf(fCenterX + fYawDiff * fPixelPerRadian), floorf(fCenterY));
		if(pCompass->bClampIcons)
		{
			if(pIcon->ScreenBox.left < pGen->ScreenBox.left)
				CBoxMoveX(&pIcon->ScreenBox, pGen->ScreenBox.left);
			else if(pIcon->ScreenBox.right > pGen->ScreenBox.right)
				CBoxMoveX(&pIcon->ScreenBox, pGen->ScreenBox.right - (pIcon->ScreenBox.right - pIcon->ScreenBox.left));
		}
		pIcon->bVisible = CBoxIntersects(&pIcon->ScreenBox, &pGen->ScreenBox);
		pIcon->bDownwards = fPitchDiff > UI_GEN_COMPASS_PITCH_ANGLE;
		pIcon->bUpwards = fPitchDiff < -UI_GEN_COMPASS_PITCH_ANGLE;
	}
}

void ui_GenTickEarlyCompass(UIGen *pGen)
{
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	bool bAnyHovered = false;
	S32 i;

	for (i = 0; i < eaSize(&pState->eaIcons); i++)
	{
		UIGenCompassIcon *pIcon = pState->eaIcons[i];
		if (!bAnyHovered && pIcon->bVisible && mouseCollision(&pIcon->ScreenBox))
			bAnyHovered = pIcon->bHover = true;
		else
			pIcon->bHover = false;
	}
}

void ui_GenDrawEarlyCompass(UIGen *pGen)
{
	// Draw background, draw icons.
	UIGenCompass *pCompass = UI_GEN_RESULT(pGen, Compass);
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	F32 fBackgroundZ = UI_GET_Z();
	F32 fIconZ = UI_GET_Z();
	F32 fNotchZ = UI_GET_Z();
	F32 fVerticalIndicatorZ = UI_GET_Z();
	S32 i;
	
	if (pState->pCompassBackground)
	{
		CBox BackgroundBox = { 0, 0,
			pState->pCompassBackground->width * pGen->fScale,
			pState->pCompassBackground->height * pGen->fScale
		};
		CBoxSetCenter(&BackgroundBox,
			(pGen->ScreenBox.lx + pGen->ScreenBox.hx) / 2 - pState->iOffset,
			(pGen->ScreenBox.ly + pGen->ScreenBox.hy) / 2);
		display_sprite_box(pState->pCompassBackground, &BackgroundBox, fBackgroundZ, ui_StyleColorPaletteIndex(pCompass->uiCompassBackgroundColor));
	}

	if (pState->pCompassNotch)
	{
		display_sprite(pState->pCompassNotch,
			((pGen->ScreenBox.lx + pGen->ScreenBox.hx) - pState->pCompassNotch->width* pGen->fScale) / 2,
			((pGen->ScreenBox.ly + pGen->ScreenBox.hy) - pState->pCompassNotch->height * pGen->fScale) / 2,
			fNotchZ,
			pGen->fScale,
			pGen->fScale,
			ui_StyleColorPaletteIndex(pCompass->uiCompassNotchColor));
	}

	for (i = 0; i < eaSize(&pState->eaIcons); i++)
	{
		UIGenCompassIcon *pIcon = pState->eaIcons[i];
		F32 fCenterX = (pIcon->ScreenBox.lx + pIcon->ScreenBox.hx) / 2;
		if (!pIcon->bVisible || !pIcon->pIcon)
			continue;

		if (pIcon->bUpwards && pState->pUpwardIcon)
			display_sprite(pState->pUpwardIcon,
			fCenterX - pState->pUpwardIcon->width * pGen->fScale / 2,
			pGen->ScreenBox.ly,
			fVerticalIndicatorZ, pGen->fScale, pGen->fScale, ui_StyleColorPaletteIndex(pIcon->uiColor));
		if (pIcon->bDownwards && pState->pDownwardIcon)
			display_sprite(pState->pDownwardIcon,
			fCenterX - pState->pUpwardIcon->width * pGen->fScale / 2,
			pGen->ScreenBox.hy - pState->pDownwardIcon->height * pGen->fScale,
			fVerticalIndicatorZ, pGen->fScale, pGen->fScale, ui_StyleColorPaletteIndex(pIcon->uiColor));

		display_sprite_box(pIcon->pIcon, &pIcon->ScreenBox, fIconZ, ui_StyleColorPaletteIndex(pIcon->uiColor));
	
		if (pIcon->bHover && pIcon->pchName)
		{
			ui_StyleFontUse(GET_REF(pCompass->hTooltipFont), false, kWidgetModifier_None);
			clipperPush(NULL);
			gfxfont_Print(
				(pIcon->ScreenBox.lx + pIcon->ScreenBox.hx) / 2,
				pIcon->ScreenBox.ly,
				UI_INFINITE_Z,
				pGen->fScale, pGen->fScale,
				CENTER_X,
				pIcon->pchName);
			clipperPop();
		}
	}
}

void ui_GenFitContentsCompass(UIGen *pGen, UIGenCompass *pCompass, CBox *pBox)
{
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	if (pState && pState->pCompassBackground)
	{
		pBox->hy = pState->pCompassBackground->height;
		pBox->hx = pState->pCompassBackground->width;
	}
}

void ui_GenHideCompass(UIGen *pGen)
{
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	if (pState)
	{
		eaDestroyStruct(&pState->eaFlagged, parse_UIGenCompassFlagged);
		eaDestroyStruct(&pState->eaIcons, parse_UIGenCompassIcon);
	}
}

// Flag an entity to appear on the compass.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CompassFlagEntityRef);
void ui_GenExprCompassFlagEntityRef(SA_PARAM_OP_VALID UIGen *pGen, U32 uiEntity)
{
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	if (pState)
	{
		UIGenCompassFlagged *pFlagged;
		S32 i;
		for (i = 0; i < eaSize(&pState->eaFlagged); i++)
			if (pState->eaFlagged[i]->hEntity == uiEntity)
				return;
		pFlagged = StructCreate(parse_UIGenCompassFlagged);
		pFlagged->hEntity = uiEntity;
		eaPush(&pState->eaFlagged, pFlagged);
	}
}

// Flag a position to appear on the compass.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CompassFlagLocation);
void ui_GenExprCompassFlagLocation(SA_PARAM_OP_VALID UIGen *pGen, F32 fX, F32 fY, F32 fZ)
{
	UIGenCompassState *pState = UI_GEN_STATE(pGen, Compass);
	if (pState)
	{
		UIGenCompassFlagged *pFlagged;
		S32 i;
		for (i = 0; i < eaSize(&pState->eaFlagged); i++)
			if (nearf(pState->eaFlagged[i]->v3Location[0], fX)
				&& nearf(pState->eaFlagged[i]->v3Location[1], fY)
				&& nearf(pState->eaFlagged[i]->v3Location[2], fZ))
				return;
		pFlagged = StructCreate(parse_UIGenCompassFlagged);
		setVec3(pFlagged->v3Location, fX, fY, fZ);
		eaPush(&pState->eaFlagged, pFlagged);
	}
}

AUTO_RUN;
void ui_GenCompassRegister(void)
{
	ui_GenRegisterType(kUIGenTypeCompass,
		UI_GEN_NO_VALIDATE,
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateCompass,
		UI_GEN_NO_LAYOUTEARLY,
		ui_GenLayoutLateCompass,
		ui_GenTickEarlyCompass,
		UI_GEN_NO_TICKLATE,
		ui_GenDrawEarlyCompass,
		ui_GenFitContentsCompass, 
		UI_GEN_NO_FITPARENTSIZE,
		ui_GenHideCompass,
		UI_GEN_NO_INPUT,
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}
