#ifndef ERRORTRACKERINTERFACE_H
#define ERRORTRACKERINTERFACE_H

#include "CrashRptConf.h"

#include "errornet.h"
#include "trackerparams.h"

CREXTERN void CREXPORT swSetErrorTracker(const char *pErrorTracker);
typedef void (CREXPORT *pfnSWSetErrorTracker)(const char *pErrorTracker);

CREXTERN void CREXPORT swSendErrorToTracker(SendErrorToTrackerParams *pParams);
typedef void (CREXPORT *pfnSWSendErrorToTracker)(SendErrorToTrackerParams *pParams);

CREXTERN void CREXPORT swSendDumpToTracker(SendDumpToTrackerParams *pParams);
typedef void (CREXPORT *pfnSWSendDumpToTracker)(SendDumpToTrackerParams *pParams);

#endif
