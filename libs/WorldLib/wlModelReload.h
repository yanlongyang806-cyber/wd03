#pragma once

void modelReloadInit(void);
void modelReloadCheck(void);
int getNumModelReloads(void);
int getModelReloadRetryCount(void);
void releaseGetVrmlLock(void);
bool waitForGetVrmlLock(bool block_wait);
void releaseGetVrmlRenderLock(void);
bool waitForGetVrmlRenderLock(bool block_wait);
