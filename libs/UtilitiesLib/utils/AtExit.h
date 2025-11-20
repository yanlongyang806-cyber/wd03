// Interface for requesting that a callback be run when threads exit
// This is mainly used for cleaning up per-thread resources so that they are not leaked on thread exit.
// All of the registration functions in this file are strongly thread-safe.

#ifndef CRYPTIC_ATEXIT_H
#define CRYPTIC_ATEXIT_H

// Called when a thread exits.
typedef void (*AtThreadExitCallback)(void *userdata, int thread_id, void *tls_value);

// Call callback when a thread exits, in the main thread, during timed callbacks.
void AtThreadExit(AtThreadExitCallback callback, void *userdata);

// Call callback when a thread exits, in the main thread, with the value of a TLS slot for that thread.
void AtThreadExitTls(AtThreadExitCallback callback, void *userdata, int slot);

// Call callback when a thread exits, in that thread's context.
// WARNING: This function is called by the thread destructor, so it must abide by the same restrictions as DllMain().
// Before writing one of these callbacks, please do the following:
// -Read and understand everything in the DllMain() documentation
// -Read and understand TlsCallback() in AtExit.c
// -Read and understand CrypticAtExitTlsCallback in AtExit.c and the supporting comments and associated web pages
void AtThreadExitInThread(AtThreadExitCallback callback, void *userdata);

#endif  // CRYPTIC_ATEXIT_H
