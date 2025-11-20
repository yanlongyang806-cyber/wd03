#pragma once

#include "timedCallback.h"

typedef struct WhatsHotList WhatsHotList;

void aslUGCDataManager_ReportUGCProjectWasPlayedForWhatsHot(U32 iID);

void aslUGCDataManagerWhatsHot_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

bool aslUGCDataManagerWhatsHot_InitComplete(void);
void aslUGCDataManagerWhatsHot_Init(void);

WhatsHotList *GetWhatsHotList(void);