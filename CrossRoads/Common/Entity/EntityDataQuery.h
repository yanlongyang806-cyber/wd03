#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ENTITYDATAQUERY_H_
#define ENTITYDATAQUERY_H_

#include "GlobalTypeEnum.h"

typedef struct ContainerRefArray	ContainerRefArray;

ContainerRefArray *GetAllContainersOfType(int type);

#endif
