/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslLogin2_Error.h"
#include "AppServerLib.h"
#include "ServerLib.h"
#include "UtilitiesLib.h"
#include "logging.h"
#include "EString.h"
#include "earray.h"

// Debugging flag that causes logging to be sent to the console.
bool gLogin2LogToConsole = false;
AUTO_CMD_INT(gLogin2LogToConsole, Login2LogToConsole);

#define NUM_RECENT_LOG_LINES_TO_SAVE 50
static int sNextLogIndex = 0;
static STRING_EARRAY sRecentLogLines = NULL;

char **
aslLogin2_GetRecentLogLines(void)
{
    return sRecentLogLines;
}

int
aslLogin2_GetNextLogIndex(void)
{
    return sNextLogIndex;
}

void
aslLogin2_Log(char* format, ...)
{
    char* logStr = NULL;
    int recentLogSize = eaSize(&sRecentLogLines);

    VA_START(ap, format);

    estrConcatfv(&logStr, format, ap);

    log_printf(LOG_LOGIN, "%s", logStr);

    if ( gLogin2LogToConsole )
    {
        printf("%s", logStr);
    }

    if ( recentLogSize < NUM_RECENT_LOG_LINES_TO_SAVE )
    {
        // If the saved log lines array is not at max size yet, then just append to it.
        eaPush(&sRecentLogLines, logStr);    
    }
    else
    {
        devassert(sNextLogIndex < recentLogSize);
        if ( sNextLogIndex < recentLogSize )
        {
            // Free the previous string in that slot.
            estrDestroy( &sRecentLogLines[sNextLogIndex] );

            sRecentLogLines[sNextLogIndex] = logStr;
        }
    }

    // Increment the index for the next log line, wrapping if necessary.
    sNextLogIndex++;
    if ( sNextLogIndex >= NUM_RECENT_LOG_LINES_TO_SAVE )
    {
        sNextLogIndex = 0;
    }
    VA_END();
}