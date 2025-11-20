#ifndef _NETBSD_H
#define _NETBSD_H

void commCheckBsdAccepts(NetComm *comm);
void bsdProcessPackets(NetComm *comm, S32 timeoutOverrideMilliseconds);

#endif