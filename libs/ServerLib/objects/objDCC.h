/***************************************************************************



***************************************************************************/

#ifndef OBJDCC_H_
#define OBJDCC_H_

typedef U32 GlobalType;

void ExpireCachedDeletes(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
U32 GetNextCachedDeleteExpireInterval(GlobalType eType);

#endif //OBJDCC_H_