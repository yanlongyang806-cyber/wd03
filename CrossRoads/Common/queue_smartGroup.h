/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef QUEUE_SMARTGROUP_H
#define QUEUE_SMARTGROUP_H

#pragma once
GCC_SYSTEM

typedef struct Entity Entity;
typedef struct QueueMemberJoinCriteria QueueMemberJoinCriteria;
typedef struct QueueGroup QueueGroup;
typedef struct QueueMember QueueMember;
typedef struct QueueDef QueueDef;

// Used in queue_common. Possibly used on many server types
void NNO_queue_EntFillJoinCriteria(Entity *pEntity, QueueMemberJoinCriteria* pMemberJoinCriteria);

#if defined(APPSERVER)

bool NNO_aslQueue_IsSmartGroupToAddTo(QueueDef* pQueueDef, QueueGroup* pGroup, int iGroupMaxSize, QueueMember* pMember, int iTeamSize,
										QueueMember ***peaReadyMembers);
#endif


#endif
