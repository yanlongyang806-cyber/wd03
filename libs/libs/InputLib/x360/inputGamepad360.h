#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/


// We have to poll to see if an XOVERLAPPED event completed. It's absolutely
// *awesome* that XOVERLAPPED structures are designed for asynchronous
// message handling, yet to see if they're done you still need to either
// poll or put your main thread to sleep.
void inpXboxUpdate(void);

void handleDeadZone(SHORT* xInOut, SHORT* yInOut, SHORT deadZoneValue);