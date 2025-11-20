#pragma once

void gfxLoadUpdate(int num_bytes);

// Sets an optional loading message to indicate what phase of loading we're in
void gfxLoadingSetLoadingMessage(SA_PARAM_OP_STR const char *message);

void gfxLoadingStartWaiting(void);
void gfxLoadingFinishWaiting(void);
bool gfxLoadingIsWaiting(void);

bool gfxLoadingIsStillLoading(void);
bool gfxLoadingIsStillLoadingEx(float minElapsedTime, unsigned minElapsedFrame, int loadThreshold, bool includePatching);
bool gfxIsLoadingForContact(F32 fMinTimeSinceLoad);

void gfxLoadingDisplayScreen(bool displayBackground);

int gfxLoadingGetFinishedLoadCount(void);
