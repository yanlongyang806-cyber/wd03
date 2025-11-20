#include "AtExit.h"
#include "TimedCallback.h"
#include "wininclude.h"

// Maximum number of AtExit callbacks
#define MAX_CALLBACKS 32

// Callbacks
static volatile AtThreadExitCallback callbacks[MAX_CALLBACKS] = {0};

// Userdata for callbacks
static void *volatile callbackuserdata[MAX_CALLBACKS] = {0};

// TLS slots for callbacks
static volatile int callbackslot[MAX_CALLBACKS] = {0};

// Highest-allocated callback.
static long callback_index = 0;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

// For debugging, profiling, etc.
bool gbDisableAtExit = false;
AUTO_CMD_INT(gbDisableAtExit, DisableAtExit);

// Run when a thread is destroyed.
static int ThreadExit()
{
	DWORD thread_id;
	int index;

	// Clear exception flags. This is because the Cryptic exception handler may
	// enable exceptions, depending on the process FPU/SSE exception handling options. If
	// the thread has been operating with numeric exceptions masked, then FPU instructions
	// in _controlfp may actually issue exceptions after they are unmasked.
	_clearfp();

	EXCEPTION_HANDLER_BEGIN

	// Get current thread ID.
	thread_id = GetCurrentThreadId();

	// Call each TLS callback.
	for (index = 0; callbacks[index] && index < MAX_CALLBACKS; ++index)
	{
		void *value = NULL;
		AtThreadExitCallback callback;
		if (callbackslot[index])
		{
			int slot = callbackslot[index] - 1;
			value = TlsGetValue(slot);
		}
		callback = callbacks[index];
		callback(callbackuserdata[index], thread_id, value);
	}

	EXCEPTION_HANDLER_END
	return 0;
}

// Run ThreadExit() when a thread is destroyed.
static void NTAPI TlsCallback(PVOID DllHandle, DWORD Reason, PVOID Reserved)
{
	if(Reason == DLL_THREAD_DETACH && !gbDisableAtExit)
		ThreadExit();
}

// Arrange for TlsCallback to be called by the OS.
// See the following links for an explanation of this mechanism:
// http://www.codeproject.com/Articles/8113/Thread-Local-Storage-The-C-Way
// http://www.nynaeve.net/?p=183
#ifdef _MSC_VER
#if defined(_M_X64)
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma const_seg(".CRT$XLB")
const PIMAGE_TLS_CALLBACK CrypticAtExitTlsCallback = TlsCallback;
#pragma const_seg()
#elif defined(_M_IX86)
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK CrypticAtExitTlsCallback = TlsCallback;
#pragma data_seg()
#else
#error TLS destructors: Unsupported Windows machine
#endif
#else
#error TLS destructors: Unsupported compiler
#endif

// Call callback when a thread exits, in the main thread, during timed callbacks.
void AtThreadExit(AtThreadExitCallback callback, void *userdata)
{
	AtThreadExitTls(callback, userdata, TLS_OUT_OF_INDEXES);
}

// Callback information to be executed in the main thread.
struct callback_data {
	AtThreadExitCallback callback;
	void *userdata;
	int thread_id;
	void *tls_value;
};

// Execute callback, now in the context of the main thread.
static void AtExitTimedCallbackFunc(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	struct callback_data *data = userData;
	data->callback(data->userdata, data->thread_id, data->tls_value);
	free(data);
}

// Original callback and userdata for internal callback.
struct callback_closure {
	AtThreadExitCallback callback;
	void *userdata;
};

// Dispatch to the main thread via TimedCallback.
static void AtThreadExitTlsCallback(void *userdata, int thread_id, void *tls_value)
{
	struct callback_closure *closure = userdata;
	struct callback_data *data;

	// Create callback data object.
	data = malloc(sizeof(*data));
	data->callback = closure->callback;
	data->userdata = closure->userdata;
	data->thread_id = thread_id;
	data->tls_value = tls_value;
	
	// Schedule timed callback.
	TimedCallback_Run(AtExitTimedCallbackFunc, data, 0);
}

// Call callback when a thread exits, in that thread's context, with TLS slot.
static void AtThreadExitInThreadSlot(AtThreadExitCallback callback, void *userdata, int slot)
{
	long index = InterlockedIncrement(&callback_index) - 1;
	callbackuserdata[index] = userdata;
	callbackslot[index] = slot + 1;
	callbacks[index] = callback;
}

// Call callback when a thread exits, in the main thread, with the value of a TLS slot for that thread.
void AtThreadExitTls(AtThreadExitCallback callback, void *userdata, int slot)
{
	struct callback_closure *closure = malloc(sizeof(*closure));
	closure->callback = callback;
	closure->userdata = userdata;
	AtThreadExitInThreadSlot(AtThreadExitTlsCallback, closure, slot);
}

// Call callback when a thread exits, in that thread's context.
void AtThreadExitInThread(AtThreadExitCallback callback, void *userdata)
{
	AtThreadExitInThreadSlot(callback, userdata, TLS_OUT_OF_INDEXES);
}
