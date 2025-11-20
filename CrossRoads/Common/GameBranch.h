/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEBRANCH_H
#define GAMEBRANCH_H

#pragma once
GCC_SYSTEM

const char *GameBranch_GetDirectory(SA_PARAM_NN_VALID char **pcEstrOut, SA_PARAM_OP_STR const char *pchDirectory);
const char *GameBranch_GetFilename(SA_PARAM_NN_VALID char **pcEstrOut, SA_PARAM_OP_STR const char *pchFilename);

const char *GameBranch_FixupPath(char **pcEstrOut, const char *pchPath, bool bFixDirectory, bool bFixFilename);

extern char *g_pcGameBranch;

#endif GAMEBRANCH_H