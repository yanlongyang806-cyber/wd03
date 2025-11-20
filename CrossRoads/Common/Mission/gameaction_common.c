/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contact_common.h"
#include "entEnums_h_ast.h"
#include "error.h"
#include "expression.h"
#include "gameaction_common.h"
#include "ItemAssignments.h"
#include "mission_common.h"
#include "mission_enums.h"
#include "ResourceInfo.h"
#include "ShardVariableCommon.h"
#include "StringUtil.h"
#include "WorldGrid.h"
#include "Guild.h"
#include "ActivityLogEnums.h"
#include "ActivityLogCommon.h"

#include "AutoGen/mission_enums_h_ast.h"
#include "AutoGen/ActivityLogEnums_h_ast.h"
#include "AutoGen/GlobalEnums_h_ast.h"

#include "../StaticWorld/group.h"

#ifdef GAMESERVER
#include "gslGameAction.h"
#include "gslSpawnPoint.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Runs generation on the given list of GameActions
// If a MissionDef is provided, it will be used to validate GrantSubMission Actions
void gameaction_GenerateActions(WorldGameActionProperties ***actions, MissionDef *def, const char *pchFilename)
{
	int i;

	for(i=eaSize(actions)-1; i>=0; --i)
	{
		WorldGameActionProperties *action = (*actions)[i];
		if (action->eActionType == WorldGameActionType_Expression)
		{
			if (action->pExpressionProperties && IsServer())
			{
#ifdef GAMESERVER
				gameaction_GenerateExpression(action);
#endif
			}
		} 
		else if (action->eActionType == WorldGameActionType_GiveDoorKeyItem && action->pGiveDoorKeyItemProperties)
		{
			int iVar;
			for(iVar = 0; iVar < eaSize(&action->pGiveDoorKeyItemProperties->eaVariableDefs); iVar++)
			{
#ifdef GAMESERVER
				worldVariableDefGenerateExpressions(action->pGiveDoorKeyItemProperties->eaVariableDefs[iVar], "GiveDoorKeyItem Game Action", pchFilename);
#endif
			}

			if(action->pGiveDoorKeyItemProperties->pDestinationMap) 
			{
#ifdef GAMESERVER
				worldVariableDefGenerateExpressions(action->pGiveDoorKeyItemProperties->pDestinationMap, "GiveDoorKeyItem Game Action", pchFilename);
#endif
			}
		}
		else if (action->eActionType == WorldGameActionType_Warp && action->pWarpProperties)
		{
			if(action->pWarpProperties->eaVariableDefs)
			{
				int iVar;
				for(iVar = 0; iVar < eaSize(&action->pWarpProperties->eaVariableDefs); iVar++)
				{
	#ifdef GAMESERVER
					worldVariableDefGenerateExpressions(action->pWarpProperties->eaVariableDefs[iVar], "Warp Game Action", pchFilename);
	#endif
				}
			}

#ifdef GAMESERVER
			worldVariableDefGenerateExpressions(&action->pWarpProperties->warpDest, "Warp Game Action", pchFilename);
#endif
		}
	}
}

// Runs validation on the given list of GameActions
// If a MissionDef is provided, it will be used to validate mission-variable-driven actions
// If a source map name is provided, it will be used to validate map-variable-driven actions
bool gameaction_ValidateActions(WorldGameActionProperties ***actions, const char *pchSourceMapName, MissionDef *pRootMissionDef, MissionDef *pMissionDef, bool bAllowMapVariables, const char *pchFilename)
{
	bool result = true;
	int i;
	int iNumWarp = 0;
	int iNumContactDialog = 0;
	int iNumTransactional = 0;

	for (i = eaSize(actions) - 1; i >= 0; --i)
	{
		WorldGameActionProperties *action = (*actions)[i];
		int numNemesisStateChanges = 0;

		if (action->eActionType == WorldGameActionType_GrantMission)
		{
			++iNumTransactional;

			if (!action->pGrantMissionProperties)
			{
				ErrorFilenamef(pchFilename, "Grant Mission action does not have properties");
				result = false;
			}
			else if (action->pGrantMissionProperties->eType == WorldMissionActionType_Named)
			{
				if (!IS_HANDLE_ACTIVE(action->pGrantMissionProperties->hMissionDef))
				{
					ErrorFilenamef(pchFilename, "Grant Mission action does not have a mission def");
					result = false;
				}
				else if (!GET_REF(action->pGrantMissionProperties->hMissionDef))
				{
					ErrorFilenamef(pchFilename, "Grant Mission action references unknown mission def (%s)",REF_STRING_FROM_HANDLE(action->pGrantMissionProperties->hMissionDef));
					result = false;
				}
			}
			else if (action->pGrantMissionProperties->eType == WorldMissionActionType_MissionVariable)
			{	
				if (!pMissionDef)
				{
					ErrorFilenamef(pchFilename, "Grant Mission action cannot access mission variables outside of a mission");
					result = false;
				}
				else
				{
					WorldVariableDef *pVarDef = eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, action->pGrantMissionProperties->pcVariableName);
					if (!pVarDef && pRootMissionDef)
						pVarDef = eaIndexedGetUsingString(&pRootMissionDef->eaVariableDefs, action->pGrantMissionProperties->pcVariableName);
					if (!pVarDef)
					{
						ErrorFilenamef(pchFilename, "Grant Mission action is using unknown mission variable (%s)", action->pGrantMissionProperties->pcVariableName);
						result = false;
					}
					else if (pVarDef->eType != WVAR_MISSION_DEF)
					{
						ErrorFilenamef(pchFilename, "Grant Mission action is using mission variable (%s), but this variable is not of MissionDef type and it must be of this type", action->pGrantMissionProperties->pcVariableName);
						result = false;
					}
				}
			}
			else if (action->pGrantMissionProperties->eType == WorldMissionActionType_MapVariable)
			{
				if (!bAllowMapVariables)
				{
					ErrorFilenamef(pchFilename, "Grant Mission action is not allowed to use map variables");
					result = false;
				}
#ifdef GAMESERVER
				else
				{
					ZoneMapInfo *pSrcZoneMap = worldGetZoneMapByPublicName(pchSourceMapName);

					if (pSrcZoneMap)
					{
						WorldVariableDef *pVarDef = pSrcZoneMap ? zmapInfoGetVariableDefByName(pSrcZoneMap, action->pGrantMissionProperties->pcVariableName) : NULL;

						if (!pVarDef)
						{
							ErrorFilenamef(pchFilename, "Grant Mission action is using unknown map variable (%s)", action->pGrantMissionProperties->pcVariableName);
							result = false;
						}
						else if (pVarDef->eType != WVAR_MISSION_DEF)
						{
							ErrorFilenamef(pchFilename, "Grant Mission action is using map variable (%s), but this variable is not of MissionDef type and it must be of this type", action->pGrantMissionProperties->pcVariableName);
							result = false;
						}
					}
				}
#endif
			}
			if (pMissionDef && missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform GrantMission actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_GrantSubMission)
		{
			++iNumTransactional;

			if (!action->pGrantSubMissionProperties)
			{
				ErrorFilenamef(pchFilename, "Grant Sub Mission action does not have properties");
				result = false;
			}
			else
			{
				if (!action->pGrantSubMissionProperties->pcSubMissionName)
				{
					ErrorFilenamef(pchFilename, "Grant Sub Mission action does not have a sub-mission name");
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_MissionOffer)
		{
			MissionDef *pOfferDef = NULL;
			++iNumContactDialog;

			if (!action->pMissionOfferProperties)
			{
				ErrorFilenamef(pchFilename, "Mission Offer action does not have properties");
				result = false;
			}
			else if (action->pMissionOfferProperties->eType == WorldMissionActionType_Named)
			{
				pOfferDef = GET_REF(action->pMissionOfferProperties->hMissionDef);
				if (!IS_HANDLE_ACTIVE(action->pMissionOfferProperties->hMissionDef))
				{
					ErrorFilenamef(pchFilename, "Mission Offer action does not have a mission def");
					result = false;
				}
				else if (!GET_REF(action->pMissionOfferProperties->hMissionDef))
				{
					ErrorFilenamef(pchFilename, "Mission Offer action references unknown mission def (%s)",REF_STRING_FROM_HANDLE(action->pMissionOfferProperties->hMissionDef));
					result = false;
				}

				// Can only offer Normal missions for now
				if (pOfferDef && pOfferDef->missionType != MissionType_Normal) 
				{
					ErrorFilenamef(pchFilename, "Mission Offer actions cannot offer missions of type '%s'",StaticDefineIntRevLookup(MissionTypeEnum,pOfferDef->missionType) );
					result = false;
				}
			}
			else if (action->pMissionOfferProperties->eType == WorldMissionActionType_MissionVariable)
			{	
				if (!pMissionDef)
				{
					ErrorFilenamef(pchFilename, "Mission Offer action cannot access mission variables outside of a mission");
					result = false;
				}
				else
				{
					WorldVariableDef *pVarDef = eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, action->pMissionOfferProperties->pcVariableName);
					if (!pVarDef && pRootMissionDef)
						pVarDef = eaIndexedGetUsingString(&pRootMissionDef->eaVariableDefs, action->pMissionOfferProperties->pcVariableName);
					if (!pVarDef)
					{
						ErrorFilenamef(pchFilename, "Mission Offer action is using unknown variable (%s)", action->pMissionOfferProperties->pcVariableName);
						result = false;
					}
					else if (pVarDef->eType != WVAR_MISSION_DEF)
					{
						ErrorFilenamef(pchFilename, "Mission Offer action is using variable (%s), but this variable is not of MissionDef type and it must be of this type", action->pMissionOfferProperties->pcVariableName);
						result = false;
					}
				}
			}
			else if (action->pMissionOfferProperties->eType == WorldMissionActionType_MapVariable)
			{
				if (!bAllowMapVariables)
				{
					ErrorFilenamef(pchFilename, "Mission Offer action is not allowed to use map variables");
					result = false;
				}
				else if (pMissionDef && missiondef_GetType(pMissionDef) != MissionType_OpenMission)
				{
					ErrorFilenamef(pchFilename, "Mission Offer action cannot use map variables for a mission unless the mission is an open mission");
					result = false;
				}
#ifdef GAMESERVER
				else
				{
					ZoneMapInfo *pSrcZoneMap = worldGetZoneMapByPublicName(pchSourceMapName);

					if (pSrcZoneMap)
					{
						WorldVariableDef *pVarDef = pSrcZoneMap ? zmapInfoGetVariableDefByName(pSrcZoneMap, action->pMissionOfferProperties->pcVariableName) : NULL;

						if (!pVarDef)
						{
							ErrorFilenamef(pchFilename, "Grant Mission action is using unknown map variable (%s)", action->pMissionOfferProperties->pcVariableName);
							result = false;
						}
						else if (pVarDef->eType != WVAR_MISSION_DEF)
						{
							ErrorFilenamef(pchFilename, "Grant Mission action is using map variable (%s), but this variable is not of MissionDef type and it must be of this type", action->pMissionOfferProperties->pcVariableName);
							result = false;
						}
					}
				}
#endif
			}
			if (pMissionDef && missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform MissionOffer actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_DropMission)
		{
			++iNumTransactional;

			if (!action->pDropMissionProperties)
			{
				ErrorFilenamef(pchFilename, "Drop Mission action does not have properties");
				result = false;
			}
			else
			{
				if (!action->pDropMissionProperties->pcMissionName)
				{
					ErrorFilenamef(pchFilename, "Drop Mission action does not have a sub-mission name");
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_TakeItem)
		{
			++iNumTransactional;

			if (!action->pTakeItemProperties)
			{
				ErrorFilenamef(pchFilename, "Take Item action does not have properties");
				result = false;
			}
			else
			{
				if (!IS_HANDLE_ACTIVE(action->pTakeItemProperties->hItemDef))
				{
					ErrorFilenamef(pchFilename, "Take Item action does not have an item def");
					result = false;
				}
				else if (!GET_REF(action->pTakeItemProperties->hItemDef))
				{
					ErrorFilenamef(pchFilename, "Take Item action references unknown item def (%s)", REF_STRING_FROM_HANDLE(action->pTakeItemProperties->hItemDef));
					result = false;
				}

				if (action->pTakeItemProperties->iCount < 0)
				{
					ErrorFilenamef(pchFilename, "Take Item action is giving an illegal count of (%d)", action->pTakeItemProperties->iCount);
					result = false;
				}
			}
			if (pMissionDef && missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform TakeItem actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_GiveItem)
		{
			++iNumTransactional;

			if (!action->pGiveItemProperties)
			{
				ErrorFilenamef(pchFilename, "Give Item action does not have properties");
				result = false;
			}
			else
			{
				if (!IS_HANDLE_ACTIVE(action->pGiveItemProperties->hItemDef))
				{
					ErrorFilenamef(pchFilename, "Give Item action does not have an item def");
					result = false;
				}
				else if (!GET_REF(action->pGiveItemProperties->hItemDef))
				{
					ErrorFilenamef(pchFilename, "Give Item action references unknown item def (%s)", REF_STRING_FROM_HANDLE(action->pGiveItemProperties->hItemDef));
					result = false;
				}

				if ((action->pGiveItemProperties->iCount < 1) || (action->pGiveItemProperties->iCount > 100))
				{
					ErrorFilenamef(pchFilename, "Give Item action is giving an illegal count of (%d)", action->pGiveItemProperties->iCount);
					result = false;
				}
			}
			if (pMissionDef && missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform GiveItem actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_GiveDoorKeyItem)
		{
			++iNumTransactional;

			if (!action->pGiveDoorKeyItemProperties)
			{
				ErrorFilenamef(pchFilename, "Give Door Key Item action does not have properties");
				result = false;
			}
			else
			{
				// Validate ItemDef
				if (!IS_HANDLE_ACTIVE(action->pGiveDoorKeyItemProperties->hItemDef))
				{
					ErrorFilenamef(pchFilename, "Give Door Key Item action does not have an item def");
					result = false;
				}
				else if (!GET_REF(action->pGiveDoorKeyItemProperties->hItemDef))
				{
					ErrorFilenamef(pchFilename, "Give Door Key Item action references unknown item def (%s)", REF_STRING_FROM_HANDLE(action->pGiveDoorKeyItemProperties->hItemDef));
					result = false;
				}

				// Validate Destination
				if (!action->pGiveDoorKeyItemProperties->pDestinationMap)
				{
					ErrorFilenamef(pchFilename, "Give Door Key Item action does not have a destination map.");
					result = false;
				}
				else if (action->pGiveDoorKeyItemProperties->pDestinationMap->eType != WVAR_MAP_POINT)
				{
					ErrorFilenamef(pchFilename, "Give Door Key Item action destination is not a MAP_POINT.  This is an internal editor error.");
					result = false;
				}
				else
				{
					result &= worldVariableValidateDef(action->pGiveDoorKeyItemProperties->pDestinationMap, action->pGiveDoorKeyItemProperties->pDestinationMap, "Give Door Key Item action", pchFilename);
				}

				if (action->pGiveDoorKeyItemProperties->pDestinationMap->eDefaultType == WVARDEF_SPECIFY_DEFAULT && action->pGiveDoorKeyItemProperties->pDestinationMap->pSpecificValue)
				{
					char* pcZoneMap = action->pGiveDoorKeyItemProperties->pDestinationMap->pSpecificValue->pcZoneMap;
					char* pcSpawnPoint = action->pGiveDoorKeyItemProperties->pDestinationMap->pSpecificValue->pcStringVal;

					// Only validate this on the server, not when saving in editor
					if (IsServer())
					{ 
#ifdef GAMESERVER
						ZoneMapInfo *pZoneMap = NULL;
						if (pcZoneMap)
						{
							pZoneMap = worldGetZoneMapByPublicName(pcZoneMap);
							if (!pZoneMap)
							{
								ErrorFilenamef(pchFilename, "Give Door Key Item action references non-existent map name '%s'.", pcZoneMap);
								result = false;
							}
						}

						if (pcSpawnPoint && (strnicmp(pcSpawnPoint, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0))
						{
							ErrorFilenamef(pchFilename, "Give Door Key Item action references temporary spawn point (or logical group) name '%s'.  You need to give the spawn point a non-temporary name before referencing it from this location.", pcSpawnPoint);
							result = false;
						}

						// Validate Vars
						if (pZoneMap)
						{
							result &= zmapInfoValidateVariableDefs(pZoneMap, action->pGiveDoorKeyItemProperties->eaVariableDefs, "Give Door Key Item action (using map variable)", pchFilename);
						}
#endif
					}
				}

				if(action->pGiveDoorKeyItemProperties->eaVariableDefs)
				{
					int iVar;
					for(iVar = 0; iVar < eaSize(&action->pGiveDoorKeyItemProperties->eaVariableDefs); iVar++)
					{
						result &= worldVariableValidateDef(action->pGiveDoorKeyItemProperties->eaVariableDefs[iVar], action->pGiveDoorKeyItemProperties->eaVariableDefs[iVar], "Give Door Key Item action", pchFilename);
					}
				}
			}
			if (pMissionDef && missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform Give Door Key Item actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_SendFloaterMsg)
		{
			if (!action->pSendFloaterProperties)
			{
				ErrorFilenamef(pchFilename, "Send Floater action does not have properties");
				result = false;
			}
			else if (IsServer()) // Only validate this on the server, not when saving in editor
			{
				if (!IS_HANDLE_ACTIVE(action->pSendFloaterProperties->floaterMsg.hMessage))
				{
					ErrorFilenamef(pchFilename, "Send Floater action does not have a message");
					result = false;
				}
				else if (!GET_REF(action->pSendFloaterProperties->floaterMsg.hMessage))
				{
					ErrorFilenamef(pchFilename, "Send Floater action references unknown message (%s)", REF_STRING_FROM_HANDLE(action->pSendFloaterProperties->floaterMsg.hMessage));
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_SendNotification)
		{
			if (!action->pSendNotificationProperties)
			{
				ErrorFilenamef(pchFilename, "Send Notification action does not have properties");
				result = false;
			}
			else if (IsServer()) // Only validate this on the server, not when saving in editor
			{
				if (IS_HANDLE_ACTIVE(action->pSendNotificationProperties->notifyMsg.hMessage) && !GET_REF(action->pSendNotificationProperties->notifyMsg.hMessage))
				{
					ErrorFilenamef(pchFilename, "Send Notification action references unknown message (%s)", REF_STRING_FROM_HANDLE(action->pSendNotificationProperties->notifyMsg.hMessage));
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_Warp)
		{
			++iNumWarp;

			if (!action->pWarpProperties)
			{
				ErrorFilenamef(pchFilename, "Warp action does not have any properties.");
				result = false;
			}
			else
			{
				// A warp  must have either a map or a target
				if (action->pWarpProperties->warpDest.eType != WVAR_MAP_POINT)
				{
					ErrorFilenamef(pchFilename, "Warp action destination is not a MAP_POINT.  This is an internal editor error.");
					result = false;
				}
				else
				{
					result &= worldVariableValidateDef(&action->pWarpProperties->warpDest, &action->pWarpProperties->warpDest, "Warp action", pchFilename);
				}

				if (action->pWarpProperties->warpDest.eDefaultType == WVARDEF_SPECIFY_DEFAULT && action->pWarpProperties->warpDest.pSpecificValue)
				{
					char* pcZoneMap = action->pWarpProperties->warpDest.pSpecificValue->pcZoneMap;
					char* pcSpawnPoint = action->pWarpProperties->warpDest.pSpecificValue->pcStringVal;
					bool bIsMissionReturn = (stricmp_safe(action->pWarpProperties->warpDest.pSpecificValue->pcStringVal, "MissionReturn") == 0);

					// Only validate this on the server, not when saving in editor
					if (IsServer())
					{ 
#ifdef GAMESERVER
						ZoneMapInfo *pZoneMap = NULL;
						if (pcZoneMap)
						{
							pZoneMap = worldGetZoneMapByPublicName(pcZoneMap);
							if (!pZoneMap)
							{
								ErrorFilenamef(pchFilename, "Warp action references non-existent map name '%s'.", pcZoneMap);
								result = false;
							}
						}

						if (pcSpawnPoint && (strnicmp(pcSpawnPoint, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0))
						{
							ErrorFilenamef(pchFilename, "Warp action references temporary spawn point (or logical group) name '%s'.  You need to give the spawn point a non-temporary name before referencing it from this location.", pcSpawnPoint);
							result = false;
						}

						if (pZoneMap)
						{
							result &= zmapInfoValidateVariableDefs(pZoneMap, action->pWarpProperties->eaVariableDefs, "Warp action (using map variable)", pchFilename);
						}
						else if (eaSize(&action->pWarpProperties->eaVariableDefs) && !pcZoneMap && !bIsMissionReturn)
						{
							ErrorFilenamef(pchFilename, "Warp action is attempting to set variables on the door, but they are ignored because the door does not leave the map.");
							result = false;
						}
#endif
					}
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_Contact)
		{
			++iNumContactDialog;

			if (!action->pContactProperties)
			{
				ErrorFilenamef(pchFilename, "Contact action does not have properties");
				result = false;
			}
			else if (IsServer()) // Only validate this on the server, not when saving in editor
			{
				if (!IS_HANDLE_ACTIVE(action->pContactProperties->hContactDef))
				{
					ErrorFilenamef(pchFilename, "Contact action does not have a contact");
					result = false;
				}
				else if (!GET_REF(action->pContactProperties->hContactDef))
				{
					ErrorFilenamef(pchFilename, "Contact action references unknown contact (%s)", REF_STRING_FROM_HANDLE(action->pContactProperties->hContactDef));
					result = false;
				}
				else if (action->pContactProperties->pcDialogName && !contact_HasSpecialDialog(GET_REF(action->pContactProperties->hContactDef), action->pContactProperties->pcDialogName))
				{
					ErrorFilenamef(pchFilename, "Contact action references unknown dialog (%s) on contact (%s)", action->pContactProperties->pcDialogName, REF_STRING_FROM_HANDLE(action->pContactProperties->hContactDef));
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_Expression)
		{
			if (!action->pExpressionProperties)
			{
				ErrorFilenamef(pchFilename, "Expression action does not have properties");
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_ChangeNemesisState)
		{
			++iNumTransactional;

			numNemesisStateChanges++;
			if (numNemesisStateChanges > 1)
			{
				ErrorFilenamef(pchFilename, "More than one nemesis state change actions");
				result = false;
			}
			else if (!action->pNemesisStateProperties)
			{
				ErrorFilenamef(pchFilename, "Change Nemesis State action does not have properties");
				result = false;
			}
			else
			{
				if (!StaticDefineIntRevLookup(NemesisStateEnum, action->pNemesisStateProperties->eNewNemesisState))
				{
					ErrorFilenamef(pchFilename, "Change Nemesis State action has unknown nemesis state %d", action->pNemesisStateProperties->eNewNemesisState);
					result = false;
				}
			}
			if (pMissionDef && missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform ChangeNemesisState actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_NPCSendMail)
		{
			++iNumTransactional;
			if (!action->pNPCSendEmailProperties)
			{
				ErrorFilenamef(pchFilename, "NPC Send Email action does not have properties");
				result = false;
			}
			else if (IsServer()) // Only validate this on the server, not when saving in editor
			{
				if (!IS_HANDLE_ACTIVE(action->pNPCSendEmailProperties->dFromName.hMessage))
				{
					ErrorFilenamef(pchFilename, "NPC Send Email action FromName does not have a message");
					result = false;
				}
				else if (!GET_REF(action->pNPCSendEmailProperties->dFromName.hMessage))
				{
					ErrorFilenamef(pchFilename, "NPC Send Email action FromName references unknown message (%s)",
						REF_STRING_FROM_HANDLE(action->pNPCSendEmailProperties->dFromName.hMessage));
					result = false;
				}
				if (!IS_HANDLE_ACTIVE(action->pNPCSendEmailProperties->dSubject.hMessage))
				{
					ErrorFilenamef(pchFilename, "NPC Send Email action Subject does not have a message");
					result = false;
				}
				else if (!GET_REF(action->pNPCSendEmailProperties->dSubject.hMessage))
				{
					ErrorFilenamef(pchFilename, "NPC Send Email action Subject references unknown message (%s)",
						REF_STRING_FROM_HANDLE(action->pNPCSendEmailProperties->dSubject.hMessage));
					result = false;
				}
				if (!IS_HANDLE_ACTIVE(action->pNPCSendEmailProperties->dBody.hMessage))
				{
					ErrorFilenamef(pchFilename, "NPC Send Email action Body does not have a message");
					result = false;
				}
				else if (!GET_REF(action->pNPCSendEmailProperties->dBody.hMessage))
				{
					ErrorFilenamef(pchFilename, "NPC Send Email action Body references unknown message (%s)",
						REF_STRING_FROM_HANDLE(action->pNPCSendEmailProperties->dBody.hMessage));
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_GADAttribValue)
		{
			++iNumTransactional;

			if (!action->pGADAttribValueProperties)
			{	
				ErrorFilenamef(pchFilename, "GameAccountData Key-Value action does not have properties");
				result = false;
			}
			else if (action->pGADAttribValueProperties->eModifyType != WorldVariableActionType_Set && 
				action->pGADAttribValueProperties->eModifyType != WorldVariableActionType_IntIncrement)
			{
				ErrorFilenamef(pchFilename, "GameAccountData Key-Value action does not support any operations other than [Set] or [IntIncrement]");
				result = false;
			}
			if (!pMissionDef)
			{
				ErrorFilenamef(pchFilename, "GameAccountData Key-Value actions are only supported on missions!");
				result = false;
			}
			else if (missiondef_GetType(pMissionDef) == MissionType_OpenMission)
			{
				ErrorFilenamef(pchFilename, "Open Missions cannot perform GameAccountData Key-Value actions (%s)", pMissionDef->pchRefString);
				result = false;
			}
		}
		else if (action->eActionType == WorldGameActionType_ShardVariable)
		{
			WorldShardVariableActionProperties *pProps = action->pShardVariableProperties;

			if (!pProps) 
			{
				ErrorFilenamef(pchFilename, "Shard Variable action does not have properties");
				result = false;
			} 
			else 
			{
				const WorldVariable *pDefaultVar = NULL;

				if (!pProps->pcVarName) 
				{
					ErrorFilenamef(pchFilename, "Shard Variable action does not specify a variable name");
					result = false;
				}
				else
				{
					pDefaultVar = shardvariable_GetDefaultValue(pProps->pcVarName);
					if (!pDefaultVar)
					{
						ErrorFilenamef(pchFilename, "Shard Variable action refers to non-existent variable '%s'", pProps->pcVarName);
						result = false;
					}
					if (pProps->eModifyType == WorldVariableActionType_Set)
					{
						if (!pProps->pVarValue)
						{
							ErrorFilenamef(pchFilename, "Shard Variable action specifies a set action on variable '%s' but has no value to set", pProps->pcVarName);
							result = false;
						}
						else if (pDefaultVar && (pProps->pVarValue->eType != pDefaultVar->eType))
						{
							ErrorFilenamef(pchFilename, "Shard Variable action specifies a set action on variable '%s' for type '%s' but the variable is really of type '%s'", pProps->pcVarName, worldVariableTypeToString(pProps->pVarValue->eType), worldVariableTypeToString(pDefaultVar->eType));
							result = false;
						}
					}
					else if (pProps->eModifyType == WorldVariableActionType_IntIncrement)
					{
						if (pProps->iIntIncrement == 0) 
						{
							ErrorFilenamef(pchFilename, "Shard Variable action specifies an int increment of zero on variable '%s'.  It should be a non-zero value.", pProps->pcVarName);
							result = false;
						}
						if (pDefaultVar && (pDefaultVar->eType != WVAR_INT))
						{
							ErrorFilenamef(pchFilename, "Shard Variable action specifies an int increment, but the variable '%s' really is of type '%s'.", pProps->pcVarName, worldVariableTypeToString(pDefaultVar->eType));
							result = false;
						}
					}
					else if (pProps->eModifyType == WorldVariableActionType_FloatIncrement)
					{
						if (pProps->fFloatIncrement == 0) 
						{
							ErrorFilenamef(pchFilename, "Shard Variable action specifies a float increment of zero on variable '%s'.  It should be a non-zero value.", pProps->pcVarName);
							result = false;
						}
						if (pDefaultVar && (pDefaultVar->eType != WVAR_FLOAT))
						{
							ErrorFilenamef(pchFilename, "Shard Variable action specifies a float increment, but the variable '%s' really is of type '%s'.", pProps->pcVarName, worldVariableTypeToString(pDefaultVar->eType));
							result = false;
						}
					}
					else if (pProps->eModifyType == WorldVariableActionType_Reset)
					{
						// Nothing to check here
					}
					else
					{
						ErrorFilenamef(pchFilename, "Shard Variable action specifies an unknown modify type");
						result = false;
					}
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_ActivityLog)
		{
			++iNumTransactional;

			if (!action->pActivityLogProperties)
			{
				ErrorFilenamef(pchFilename, "Activity Log action does not have properties");
				result = false;
			}
			else if (!StaticDefineIntRevLookup(ActivityLogEntryTypeEnum, action->pActivityLogProperties->eEntryType))
			{
				ErrorFilenamef(pchFilename, "Activity Log action has unknown entry type %d", action->pActivityLogProperties->eEntryType);
				result = false;
			}
            else if ( action->pActivityLogProperties->eEntryType == ActivityLogEntryType_None )
            {
                ErrorFilenamef(pchFilename, "Activity Log action type \"None\" is not valid.");
                result = false;
            }
            else if ( action->pActivityLogProperties->eEntryType == ActivityLogEntryType_All )
            {
                ErrorFilenamef(pchFilename, "Activity Log action type \"All\" is not valid.");
                result = false;
            }
            else if ( ActivityLog_GetTypeConfig(action->pActivityLogProperties->eEntryType) == NULL )
            {
                ErrorFilenamef(pchFilename, "Activity Log action type %d does not have a configuration in ActivityLogConfig.def", action->pActivityLogProperties->eEntryType);
                result = false;
            }
			else if (IsServer()) // Only validate this on the server, not when saving in editor
			{
				if (!IS_HANDLE_ACTIVE(action->pActivityLogProperties->dArgString.hMessage))
				{
					ErrorFilenamef(pchFilename, "Activity Log action does not have a message");
					result = false;
				}
				else if (!GET_REF(action->pActivityLogProperties->dArgString.hMessage))
				{
					ErrorFilenamef(pchFilename, "Activity Log action references unknown message (%s)", REF_STRING_FROM_HANDLE(action->pActivityLogProperties->dArgString.hMessage));
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_GuildStatUpdate)
		{
			++iNumTransactional;

			if (action->pGuildStatUpdateProperties == NULL)
			{
				ErrorFilenamef(pchFilename, "Guild stat update action does not have any properties.");
				result = false;
			}
			else
			{
				if (action->pGuildStatUpdateProperties->pchStatName == NULL || action->pGuildStatUpdateProperties->pchStatName[0] == '\0')
				{
					ErrorFilenamef(pchFilename, "Guild stat update action does not have a stat name.");
					result = false;
				}

				if (action->pGuildStatUpdateProperties->eOperation == GuildStatUpdateOperation_None)
				{
					ErrorFilenamef(pchFilename, "Guild stat update action does not have an operation set.");
					result = false;
				}
				else
				{
					if ((action->pGuildStatUpdateProperties->eOperation == GuildStatUpdateOperation_Add || 
						action->pGuildStatUpdateProperties->eOperation == GuildStatUpdateOperation_Subtract) &&
						action->pGuildStatUpdateProperties->iValue <= 0)
					{
						ErrorFilenamef(pchFilename, "Guild stat update action does not have a proper value set. The value must be a positive integer for 'Add' and 'Subtract' operations.");
						result = false;
					}
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_GuildThemeSet)
		{
			++iNumTransactional;
			if (action->pGuildThemeSetProperties == NULL)
			{
				ErrorFilenamef(pchFilename, "Guild theme set action does not have any properties.");
				result = false;
			}
			else
			{
				if (action->pGuildThemeSetProperties->pchThemeName == NULL || action->pGuildThemeSetProperties->pchThemeName[0] == '\0')
				{
					ErrorFilenamef(pchFilename, "Guild theme set action does not have a theme name.");
					result = false;
				}
			}
		}
		else if (action->eActionType == WorldGameActionType_UpdateItemAssignment)
		{
			if (!action->pItemAssignmentProperties)
			{
				ErrorFilenamef(pchFilename, "ItemAssignment action does not have any properties.");
				result = false;
			}
			else
			{
				if (!IS_HANDLE_ACTIVE(action->pItemAssignmentProperties->hAssignmentDef))
				{
					ErrorFilenamef(pchFilename, "ItemAssignment action does not have an ItemAssignmentDef.");
					result = false;
				}
				else if (!GET_REF(action->pItemAssignmentProperties->hAssignmentDef))
				{
					ErrorFilenamef(pchFilename, "ItemAssignment action references unknown ItemAssignmentDef (%s)", REF_STRING_FROM_HANDLE(action->pItemAssignmentProperties->hAssignmentDef));
					result = false;
				}
				if (action->pItemAssignmentProperties->eOperation <= kItemAssignmentOperation_None ||
					action->pItemAssignmentProperties->eOperation >= kItemAssignmentOperation_Count)
				{
					ErrorFilenamef(pchFilename, "ItemAssignment action uses an invalid operation");
					result = false;
				}
			}
		}
		else
		{
			Errorf("Someone added a new type to GameActionType without adding validate logic");
			result = false;
		}
	}

	if (iNumWarp > 1)
	{
		ErrorFilenamef(pchFilename, "Game actions cannot include more than one Warp action.");
		result = false;
	}
	if (iNumContactDialog > 1)
	{
		ErrorFilenamef(pchFilename, "Game actions cannot include more than one Contact or Mission Offer action.");
		result = false;
	}
	if (iNumTransactional > 0 && iNumWarp > 0) 
	{
		ErrorFilenamef(pchFilename, "Game actions cannot include both a Warp action and any transactional actions.  The Warp action will be ignored.");
		result = false;
	}

	return result;
}

// Returns TRUE if the two blocks are identical
// Note that NULL and a block with no actions are considered identical
bool gameactionblock_Compare(WorldGameActionBlock *a, WorldGameActionBlock *b)
{
	if (a && b && (StructCompare(parse_WorldGameActionBlock, a, b, 0, 0, 0) == 0))
		return true;

	if ((!a && !b) || (!a && b && !eaSize(&b->eaActions)) || (!b && a && !eaSize(&a->eaActions)))
		return true;

	return false;
}

// Returns TRUE if the two GameActionBlocks are similar enough to edit.
// (They have the same number of actions, and all actions are of the same type)
bool gameactionblock_CompareForEditing(WorldGameActionBlock *a, WorldGameActionBlock *b)
{
	int i;
	int size_a, size_b;
	size_a = a ? eaSize(&a->eaActions) : 0;
	size_b = b ? eaSize(&b->eaActions) : 0;

	if (size_a != size_b)
		return false;
	for (i = 0; i < size_a; i++)
	{
		if (a->eaActions[i]->eActionType != b->eaActions[i]->eActionType)
			return false;
	}
	return true;
}

bool gameactionblock_CanOpenContactDialog(WorldGameActionBlock *pBlock)
{
	int i;

	for(i=eaSize(&pBlock->eaActions)-1; i>=0; --i) {
		if (pBlock->eaActions[i]->eActionType == WorldGameActionType_Contact
			|| pBlock->eaActions[i]->eActionType == WorldGameActionType_MissionOffer) {
			return true;
		}
	}
	return false;
}

void gameactionblock_Clean(WorldGameActionBlock *pBlock)
{
	int i,j;

	for(i=eaSize(&pBlock->eaActions)-1; i>=0; --i) {
		WorldGameActionProperties *pAction = pBlock->eaActions[i];
		if ((pAction->eActionType == WorldGameActionType_Expression) && pAction->pExpressionProperties) {
			exprClean(pAction->pExpressionProperties->pExpression);
		} 
		else if((pAction->eActionType == WorldGameActionType_Warp) && pAction->pWarpProperties) {
			// Destination
			exprClean(pAction->pWarpProperties->warpDest.pExpression);

			// Vars
			for(j=eaSize(&pAction->pWarpProperties->eaVariableDefs)-1; j>=0; --j)
			{
				worldVariableDefCleanExpressions(pAction->pWarpProperties->eaVariableDefs[j]);
			}
		}
		else if((pAction->eActionType == WorldGameActionType_GiveDoorKeyItem) && pAction->pGiveDoorKeyItemProperties) {
			// Destination
			if(pAction->pGiveDoorKeyItemProperties->pDestinationMap)
				exprClean(pAction->pGiveDoorKeyItemProperties->pDestinationMap->pExpression);

			// Vars
			for(j=eaSize(&pAction->pGiveDoorKeyItemProperties->eaVariableDefs)-1; j>=0; --j)
			{
				worldVariableDefCleanExpressions(pAction->pGiveDoorKeyItemProperties->eaVariableDefs[j]);
			}
		}
	}
}

void gameaction_FixupMessages(WorldGameActionProperties *pAction, const char *pcScope, const char *pcBaseMessageKey, int iIndex, bool bCreate)
{
	if (pAction->eActionType == WorldGameActionType_SendFloaterMsg) {
		char buf1[1024];
		char buf2[1024];
		DisplayMessage *pDispMsg = &pAction->pSendFloaterProperties->floaterMsg;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.floater", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.floater", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
	} else if (pAction->eActionType == WorldGameActionType_SendNotification) {
		char buf1[1024];
		char buf2[1024];
		DisplayMessage *pDispMsg = &pAction->pSendNotificationProperties->notifyMsg;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.notify", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.notify", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
	} else if (pAction->eActionType == WorldGameActionType_MissionOffer) {
		char buf1[1024];
		char buf2[1024];
		DisplayMessage *pDispMsg = &pAction->pMissionOfferProperties->headshotNameMsg;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.missionoffer", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.missionoffer", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
	} else if (pAction->eActionType == WorldGameActionType_Warp) {
		int i;
		char buf1[1024];
		char buf2[1024];

		// Destination
		DisplayMessage *pDispMsg = SAFE_MEMBER_ADDR( pAction->pWarpProperties->warpDest.pSpecificValue, messageVal );
		if (pDispMsg) {
			if (bCreate) {
				sprintf(buf1, "%s.action_%d.warp.destination", pcBaseMessageKey, iIndex);
				sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
				pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
			} else if (pDispMsg->pEditorCopy) {
				sprintf(buf1, "%s.action_%d.warp.destination", pcBaseMessageKey, iIndex);
				sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
				langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
			}
		}

		// Vars
		for(i=eaSize(&pAction->pWarpProperties->eaVariableDefs)-1; i>=0; --i) {
			pDispMsg = SAFE_MEMBER_ADDR( pAction->pWarpProperties->eaVariableDefs[i]->pSpecificValue, messageVal );
			if (pDispMsg) {
				if (bCreate) {
					sprintf(buf1, "%s.action_%d.warp.var_%d", pcBaseMessageKey, iIndex, i);
					sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
					pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
				} else if (pDispMsg->pEditorCopy) {
					sprintf(buf1, "%s.action_%d.warp.var_%d", pcBaseMessageKey, iIndex, i);
					sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
					langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
				}
			}
		}
	}
	else if (pAction->eActionType == WorldGameActionType_NPCSendMail) {
		char buf1[1024];
		char buf2[1024];
		DisplayMessage *pDispMsg = &pAction->pNPCSendEmailProperties->dFromName;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.FromName", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.FromName", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
		pDispMsg = &pAction->pNPCSendEmailProperties->dSubject;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.Subject", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.Subject", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
		pDispMsg = &pAction->pNPCSendEmailProperties->dBody;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.Body", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.Body", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
	}
	else if (pAction->eActionType == WorldGameActionType_ShardVariable)
	{
		if ((pAction->pShardVariableProperties->eModifyType == WorldVariableActionType_Set) &&
			pAction->pShardVariableProperties->pVarValue &&
			(pAction->pShardVariableProperties->pVarValue->eType == WVAR_MESSAGE)) 
		{
			char buf1[1024];
			char buf2[1024];
			DisplayMessage *pDispMsg = &pAction->pShardVariableProperties->pVarValue->messageVal;
			if (bCreate) {
				sprintf(buf1, "%s.action_%d.setshardvar", pcBaseMessageKey, iIndex);
				sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
				pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
			} else if (pDispMsg->pEditorCopy) {
				sprintf(buf1, "%s.action_%d.setshardvar", pcBaseMessageKey, iIndex);
				sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
				langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
			}
		}
	}
	else if (pAction->eActionType == WorldGameActionType_ActivityLog)
	{
		char buf1[1024];
		char buf2[1024];
		DisplayMessage *pDispMsg = &pAction->pActivityLogProperties->dArgString;
		if (bCreate) {
			sprintf(buf1, "%s.action_%d.ActivityLog", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
		} else if (pDispMsg->pEditorCopy) {
			sprintf(buf1, "%s.action_%d.ActivityLog", pcBaseMessageKey, iIndex);
			sprintf(buf2, "This is a parameter for a \"%s\" action that occurs for a MissionDef.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
			langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
		}
	}
	else if (pAction->eActionType == WorldGameActionType_GiveDoorKeyItem && pAction->pGiveDoorKeyItemProperties) {
		int i;
		char buf1[1024];
		char buf2[1024];

		// Destination
		if(pAction->pGiveDoorKeyItemProperties->pDestinationMap)
		{
			DisplayMessage *pDispMsg = SAFE_MEMBER_ADDR( pAction->pGiveDoorKeyItemProperties->pDestinationMap->pSpecificValue, messageVal );
			if (pDispMsg) {
				if (bCreate) {
					sprintf(buf1, "%s.action_%d.GiveDoorKeyItem.DestinationVar", pcBaseMessageKey, iIndex);
					sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
					pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
				} else if (pDispMsg->pEditorCopy) {
					sprintf(buf1, "%s.action_%d.GiveDoorKeyItem.DestinationVar", pcBaseMessageKey, iIndex);
					sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
					langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
				}
			}
		}

		// Vars
		for(i=eaSize(&pAction->pGiveDoorKeyItemProperties->eaVariableDefs)-1; i>=0; --i) {
			DisplayMessage *pDispMsg = SAFE_MEMBER_ADDR( pAction->pGiveDoorKeyItemProperties->eaVariableDefs[i]->pSpecificValue, messageVal );
			if (pDispMsg) {
				if (bCreate) {
					sprintf(buf1, "%s.action_%d.GiveDoorKeyItem.var_%d", pcBaseMessageKey, iIndex, i);
					sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
					pDispMsg->pEditorCopy = langCreateMessage(buf1, buf2, pcScope, "");
				} else if (pDispMsg->pEditorCopy) {
					sprintf(buf1, "%s.action_%d.GiveDoorKeyItem.var_%d", pcBaseMessageKey, iIndex, i);
					sprintf(buf2, "This is a parameter for a \"%s\" action.", StaticDefineIntRevLookup(WorldGameActionTypeEnum, pAction->eActionType));
					langFixupMessage(pDispMsg->pEditorCopy, buf1, buf2, pcScope);
				}
			}
		}
	}
}


void gameaction_FixupMessageList(WorldGameActionProperties*** peaActionList, const char *pcScope, const char *pcBaseMessageKey, int iBaseIndex)
{
	int i;
	
	for(i=eaSize(peaActionList)-1; i>=0; --i) {
		WorldGameActionProperties *pAction = (*peaActionList)[i];
		gameaction_FixupMessages(pAction, pcScope, pcBaseMessageKey, i+iBaseIndex, false);
	}
}

// Helper function to count the number of actions in an array by type
int gameaction_CountActionsByType(WorldGameActionProperties** eaActionList, S32 eType)
{
	int i, iCount = 0;
	for (i = eaSize(&eaActionList)-1; i >= 0; i--) {
		if (eaActionList[i]->eActionType == eType) {
			iCount++;
		}
	}
	return iCount;
}


#include "AutoGen/gameaction_common_h_ast.c"
