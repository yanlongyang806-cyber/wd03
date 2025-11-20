/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
#if _XBOX

// Initialization code for XCommon
void xCommon_Init(void);

// Per tick processing for XCommon
void xCommon_Tick(void);

// Returns true if the user with the given xuid is a friend of the user for the first signed in profile
bool xCommon_IsFriend(U64 xuid);

// Sets the index of the current player in xutil and input libraries. Pass XUSER_INDEX_NONE for no active player. 
void xCommon_SetCurrentPlayerIndex(U32 iPlayerIndex);

#endif