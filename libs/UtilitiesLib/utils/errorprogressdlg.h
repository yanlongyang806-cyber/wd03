#ifndef ERRORPROGRESSDLG_H
#define ERRORPROGRESSDLG_H

typedef void (*errorProgressDlgUpdateCallback)(size_t iSentBytes, size_t iTotalBytes);
void errorProgressDlgSetUpdateCallback(errorProgressDlgUpdateCallback cb);

void errorProgressDlgResetCallbacks(void);

void errorProgressDlgInit(const char *pCrashInfo);
void errorProgressDlgPump(); // Pumps windows messages
void errorProgressDlgUpdate(size_t iSentBytes, size_t iTotalBytes);
void errorProgressDlgShutdown(void);
void errorProgressDlgCancel(void);

// Cancelled from Error Tracker's side or through other automatic termination
bool errorProgressDlgIsCancelled(void);
// Cancelled from the client side
bool errorProgressDlgWasExplicitlyCancelled(void);

#endif
