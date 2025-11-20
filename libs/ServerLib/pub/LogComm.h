/***************************************************************************



***************************************************************************/

#ifndef LOGCOMM_H
#define LOGCOMM_H

#include "logging.h"

typedef struct LinkToMultiplexer LinkToMultiplexer;

int svrLogInit(void);
void svrLogFlush(int force);
void svrLogSetSystemIsShuttingDown(void);

extern LinkToMultiplexer *gpLinkToMultiplexerForLogging;

void UpdateMultiplexerLinksForLogging();

//if true, then never do linkConnectWait, just go immediately to using fsm connections. Use this on things
//like launcher.exe where it's super-critical that they never stall, less critical that a logging
//connection instantly happens
void SetDontDoBlockingLogConnect(bool bSet);

void GetAdditionalLogServerCommandLineString(char** ppOut);

#endif
