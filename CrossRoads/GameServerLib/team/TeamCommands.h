/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Entity Entity;
typedef struct TeamMissionMapTransfer TeamMissionMapTransfer;

SA_RET_OP_VALID TeamMissionMapTransfer* gslTeam_FindAwayTeamTransfer(SA_PARAM_NN_VALID Entity* pEntity);

bool gslTeam_IsEntReadyForMapTransfer(Entity *pEnt);
void gslTeam_ShowPartyCircleOnClient(Entity *pEntity, Vec3 v3Center);
void gslTeam_HidePartyCircleOnClient(Entity *pEntity);
F32 gslTeam_GetTeamCorralCircleRadius();
F32 gslTeam_GetTeamCorralCircleRadiusSquared();
S32 gslTeam_GetTeamTransferTimeDefault();