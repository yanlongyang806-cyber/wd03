// The fallback unwinder is designed to recover from DIRE DISASTER conditions, and generate a callstack when the main unwinder in stackwalk.c cannot.
//
// Therefore, it meets the following specifications:
// 1. It does not does not do any heap allocation, or any memory allocation at all, except possibly expanding the stack.  The maximum stack expansion
// is limited by the frame count limit.
// 2. It does not use any outside DLLs.
// 3. It does not require any Windows API calls.  It will attempt a few Windows API calls, that are generally likely to succeed.  However, it will
// use an alternate mechanism if they fail.
// 4. It does not trust the memory contents, and wraps risky bits in exception wrappers.
//
// The unwinder is based on heuristics.  There are multiple implemented unwind methods.  Each one of these, for a frame, finds the previous frame.
// Because not all methods will work in all cases, and the methods that have a higher likelihood of delivering a result tend to be less accurate, 
// the unwinder uses heuristics to attempt to find the best permutation of methods to yield the most accurate callstack.  It uses a basic
// recursive-descent approach.  Because this would tend to be prohibitively expensive (exponential time), memoization is used to reduce the
// complexity to linear (in most cases).

#include "callstack.h"
#include "disasm.h"
#include "ParsePEHeader.h"
#include "stackwalk.h"
#include "stackwalkfallback.h"
#include "utils.h"
#include "wininclude.h"
#include "winutil.h"
#include "UTF8.h"

// This must be included after <windows.h>
// imagehlp.h must be compiled with packing to eight-byte-boundaries,
// but does nothing to enforce that.
#pragma pack( push, before_imagehlp, 8 )
#include <dbghelp.h>
#pragma pack( pop, before_imagehlp )

#define MAX_FRAME_COUNT 50						// Max number of frames in stackwalk to cut off infinite loops
#define MAX_MEMO_ENTRIES (MAX_FRAME_COUNT*128)	// Max number of frame memo entries, to conserve storage

// From stackwalk.c
extern unsigned guStackwalkBoundaryScanTest;

/*** Miscellaneous utility functions, used below ***/

// Return the bottom or base of the stack.  On systems where the stack grows downward, this is the highest-valued pointer.
static char *tib_stack_bottom(FallbackStackThreadContext *context)
{
	NT_TIB *tib = context->tib;
	return tib->StackBase;
}

// Return the top or limit of the stack.  On systems where the stack grows downward, this is the lowest-valued pointer.
static char *tib_stack_top(FallbackStackThreadContext *context)
{
	NT_TIB *tib = context->tib;
	return tib->StackLimit;
}

// Return true if this is probably a valid stack pointer.
static int is_reasonable_stack_pointer(void *ptr, FallbackStackThreadContext *context)
{
	return ((uintptr_t)ptr & 0x3) == 0 && (char *)ptr <= tib_stack_bottom(context) && (char *)ptr >= tib_stack_top(context);
}

// Return true if this might be a code pointer.
static int is_reasonable_code_pointer(void *ptr, FallbackStackThreadContext *context)
{
	return (uintptr_t)ptr >= 4096							// Can't be null or on the null page
		&& ptr < (void *)0x80000000							// Can't have high bit set (not necessarily true, but usually true, and huge time saver)
		&& !is_reasonable_stack_pointer(ptr, context)		// Make sure it's not on the stack, because its easy to check
		&& !IsBadCodePtr(ptr);								// This is super-slow when it fails, especially in the debugger, due to page fault handling.
}

/*** Module information lookup ***/

// Look up information from the module's debug directory.
// This should fill in the following:
//
//   Member        : Report Line
//   ------          -----------
//   TimeDateStamp : Time
//   PdbSig70      : GUID
//   PdbAge        : Age
//   LoadedPdbName : PDB
//   ImageSize     : Size
//   TimeDateStamp : Time
static bool module_debug_info_lookup(void *module_base, IMAGEHLP_MODULE64 *moduleInfo, bool probe_only)
{
	const char *failureReason = NULL;
	int failureErrorCode = 0;
	bool failed = false;

	// Try debug info lookup.
	__try
	{
		GetDebugInfo(NULL, module_base, moduleInfo, &failureReason, &failureErrorCode);
	}
#pragma warning(suppress:6320)
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		if (!probe_only)
			stackWalkWarning(module_base, "GetDebugInfo:Exception", GetExceptionCode());
		failed = true;
	}
	
	// Report failure, if any.
	if (!failed && failureReason)
	{
		if (!probe_only)
			stackWalkWarning(module_base, failureReason, failureErrorCode);
		failed = true;
	}

	return !failed;
}

// Get a pointer to the module base from a pointer into that module, using the Win32 API.
static void *find_module_base_api(void *ptr)
{
	HMODULE base = NULL;
	bool result;
	MEMORY_BASIC_INFORMATION mbi;
	size_t size;
	
	// Try the normal way first.
	result = GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, ptr, &base);
	if (result && base && !(guStackwalkBoundaryScanTest & 0x0010))
		return base;
	stackWalkWarning(ptr, "Base:GetModuleHandleEx", GetLastError());

	// If that didn't work, try to cheat using VirtualQuery().
	size = VirtualQuery(ptr, &mbi, sizeof(mbi));
	if (size && mbi.AllocationBase && !(guStackwalkBoundaryScanTest & 0x0020))
		return mbi.AllocationBase;
	stackWalkWarning(ptr, "Base:VirtualQuery", GetLastError());

	// This isn't going to work.
	return 0;
}

// Get a pointer to the module base from a pointer into that module, using a brute force probing approach.
// We make the Windows-specific assumption that the page size is 4 KiB.
static void *find_module_base_fallback(void *ptr)
{
	char *page_ptr = (char *)((uintptr_t)ptr / 4096 * 4096);  // Align down to page boundary.

	if (guStackwalkBoundaryScanTest & 0x0040)
		return NULL;

	// Walk downward through pages until we find the base of the module.
	while (page_ptr)
	{
		IMAGEHLP_MODULE64 dummy;

		// If it passes all of the debug header checks, assume that it's our header.
		if (module_debug_info_lookup(page_ptr, &dummy, true))
			return page_ptr;

		// Even if it doesn't, assume that if we hit an unreadable page, the previous page must be the base of the module.
		if (IsBadReadPtr(page_ptr, 1))
			return page_ptr + 4096;

		// Try the previous page.
		page_ptr -= 4096;
	}

	return NULL;
}

// Get a pointer to the module base from a pointer into that module.
static void *find_module_base(void *ptr)
{
	void *base = find_module_base_api(ptr);
	if (!base)
		base = find_module_base_fallback(ptr);
	return base;
}

// Get the name for a module.
// This information is optional, so it's OK if this fails.
void module_get_name(char *name, size_t name_size, void *base)
{
	char *pPath = NULL;
	DWORD result;
	char *slash;
	char *filename;

	estrStackCreate(&pPath);

	// Get module file name.
	result = GetModuleFileName_UTF8(base, &pPath);
	if (!result)
	{
		stackWalkWarning(base, "GetModuleFileName", GetLastError());
		estrDestroy(&pPath);
		return;
	}

	// Trim path.
	slash = strrchr(pPath, '\\');
	if (slash)
		filename = slash + 1;
	else
		filename = pPath;
	strcpy_s(SAFESTR2(name), filename);

	estrDestroy(&pPath);
}

// Look up the callstack report information for a module.
static bool module_info_lookup(void *module_base, IMAGEHLP_MODULE64 *moduleInfo)
{
	bool success;

	if (!module_base)
		return false;

	// Save base.
	moduleInfo->BaseOfImage = (DWORD64)module_base;

	// Get executable debug header information.
	success = module_debug_info_lookup(module_base, moduleInfo, false);

	// Get module name.
	module_get_name(moduleInfo->ImageName, sizeof(moduleInfo->ImageName), module_base);

	return success;
}

// Generate the callstack report information for this stack frame.
static bool report_frame_module(char *buffer, size_t buffer_size, char *pCallstackReport, size_t pCallstackReport_size, void *return_address, const char *method)
{
	void *base;
	IMAGEHLP_MODULE64 moduleInfo = {0};
	bool success = false;
	
	// Get the module base.
	base = find_module_base(return_address);

	// Look up information 
	if (base)
		success = module_info_lookup(base, &moduleInfo);

	// Print the stack frame.
	if (success)
		strcatf_s(SAFESTR2(buffer), "%s!%08x (...)", moduleInfo.ImageName, (char *)return_address - (char *)base);
	else
		strcatf_s(SAFESTR2(buffer), "0x%08x (...)", (uintptr_t)return_address);
	strcatf_s(SAFESTR2(buffer), "\n\t\tLine: --- (0)\n\t\tModule: %s\n", moduleInfo.ImageName ? moduleInfo.ImageName : "???");

	// If no callstack report is requested, we're done.
	if (!pCallstackReport)
		return true;
	
	// Print module information for this frame.
	strcatf_s(SAFESTR2(pCallstackReport),
		"%s%s\n"	// Module Name
		"%s%s\n",	// PDB Name
		LineContentHeaders[LINECONTENTS_MODULE_NAME], moduleInfo.ImageName,
		LineContentHeaders[LINECONTENTS_MODULE_PDB], moduleInfo.LoadedPdbName);
	strcatf_s(SAFESTR2(pCallstackReport),
		"%s%I64x\n"	// Module Base Address
		"%s%08x\n"	// Module Size
		"%s%d\n",	// Module Time (in seconds since 2000): Never included here as it is non-necessary
		LineContentHeaders[LINECONTENTS_MODULE_BASE_ADDRESS], moduleInfo.BaseOfImage,
		LineContentHeaders[LINECONTENTS_MODULE_SIZE], moduleInfo.ImageSize,
		LineContentHeaders[LINECONTENTS_MODULE_TIME], 0);
	strcatf_s(SAFESTR2(pCallstackReport),
		"%s{%08X-%04X-%04X-%02X %02X-%02X %02X %02X %02X %02X %02X}\n", 
		LineContentHeaders[LINECONTENTS_MODULE_GUID],
		moduleInfo.PdbSig70.Data1,
		moduleInfo.PdbSig70.Data2,
		moduleInfo.PdbSig70.Data3,
		moduleInfo.PdbSig70.Data4[0], moduleInfo.PdbSig70.Data4[1],
		moduleInfo.PdbSig70.Data4[2], moduleInfo.PdbSig70.Data4[3], 
		moduleInfo.PdbSig70.Data4[4], moduleInfo.PdbSig70.Data4[5], 
		moduleInfo.PdbSig70.Data4[6], moduleInfo.PdbSig70.Data4[7]);
	strcatf_s(SAFESTR2(pCallstackReport),
		"%s%d\n", 
		LineContentHeaders[LINECONTENTS_MODULE_AGE], moduleInfo.PdbAge);
	strcatf_s(SAFESTR2(pCallstackReport),
		"%s%s\n\n", 
		LineContentHeaders[LINECONTENTS_MODULE_METHOD], method);

	return success;
}

/*** Stack unwinding ***/

// Generate information for this frame.
bool stackwalk_process_frame(void *return_address, int frame, const char *method,
	char *buffer, size_t buffer_size,
	char *pCallstackReport, size_t pCallstackReport_size,
	char *pReturnAddresses, size_t pReturnAddresses_size)
{
	bool success;
	strcatf_s(SAFESTR2(buffer), "%3i ", frame);
	success = report_frame_module(buffer, buffer_size, pCallstackReport, pCallstackReport_size, return_address, method);
	strcatf_s(SAFESTR2(pReturnAddresses), "%s%08p\n", LineContentHeaders[LINECONTENTS_CALLSTACK_ADDRESS], return_address);
	return success;
}

// Write the callstack report prologue.
static void start_callstack_report(char *pCallstackReport, size_t pCallstackReportSize)
{
	strcatf_s(pCallstackReport, pCallstackReportSize, "%s\n\n", LineContentHeaders[LINECONTENTS_MODULES_START]);
}

// Write the callstack report epilogue and the stack itself.
static void finish_callstack_report(char *pCallstackReport, size_t pCallstackReportSize, const char *pReturnAddresses)
{
	strcatf_s(pCallstackReport, pCallstackReportSize, "%s\n\n", LineContentHeaders[LINECONTENTS_MODULES_END]);
	strcatf_s(pCallstackReport, pCallstackReportSize, "%s\n\n", LineContentHeaders[LINECONTENTS_CALLSTACK_START]);
	strcatf_s(pCallstackReport, pCallstackReportSize, "%s", pReturnAddresses);
	strcatf_s(pCallstackReport, pCallstackReportSize, "%s\n\n", LineContentHeaders[LINECONTENTS_CALLSTACK_END]);
}

// Unwind one frame using the frame pointer.
static void *stackwalk_frame_fp(FallbackStackThreadContext *context, void *frame, bool got_to_main, int *method_score)
{
	void *result;

	if (guStackwalkBoundaryScanTest & 0x0008)
		return NULL;

	// Follow frame pointer.
	result = *(void**)frame;

	// If it looks OK, return that.
	if (is_reasonable_stack_pointer(result, context) && (char *)result > (char *)frame)
		return result;

	// Otherwise, fail.
	return NULL;
}

// Unwind one frame using unwind tables.
//static void *stackwalk_frame_table(FallbackStackThreadContext *context, void *frame, bool got_to_main, int *method_score)
// {
// 	// TODO: Support table-based unwinding for x64.
// 	return NULL;
// }

// Unwind one frame by disassembling the frame to try to determine the frame size.
static void *stackwalk_frame_framesize(FallbackStackThreadContext *context, void *frame, bool got_to_main, int *method_score)
{
	void *return_address;
	size_t stack_offset;
	void *previous_frame;
	void *previous_return_address;

	// Currently, this method is experimental, and under test, so it's off unless enabled by the test mode.
	if (!(guStackwalkBoundaryScanTest & 0x0100))
		return NULL;

	// Don't use this method if we've already made it to the main frame: it's not worth it.
	if (got_to_main)
		return NULL;

	*method_score -= 2;

	// Get return address.
	return_address = *((void **)frame + 1);
	if (!is_reasonable_code_pointer(return_address, context))
		return NULL;

	// Try to determine the current stack offset, by examining the code in the frame.
	stack_offset = disasm_guess_frame_size(return_address);

	// Cap maximum frame size.
	if (stack_offset > 2*1024*1024)
		return NULL;

	// Calculate previous frame pointer.
	previous_frame = (char *)((void **)frame + 2) + stack_offset;

	// Make sure it's reasonable.
	if (!is_reasonable_stack_pointer(previous_frame, context))
		return NULL;

	// Corroborate previous frame using the same method as stackwalk_frame_callsite().
	previous_return_address = *((void **)previous_frame + 1);
	if (!is_reasonable_code_pointer(previous_return_address, context) || !disasm_is_return_point(previous_return_address))
		return NULL;

	return previous_return_address;
}

// Unwind one frame by looking for pointers on the stack to code right after call instructions.
static void *stackwalk_frame_callsite(FallbackStackThreadContext *context, void *frame, bool got_to_main, int *method_score)
{
	// Don't use this method if we've already made it to the main frame: it's not worth it.
	if (got_to_main)
		return NULL;

	*method_score -= 4;

	for (frame = (void **)frame + 1; (char *)frame <= tib_stack_bottom(context); frame = (void **)frame + 1)
	{
		void *return_address = *((void **)frame + 1);

		// Skip this address if it doesn't look like a return point.
		if (!is_reasonable_code_pointer(return_address, context) || !disasm_is_return_point(return_address))
			continue;

		return frame;
	}

	return NULL;
}

// Recorded score for a particular frame.
// This can be used to quickly advance to a previous frame, and to rule out a previous frame as being useful.
typedef struct MemoEntry {
	void *frame;			// Key: The frame that this information is for
	int score;				// The best achievable score from this frame
	void *previous_frame;	// For that score, the previous frame
	int score_offset;		// For that score, the score offset of the previous frame
	const char *method;		// For that score, the unwind method used
} MemoEntry;

// Check if this is the best callstack we've seen yet, and if so, save it.
static void check_best(void **frames,
	const char **framesMethod,
	void **bestFrames,
	const char **bestFramesMethod,
	int *bestFramesCount,
	bool *found_main,
	int count,
	int score,
	int *best,
	bool got_to_main)
{
	if (got_to_main && !*found_main
		|| (count && score > *best && (got_to_main || !*found_main)))
	{
		memcpy(bestFrames, frames, sizeof(*frames)*count);
		memcpy((void *)bestFramesMethod, framesMethod, sizeof(*framesMethod)*count);
		*bestFramesCount = count;
		if (got_to_main)
			*found_main = true;
		*best = score;
	}
}

typedef void *(*frame_unwind_method)(FallbackStackThreadContext *context, void *frame, bool got_to_main, int *method_score);
static int stackwalk_frame(void **frames, const char **framesMethod, void **bestFrames, const char **bestFramesMethod,
	int *bestFramesCount, bool *found_main, void *frame, int count, FallbackStackThreadContext *context, void *objectInMainFrame,
	int score, int *best, MemoEntry *memos, int *memo_count);

// Try to unwind a frame using the provided method.  If it works, proceed with unwinding from that frame.
// If not, this is the end of this particular callstack; see if we've created a callstack worth keeping.
static int try_frame_unwind_method(frame_unwind_method method, const char *method_name,
	void **frames,
	const char **framesMethod,
	void **bestFrames,
	const char **bestFramesMethod,
	int *bestFramesCount,
	bool *found_main,
	void *frame,
	int count,
	FallbackStackThreadContext *context,
	void *objectInMainFrame,
	int score,
	int *best,
	MemoEntry *memos, int *memo_count,
	void **previous_frame, int *score_offset)
{
	bool got_to_main = (char *)frame > (char *)objectInMainFrame;
	void *method_fp;
	void *return_address;
	int new_score = score;

	// Try the frame unwind.
	method_fp = method(context, frame, got_to_main, &new_score);

	// Make sure the result is reasonable.
	if (method_fp)
	{
		return_address = *((void **)method_fp + 1);
		if (!is_reasonable_code_pointer(return_address, context))
			method_fp = NULL;
	}

	// Don't adjust score if it didn't work.
	if (!method_fp)
		new_score = score;

	// Save previous frame and score offset.
	*previous_frame = method_fp;
	*score_offset = new_score - score;

	if (method_fp)
	{
		// Save this frame, and move on to the next.
		frames[count] = method_fp;
		framesMethod[count] = method_name;
		return stackwalk_frame(frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, method_fp, count + 1, context, objectInMainFrame, new_score + 1, best, memos, memo_count);
	}
	else
	{
		// End of callstack.
		check_best(frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, count, new_score, best, got_to_main);
		return new_score;
	}
}

// Order MemEntrys by frame position in memory.
int memo_compare(const void *lhs, const void *rhs)
{
	const MemoEntry *memo_lhs = lhs;
	const MemoEntry *memo_rhs = rhs;
	return (intptr_t)memo_lhs->frame - (intptr_t)memo_rhs->frame;
}

// Check if a stackwalk score has been saved.
const MemoEntry *stackwalk_frame_from_memo(const MemoEntry *memos, int memo_count, void *frame)
{
	MemoEntry frame_memo;
	size_t index;

	// Binary search to find score.
	frame_memo.frame = frame;
	index = bfind(&frame_memo, memos, memo_count, sizeof(MemoEntry), memo_compare);

	// Save it if found.
	if (index < (size_t)memo_count && memos[index].frame == frame)
		return memos + index;

	return NULL;
}

// Add a memo entry for a stackwalk score.
void stackwalk_frame_add_memo(MemoEntry *memos, int *memo_count, void *frame, int score,
	void *previous_frame, int score_offset, const char *method)
{
	MemoEntry frame_memo;
	size_t index;

	// Make sure there's room.
	if (*memo_count + 1 >= MAX_MEMO_ENTRIES)
		return;
	
	// Binary search to find insert position.
	frame_memo.frame = frame;
	index = bfind(&frame_memo, memos, *memo_count, sizeof(MemoEntry), memo_compare);

	// Shift existing entries up.
	memmove(memos + index + 1, memos + index, (*memo_count - index) * sizeof(MemoEntry));

	// Save memo data.
	memos[index].frame = frame;
	memos[index].score = score;
	memos[index].previous_frame = previous_frame;
	memos[index].score_offset = score_offset;
	memos[index].method = method;

	// Add count.
	++*memo_count;
}

// Debugging statistics for memoization, meant to be accessed by the debugger.
static int memo_debug_total = 0;
static int memo_debug_hits = 0;
static int memo_debug_early_return = 0;
static int memo_debug_quick_recurse = 0;

// Unwind one stack frame
// We try the methods in order of our preference.  That is, if two methods might produce the same score,
// but we can get to one by using a more reliable method, we use the more reliable method first.
//
// Note that this preference heuristic is more significant near the top of the stack, and it may have
// the undesirable effect of precluding better possibly unwindings near the bottom of the stack.  However, we
// expect that the top part of the stack will be the part that is harder to unwind, and more important
// to unwind accurately, for the sake of bucketing properly.
//
// On top of score, a callstack that gets all the way back to main always beats a callstack that doesn't,
// no matter how high the score is.
static int stackwalk_frame(void **frames,
	const char **framesMethod,
	void **bestFrames,
	const char **bestFramesMethod,
	int *bestFramesCount,
	bool *found_main,
	void *frame,
	int count,
	FallbackStackThreadContext *context,
	void *objectInMainFrame,
	int score,
	int *best,
	MemoEntry *memos, int *memo_count)
{
	int method_score;
	const MemoEntry *saved_memo;
	void *previous_frame;
	int score_offset;
	int best_score = INT_MIN;
	void *best_previous_frame = NULL;
	int best_score_offset = 0;
	const char *best_method = "";

	// Make sure we never exceed MAX_FRAME_COUNT frames.
	if (count >= MAX_FRAME_COUNT)
	{
		check_best(frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, count - 1, score, best, false);
		return score;
	}

	// Optimization: If we cannot possibly beat the best callstack, stop early.
	if (*found_main && score + (MAX_FRAME_COUNT - count) <= *best)
		return score;

	// See if we've already computed the stack from this point.
	// If so, do not proceed if it won't lead to a better result.
	++memo_debug_total;
	saved_memo = stackwalk_frame_from_memo(memos, *memo_count, frame);
	if (saved_memo)
	{
		++memo_debug_hits;
		if (score + saved_memo->score <= *best)
		{
			++memo_debug_early_return;
			return score;
		}

		// Quickly unwind the same way we did last time, without recomputing.
		if (saved_memo->previous_frame)
		{
			++memo_debug_quick_recurse;
			frames[count] = saved_memo->previous_frame;
			framesMethod[count] = saved_memo->method;
			return stackwalk_frame(frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, saved_memo->previous_frame, count + 1, context,
				objectInMainFrame, score + saved_memo->score_offset + 1, best, memos, memo_count);
		}
	}

	// Try each unwind method, in order of preference.
	method_score = try_frame_unwind_method(stackwalk_frame_fp, "fallback_fp",
		frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, frame, count, context, objectInMainFrame, score, best, memos, memo_count, &previous_frame, &score_offset);
	if (method_score > best_score)
	{
		best_score = method_score; 
		best_previous_frame = previous_frame;
		best_score_offset = score_offset;
		best_method = "fallback_fp";
	}
	//method_score = try_frame_unwind_method(stackwalk_frame_table, "fallback_table",
	//	frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, frame, count, context, objectInMainFrame, score, best, memos, memo_count, &previous_frame, &score_offset);
	//if (method_score > best_score)
	//{
	//	best_score = method_score; 
	//	best_previous_frame = previous_frame;
	//	best_score_offset = score_offset;
	//	best_method = "fallback_table";
	//}
	method_score = try_frame_unwind_method(stackwalk_frame_framesize, "fallback_framesize",
		frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, frame, count, context, objectInMainFrame, score, best, memos, memo_count, &previous_frame, &score_offset);
	if (method_score > best_score)
	{
		best_score = method_score; 
		best_previous_frame = previous_frame;
		best_score_offset = score_offset;
		best_method = "fallback_framesize";
	}
	method_score = try_frame_unwind_method(stackwalk_frame_callsite, "fallback_callsite",
		frames, framesMethod, bestFrames, bestFramesMethod, bestFramesCount, found_main, frame, count, context, objectInMainFrame, score, best, memos, memo_count, &previous_frame, &score_offset);
	if (method_score > best_score)
	{
		best_score = method_score; 
		best_previous_frame = previous_frame;
		best_score_offset = score_offset;
		best_method = "fallback_callsite";
	}

	// Save the best score achievable from this frame.
	if (!saved_memo)
		stackwalk_frame_add_memo(memos, memo_count, frame, best_score, best_previous_frame, best_score_offset, best_method);

	return best_score;
}

// Generate the text callstack by filling in module information.
static void stackwalk_generate_text(void **frames,
	const char **framesMethod,
	int count,
	FallbackStackThreadContext *context,
	char *buffer, size_t buffer_size,
	char *pCallstackReport, size_t pCallstackReport_size)
{
	int i;
	int frame_number = 0;
	char *return_addresses = NULL;

	// Start writing report.
	*buffer = 0;
	if (pCallstackReport)
	{
		*pCallstackReport = 0;
		return_addresses = alloca(pCallstackReport_size);
		return_addresses[0] = 0;
	}
	start_callstack_report(SAFESTR2(pCallstackReport));

	// Always print the top frame, if it's available.
	if (context->has_thread_context)
	{
		stackwalk_process_frame(context->topPc, frame_number, "ThreadContextIp", SAFESTR2(buffer), SAFESTR2(pCallstackReport), return_addresses, pCallstackReport_size);
		++frame_number;
	}

	// Print the frame just below the top frame, the frame pointed to by the frame pointer register.
	if (context->has_thread_context && is_reasonable_stack_pointer(context->topFramePointer, context))
	{
		void *return_address = *((void **)context->topFramePointer + 1);
		if (is_reasonable_code_pointer(return_address, context))
		{
			stackwalk_process_frame(return_address, frame_number, "ThreadContextBp", SAFESTR2(buffer), SAFESTR2(pCallstackReport), return_addresses, pCallstackReport_size);
			++frame_number;
		}
	}

	// Write each frame.
	for (i = 0; i != count; ++i, ++frame_number)
	{
		void *return_address = *((void **)frames[i] + 1);
		stackwalk_process_frame(return_address, frame_number, framesMethod[i], SAFESTR2(buffer), SAFESTR2(pCallstackReport), return_addresses, pCallstackReport_size);
	}

	// Finish report.
	finish_callstack_report(SAFESTR2(pCallstackReport), return_addresses);
}

// Perform a heuristic unwind of the call stack, choosing the best callstack from the variety of frame unwinding methods available.
// Each different frame unwinding method has a different score penalty, with a high-specificity method like frame pointer unwinding
// having the least penalty (none, in the current implementation) and a low-specificity (but more likely to generate an outcome)
// method like call site guessing has a high penalty.  From all combination of different unwinding methods for each frame, we
// choose the callstack that has the highest score.  This means we only use unreliable methods if necessary, and try to choose
// the method that is most likely to have generated something accurate.
// Additionally, we add one point to the score for each frame, which tends to preference longer callstacks.  Since the penalty for
// a method which might generate false frames is fairly high, this will generally not cause false frames to be introduced, but it will
// avoid a situation where a preferred mechanism, like frame pointer unwinding, has a fault that causes it to be miss much of the call
// stack.
static int fallback_generate_callstack(char *buffer, size_t buffer_size,
	FallbackStackThreadContext *context,
	char *pCallstackReport, size_t pCallstackReport_size,
	void *objectInMainFrame)
{
	void *frames[MAX_FRAME_COUNT];						// Current working call stack for stackwalk_frame()
	const char *framesMethod[MAX_FRAME_COUNT];			// Unwind method for each frame in 'frames'
	void *bestFrames[MAX_FRAME_COUNT];					// Current best complete callstack, copied from frames
	const char *bestFramesMethod[MAX_FRAME_COUNT];		// Unwind method for each frame in 'bestFrames'
	int count = 0;										// Number of frames in bestFrames
	int best = INT_MIN;									// Best score found so far
	bool found_main = false;							// True if one callstack has made it to main
	MemoEntry memos[MAX_MEMO_ENTRIES];					// Lazily-computed best possible scores for a frame
	int memo_count = 0;									// Number of entries in memos
	void *starting_frame_pointer = NULL;

	// If we have a thread context, try to use it as a starting point.
	// We generally have a thread context if this is a crash, meaning that OS has to tell us about the crashing frame, since there's no other way
	// to get information about it (such as the PC/IP value).  This is usually unnecessary if we're just doing a normal unwind where we aren't
	// starting from a specific context, since the irrelevant frames at the top of the stack will generally be trimmed.
	// It gets a score bonus so that it will tend to override the normal walk.
	if (context->has_thread_context && is_reasonable_stack_pointer(context->topFramePointer, context))
		stackwalk_frame(frames, framesMethod, bestFrames, bestFramesMethod, &count, &found_main, context->topFramePointer, 0, context, objectInMainFrame, 8, &best, memos, &memo_count);

	// Walk the stack recursively.
	// If we used a thread context above, try to see if we can do better by ignoring it and just unwinding.
	stackwalk_frame(frames, framesMethod, bestFrames, bestFramesMethod, &count, &found_main, context->boundingFramePointer, 0, context, objectInMainFrame, 0, &best, memos, &memo_count);

	// Generate text callstack.
	stackwalk_generate_text(bestFrames, bestFramesMethod, count, context, SAFESTR2(buffer), SAFESTR2(pCallstackReport));

	if (guStackwalkBoundaryScanTest & 0x0080)
		printf("---fallback_heuristic callstack:\n%s\n---report:\n%s\n------\n", buffer, NULL_TO_EMPTY(pCallstackReport));

	return count;
}

/*** External interface ***/

// Alternate implementation of stackWalkDumpStackToBuffer(), used when the former fails.  It not be called directly.
// This is a thin wrapper for fallback_stackwalk() that adds failure handling.
int fallback_stackWalkDumpStackToBuffer(char *stacktext, size_t stacktextSize, FallbackStackThreadContext *context, char *pCallstackReport, size_t pCallstackReportSize, void *objectInMainFrame, int best_so_far)
{
	int frames = 0;
	bool is64bit = false;

#if _WIN64
	is64bit = true;
#endif

	// TODO: This code should work for 64-bit, but it is untested.  It should be thoroughly tested, and any bugs fixed.
	if (is64bit)
		return 0;

	// Wrap callstack in exception wrapper so it can't crash.
	__try
	{

		// Allocate temporary buffer, in case fallback unwinding fails, and the original was better.
		char *temp_stacktext = alloca(stacktextSize);
		char *temp_callstack_report = NULL;
		temp_stacktext[0] = 0;
		if (pCallstackReport)
		{
			temp_callstack_report = alloca(pCallstackReportSize);
			temp_callstack_report[0] = 0;
		}

		// Attempt stackwalk.
		frames = fallback_generate_callstack(temp_stacktext, stacktextSize, context, temp_callstack_report, pCallstackReportSize, objectInMainFrame);

		// If we did better than the primary unwinder, copy the result out.
		if (frames > best_so_far || (guStackwalkBoundaryScanTest & 0x0200))
		{
			memcpy(stacktext, temp_stacktext, stacktextSize);
			if (pCallstackReport)
				memcpy(pCallstackReport, temp_callstack_report, pCallstackReportSize);
		}
	}
#pragma warning(suppress:6320)
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		stackWalkWarning(0, "fallback_stackWalkDumpStackToBuffer:Exception", GetExceptionCode());
	}

	return frames;
}

// Get a pointer to the Thread Information Block.
static void *get_tib()
{
	return NtCurrentTeb();
}

// Capture the current thread context, so we know what to unwind.
// All fallback unwinding is dependent on this function saving correct information.  Everything in it should be very safe.
// If we find problems with something not always working, we should augment it with a safer fallback.
void fallback_captureCurrentContext(FallbackStackThreadContext *context, void *tib, void *boundingFramePointer, bool has_thread_context, void *topPc, void *topFramePointer)
{
	__try
	{
		if (boundingFramePointer)
			context->boundingFramePointer = boundingFramePointer;
		else
			context->boundingFramePointer = *((void **)_AddressOfReturnAddress() - 1);
		if (tib)
			context->tib = tib;
		else
			context->tib = GetCurrentThreadTib();
		context->has_thread_context = has_thread_context;
		context->topPc = topPc;
		context->topFramePointer = topFramePointer;
	}
#pragma warning(suppress:6320)
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		stackWalkWarning(0, "fallback_captureCurrentContext:Exception", GetExceptionCode());
	}
}
