/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef LOADSCREEN_COMMON_H
#define LOADSCREEN_COMMON_H

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

AUTO_STRUCT;
typedef struct LoadScreenPair
{
	const char *pMap;
	const char *pLoadScreen; 

} LoadScreenPair;

AUTO_STRUCT;
typedef struct LoadScreenDynamic
{
	EARRAY_OF(LoadScreenPair)	esLoadScreens;

} LoadScreenDynamic;

#endif // LOADSCREEN_COMMON_H