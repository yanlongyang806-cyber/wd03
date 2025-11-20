#ifndef _NETSENDTHREAD_H
#define _NETSENDTHREAD_H

void netCompressDebugFree(NetCompress* compress);
void commInitSendThread(NetComm *comm,int num_threads);
void commForceThreadFrame(NetComm* comm);

#endif
