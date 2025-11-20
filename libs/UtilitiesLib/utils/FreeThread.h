/***************************************************************************



***************************************************************************/

// In order to use the FreeThread, just create a free function that takes a 
// pointer to your structure. Call FreeThreadQueue(structPtr, func), and the 
// structure will be passed to a background thread to be cleaned up. There 
// are built in perf timers for the subsidiary functions. These will work best 
// if there is only one call point for FreeThreadQueue for each callback 
// function. If you need to destroy from multiple places, I suggest creating
// a function to wrap the call to FreeThreadQueue.

// Command-line arguments:
// -FreeThreadNumThreads <n>: Sets the number of threads to use. Default: 1
// -FreeThreadInputQueueSize <n>: Sets the size of the input queue. Default: 16384
// -UseFreeThread <0/1>: Determines whether to actually use the thread or not.

typedef struct PerfInfoStaticData	PerfInfoStaticData;

typedef void (*FreeThreadFunction)(void *);

// This returns true if the userData will eventually get freed, either if it is
// queued to the background thread, or if it is freed immediately because the 
// thread is off.
#define FreeThreadQueue(userData, func)							\
	do															\
	{															\
		static PerfInfoStaticData* piStatic;					\
		FreeThreadQueueEx(userData, func, #func, &piStatic);	\
	} while(0)

// Setting this to false will turn off the threading and just immediately call the free function on userData when FreeThreadQueue is called.
// Changing this after the thread manager has been initialized will cause future calls to FreeThreadQueue to immediately process the input,
// but anything already in the input queue will still be processed in the background threads.
// default true (threading is on)
void EnableFreeThread(bool enable);

// Sets the number of threads to use. Changing this after the thread manager has been initialized has no effect.
// default 1
void FreeThreadSetNumThreads(int num);

// Sets the size of the input queue. Changing this after the thread manager has been initialized has no effect.
// default 16384
void FreeThreadSetInputQueueSize(int size);

// You should never call this directly. 
bool FreeThreadQueueEx(void *userData, FreeThreadFunction func, const char *funcName, PerfInfoStaticData **staticPtr);
