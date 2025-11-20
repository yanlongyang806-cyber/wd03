/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef DBOFFLINING_H_
#define DBOFFLINING_H_

void CreateOffliningQueue(bool skipOfflining);
void ProcessOffliningQueue();
U32 TotalEntityPlayersToOffline();
U32 EntityPlayersLeftToOffline();
U32 OfflineFailures();
U32 EntityPlayersOfflined();
bool IsMovingToOfflineHoggDone();

void QueueOfflineHoggCleanUp(bool skipOfflining);
void ProcessCleanUpQueue();
U32 TotalOfflineContainersToCleanup();
U32 OfflineContainersLeftToCleanup();
U32 ContainersCleanedUp();
bool IsCleaningUpOfflineHoggDone();

int GetTotalOfflineCharacters();
void IncrementTotalOfflineCharacters();
void DecrementTotalOfflineCharacters();
void InitializeTotalOfflineCharacters();

void DecrementContainersToOffline(void);
void DecrementOfflineContainersToCleanup(void);
void DecrementOutstandingAccountStubOperations(void);

bool LazyRestoreSharedBank();

#endif
