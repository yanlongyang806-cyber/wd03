/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
#if _XBOX

void xSession_Init(void);

void xSession_Tick(void);

void xSession_KillExistingSession(void);

void xSession_CreateSession(U32 iTeamId);

bool xSession_HasActiveSession(void);

bool xSession_IsInSession(U32 entId);

#endif