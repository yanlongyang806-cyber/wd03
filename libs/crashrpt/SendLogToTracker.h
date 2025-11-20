#pragma once

#include "CrashRptConf.h"

//magical string for the error string sent to the controllertracker as part of the bug counting process. Do not ever use this
#define SEND_ASSERT_TO_TRACKER_PREFIX "EXTRAINFO=<<"
#define SEND_ASSERT_TO_TRACKER_SUFFIX ">>"
#define SEND_ASSERT_TO_TRACKER_USERNAME "USERNAME "


CREXTERN void CREXPORT swSendLogToTracker(const char *pErrorString, const char *pCommandString, const char *pVersionString);
typedef void (CREXPORT *pfnSWSendLogToTracker)(const char *pErrorString, const char *pCommandString, const char *pVersionString);

