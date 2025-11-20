#pragma once

// GSM functions

void gcl_PatchStreamingEnter(void);
void gcl_PatchStreamingFrame(void);
void gcl_PatchStreamingLeave(void);

// gclMain functions

//bool gcl_PatchRestart(void);
void gcl_PatchStreamingFinish(void);

void gclPatchStreamingProcess(void);

// In development mode, turn off artificial delays in patch streaming.
void gclPatchStreamingFastMode(void);

// Make debug markers for map loading begin and end
void gclPatchStreamingDebugLoadingStarted(void);
void gclPatchStreamingDebugLoadingFinished(void);
