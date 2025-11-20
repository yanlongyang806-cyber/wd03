/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;

// Used to check if user experience logging should occur for this user
// This is latelink so it can be written individually per game
LATELINK;
bool UserExp_ShouldLogThisUser(Entity *pEnt);

// Logging functions
void UserExp_LogLogin(Entity *pEnt);
void UserExp_LogLogout(Entity *pEnt);

void UserExp_LogSystemInfo(Entity *pEnt, const char *pcInfoString);

void UserExp_LogMapTransferArrival(Entity *pEnt);
void UserExp_LogMapTransferDeparture(Entity *pEnt);

void UserExp_LogMissionGranted(Entity *pEnt, const char *pcMissionName, const char *pcSubMissionName);
void UserExp_LogMissionComplete(Entity *pEnt, const char *pcMissionName, const char *pcSubMissionName, int bDropped);
void UserExp_LogMissionProgress(Entity *pEnt, const char *pcMissionName, const char *pcSubMissionName, const char *pcProgressStat, int iValue);
