U32 progressOverlayCreate(S32 total_steps, const char *label);
S32 progressOverlayGetSize(U32 id);
void progressOverlaySetSize(U32 id, S32 total_steps);
void progressOverlaySetValue(U32 id, S32 value);
void progressOverlayIncrement(U32 id, S32 steps);
void progressOverlayRelease(U32 id);
void progressOverlayDraw();
