#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// Utility for logging errors during login.
void aslLogin2_Log(char *format, ...);

char **aslLogin2_GetRecentLogLines(void);
int aslLogin2_GetNextLogIndex(void);