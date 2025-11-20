#pragma once

void Controller_ClusterControllerTick(void);

//reported by controllerTracker code, which calculates it every 5 seconds, to avoid duplication in
//walking the list of gameservers
void Controller_ClusterControllerHereIsNumPlayers(int iNumPlayers);

void Controller_ClusterControllerHandleCommandReturnFromServer(int iRequestID, char *pMessageString);

char *Controller_GetClusterControllerName(void);