// This is an independent implementation of the functions in stackwalk.h, designed to produce results that are compatible with it (including
// a callstack report based on callstack.h).  If the unwinding in stackwalk.h fails, this implementation is used as a last resort.  Failure
// to generate a callstack will may prevent the error report from being useful.  For instance, without a callstack, crash bucketing will fail.
// Therefore, we try as hard as possible to produce something useful.  This implementation is specifically designed to be more robust
// and produce something even under adverse conditions.

#ifndef CRYPTIC_STACKWALK_FALLBACK_H
#define CRYPTIC_STACKWALK_FALLBACK_H

// Information that we need to unwind the stack of another thread.
typedef struct FallbackStackThreadContext {
	void *tib;						// Windows Thread Information Block
	void *boundingFramePointer;		// A frame pointer (eg, ebp) above the top of the stack, if available.  This is used if topFramePointer doesn't work out.
	bool has_thread_context;		// If true, topPc and topFramePointer have been filled with guesses.
	void *topPc;					// PC or IP of the top frame, from the fault information, if present
	void *topFramePointer;			// Pointer to the top frame, from the fault information, if present
} FallbackStackThreadContext;

// Alternate implementation of stackWalkDumpStackToBuffer(), used when the former fails.  It not be called directly.
// This function only does something if it's able to decode more good frames than best_so_far.  If so, it returns how many frames it was able to decode.
int fallback_stackWalkDumpStackToBuffer(char* stacktext, size_t stacktextSize, FallbackStackThreadContext *context, char *pCallstackReport, size_t pCallstackReportSize, void *objectInMainFrame, int best_so_far);

// Capture the current thread context, so we know what to unwind.
void fallback_captureCurrentContext(FallbackStackThreadContext *context, void *tib, void *boundingFramePointer, bool has_thread_context, void *topPc, void *topFramePointer);

#endif  // CRYPTIC_STACKWALK_FALLBACK_H
